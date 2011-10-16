/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2002 by Alan Korr & Nick Robinson
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "panic.h"
#include "iap.h"
#include "button.h"
#include "config.h"
#include "cpu.h"
#include "system.h"
#include "kernel.h"
#include "serial.h"
#include "appevents.h"

#include "playlist.h"
#include "playback.h"
#include "audio.h"
#include "settings.h"
#include "metadata.h"
#include "wps.h"
#include "sound.h"
#include "action.h"
#include "powermgmt.h"

#include "tuner.h"
#include "ipod_remote_tuner.h"

#include "filetree.h"
#include "dir.h"

static volatile int iap_pollspeed = 0;
static volatile bool iap_remotetick = true;
static bool iap_setupflag = false, iap_updateflag = false;
static int iap_changedctr = 0;

static unsigned long iap_remotebtn = 0;
static int iap_repeatbtn = 0;
static bool iap_btnrepeat = false, iap_btnshuffle = false;

static unsigned char serbuf[RX_BUFLEN];

static unsigned char response[TX_BUFLEN];

static char cur_dbrecord[5] = {0};

/* states of the iap de-framing state machine */
enum fsm_state {
    ST_SYNC,    /* wait for 0xFF sync byte */
    ST_SOF,     /* wait for 0x55 start-of-frame byte */
    ST_LEN,     /* receive length byte (small packet) */
    ST_LENH,    /* receive length high byte (large packet) */
    ST_LENL,    /* receive length low byte (large packet) */
    ST_DATA,    /* receive data */
    ST_CHECK    /* verify checksum */
};

static struct state_t {
    enum fsm_state state;   /* current fsm state */
    unsigned int len;       /* payload data length */
    unsigned char *payload; /* payload data pointer */
    unsigned int check;     /* running checksum over [len,payload,check] */
    unsigned int count;     /* playload bytes counter */
} frame_state = {
    .state = ST_SYNC
};

static void put_u32(unsigned char *buf, uint32_t data)
{
    buf[0] = (data >> 24) & 0xFF;
    buf[1] = (data >> 16) & 0xFF;
    buf[2] = (data >>  8) & 0xFF;
    buf[3] = (data >>  0) & 0xFF;
}

static uint32_t get_u32(const unsigned char *buf)
{
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static void iap_task(void)
{
    static int count = 0;

    count += iap_pollspeed;
    if (count < (500/10)) return;

    /* exec every 500ms if pollspeed == 1 */
    count = 0;
    queue_post(&button_queue, SYS_IAP_PERIODIC, 0);
}

/* called by playback when the next track starts */
static void iap_track_changed(void *ignored)
{
    (void)ignored;
    iap_changedctr = 1;
}

void iap_setup(int ratenum)
{
    iap_bitrate_set(ratenum);
    iap_pollspeed = 0;
    iap_remotetick = true;
    iap_updateflag = false;
    iap_changedctr = 0;
    iap_setupflag = true;
    iap_remotebtn = BUTTON_NONE;
    tick_add_task(iap_task);
    add_event(PLAYBACK_EVENT_TRACK_CHANGE, false, iap_track_changed);
}

void iap_bitrate_set(int ratenum)
{
    switch(ratenum)
    {
        case 0:
            serial_bitrate(0);
            break;
        case 1:
            serial_bitrate(9600);
            break;
        case 2:
            serial_bitrate(19200);
            break;
        case 3:
            serial_bitrate(38400);
            break;
        case 4:
            serial_bitrate(57600);
            break;
    }
}

/* Message format:
   0xff
   0x55
   length
   mode
   command (2 bytes)
   parameters (0-n bytes)
   checksum (length+mode+parameters+checksum == 0)
*/

void iap_send_pkt(const unsigned char * data, int len)
{
    int i, chksum, responselen;
    
    if(len > TX_BUFLEN-4) len = TX_BUFLEN-4;
    responselen = len + 4;
    
    response[0] = 0xFF;
    response[1] = 0x55;
    
    chksum = response[2] = len;
    for(i = 0; i < len; i ++)
    {
        chksum += data[i];
        response[i+3] = data[i];
    }

    response[i+3] = 0x100 - (chksum & 0xFF);
    
    for(i = 0; i < responselen; i ++)
    {
        while (!tx_rdy()) ;
        tx_writec(response[i]);
    }
}    

bool iap_getc(unsigned char x)
{
    struct state_t *s = &frame_state;
    
    /* run state machine to detect and extract a valid frame */
    switch (s->state) {
    case ST_SYNC:
        if (x == 0xFF) {
            s->state = ST_SOF;
        }
        break;
    case ST_SOF:
        if (x == 0x55) {
            /* received a valid sync/SOF pair */
            s->state = ST_LEN;
        } else {
            s->state = ST_SYNC;
            return iap_getc(x);
        }
        break;
    case ST_LEN:
        s->check = x;
        s->count = 0;
        s->payload = serbuf;
        if (x == 0) {
            /* large packet */
            s->state = ST_LENH;
        } else {
            /* small packet */
            s->len = x;
            s->state = ST_DATA;
        }
        break;
    case ST_LENH:
        s->check += x;
        s->len = x << 8;
        s->state = ST_LENL;
        break;
    case ST_LENL:
        s->check += x;
        s->len += x;
        if ((s->len == 0) || (s->len > RX_BUFLEN)) {
            /* invalid length */
            s->state = ST_SYNC;
            return iap_getc(x);
        } else {
            s->state = ST_DATA;
        }
        break;
    case ST_DATA:
        s->check += x;
        s->payload[s->count++] = x;
        if (s->count == s->len) {
            s->state = ST_CHECK;
        }
        break;
    case ST_CHECK:
        s->check += x;
        if ((s->check & 0xFF) == 0) {
            /* done, received a valid frame */
            queue_post(&button_queue, SYS_IAP_HANDLEPKT, 0);
        }
        s->state = ST_SYNC;
        break;
    default:
        panicf("Unhandled iap state %d", (int) s->state);
        break;
    }
    
    /* return true while still hunting for the sync and start-of-frame byte */
    return (s->state == ST_SYNC) || (s->state == ST_SOF);
}

void iap_periodic(void)
{
    if(!iap_setupflag) return;
    if(!iap_pollspeed) return;
    
    /* PlayStatusChangeNotification */
    unsigned char data[] = {0x04, 0x00, 0x27, 0x04, 0x00, 0x00, 0x00, 0x00};
    unsigned long time_elapsed = audio_current_track()->elapsed;

    time_elapsed += wps_get_ff_rewind_count();

    data[3] = 0x04; /* playing */

    /* If info has changed, don't flag it right away */
    if(iap_changedctr && iap_changedctr++ >= iap_pollspeed * 2)
    {
        /* track info has changed */
        iap_changedctr = 0;
        data[3] = 0x01; /* 0x02 has same effect?  */
        iap_updateflag = true;
    }

    put_u32(&data[4], time_elapsed);
    iap_send_pkt(data, sizeof(data));
}

static void iap_set_remote_volume(void)
{
    unsigned char data[] = {0x03, 0x0D, 0x04, 0x00, 0x00};
    data[4] = (char)((global_settings.volume+58) * 4);
    iap_send_pkt(data, sizeof(data));
}

static void cmd_ok_mode0(unsigned char cmd)
{
    unsigned char data[] = {0x00, 0x02, 0x00, 0x00};
    data[3] = cmd;  /* respond with cmd */
    iap_send_pkt(data, sizeof(data));
}

static void iap_handlepkt_mode0(unsigned int len, const unsigned char *buf)
{
    (void)len;    /* len currently unused */
    
    unsigned int cmd = buf[1];
    switch (cmd) {
        /* Identify */
        case 0x01:
        {
            /* FM transmitter sends this: */
            /* FF 55 06 00 01 05 00 02 01 F1 (mode switch) */
            if(buf[2] == 0x05)
            {
                sleep(HZ/3);
                /* RF Transmitter: Begin transmission */
                unsigned char data[] = {0x05, 0x02};
                iap_send_pkt(data, sizeof(data));
            }
            /* FM remote sends this: */
            /* FF 55 03 00 01 02 FA (1st thing sent) */
            else if (buf[2] == 0x02)
            {
                /* useful only for apple firmware */
            }
            break;
        }
    
        /* EnterRemoteUIMode, FM transmitter sends FF 55 02 00 05 F9 */
        case 0x05:
        {
            /* ACK Pending (3000 ms) */
            unsigned char data[] = {0x00, 0x02, 0x06,
                                    0x05, 0x00, 0x00, 0x0B, 0xB8};
            iap_send_pkt(data, sizeof(data));
            cmd_ok_mode0(cmd);
            break;
        }

        /* ExitRemoteUIMode */
        case 0x06:
        {
            audio_stop();
            cmd_ok_mode0(cmd);
            break;
        }

        /* RequestiPodSoftwareVersion, Ipod FM remote sends FF 55 02 00 09 F5 */
        case 0x09:
        {
            /* ReturniPodSoftwareVersion, ipod5G firmware version */
            unsigned char data[] = {0x00, 0x0A, 0x01, 0x02, 0x01};
            iap_send_pkt(data, sizeof(data));
            break;
        } 

        /* RequestiPodModelNum */
        case 0x0D:
        {
            /* ipod is supposed to work only with 5G and nano 2G */
            /*{0x00, 0x0E, 0x00, 0x0B, 0x00, 0x05, 0x50, 0x41, 0x31, 0x34, 
                    0x37, 0x4C, 0x4C, 0x00};    PA147LL (IPOD 5G 60 GO) */
            /* ReturniPodModelNum */
            unsigned char data[] = {0x00, 0x0E, 0x00, 0x0B, 0x00, 0x10,
                                'R', 'O', 'C', 'K', 'B', 'O', 'X', 0x00};
            iap_send_pkt(data, sizeof(data));
            break;
        }

        /* RequestLingoProtocolVersion */
        case 0x0F:
        {
            /* ReturnLingoProtocolVersion */
            unsigned char data[] = {0x00, 0x10, 0x00, 0x01, 0x05};
            data[2] = buf[2];
            iap_send_pkt(data, sizeof(data));
            break;
        }

        /* IdentifyDeviceLingoes */
        case 0x13:
        {
            cmd_ok_mode0(cmd);

            uint32_t lingoes = get_u32(&buf[2]);

            if (lingoes == 0x35)
            /* FM transmitter sends this: */
            /* FF 55 0E 00 13 00 00 00 35 00 00 00 04 00 00 00 00 A6 (??)*/
            {
                /* GetAccessoryInfo */
                unsigned char data2[] = {0x00, 0x27, 0x00};
                iap_send_pkt(data2, sizeof(data2));
                /* RF Transmitter: Begin transmission */
                unsigned char data3[] = {0x05, 0x02};
                iap_send_pkt(data3, sizeof(data3));
            }
            else
            {
                /* ipod fm remote sends this: */ 
                /* FF 55 0E 00 13 00 00 00 8D 00 00 00 0E 00 00 00 03 41 */
                if (lingoes & (1 << 7)) /* bit 7 = RF tuner lingo */
                    radio_present = 1;
                /* GetDevAuthenticationInfo */    
                unsigned char data4[] = {0x00, 0x14};
                iap_send_pkt(data4, sizeof(data4));
            }
            break;
        }

        /* RetDevAuthenticationInfo */
        case 0x15:
        {
            /* AckDevAuthenticationInfo */
            unsigned char data0[] = {0x00, 0x16, 0x00};
            iap_send_pkt(data0, sizeof(data0));
            /* GetAccessoryInfo */
            unsigned char data1[] = {0x00, 0x27, 0x00};
            iap_send_pkt(data1, sizeof(data1));
            /* AckDevAuthenticationStatus, mandatory to enable some hardware */
            unsigned char data2[] = {0x00, 0x19, 0x00};
            iap_send_pkt(data2, sizeof(data2));
            if (radio_present == 1)
            {
                /* GetTunerCaps */
                unsigned char data3[] = {0x07, 0x01};
                iap_send_pkt(data3, sizeof(data3));
            }
            iap_set_remote_volume();
            break;
        }

        /* RetDevAuthenticationSignature */
        case 0x18:
        {
            /* Isn't used since we don't send the 0x00 0x17 command */
            break;
        }

        /* GetIpodOptions */
        case 0x24:
        {
            /* RetIpodOptions (ipod video send this) */
            unsigned char data[] = {0x00, 0x25, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x01};
            iap_send_pkt(data, sizeof(data));
            break;
        }
        
        /* default response is with cmd ok packet */
        default:
        {
            cmd_ok_mode0(cmd);
            break;
        }
    }
}

static void iap_handlepkt_mode2(unsigned int len, const unsigned char *buf)
{
    if(buf[1] != 0) return;
    iap_remotebtn = BUTTON_NONE;
    iap_remotetick = false;
    
    if(len >= 3 && buf[2] != 0)
    {
        if(buf[2] & 1)
            iap_remotebtn |= BUTTON_RC_PLAY;
        if(buf[2] & 2)
            iap_remotebtn |= BUTTON_RC_VOL_UP;
        if(buf[2] & 4)
            iap_remotebtn |= BUTTON_RC_VOL_DOWN;
        if(buf[2] & 8)
            iap_remotebtn |= BUTTON_RC_RIGHT;
        if(buf[2] & 16)
            iap_remotebtn |= BUTTON_RC_LEFT;
    }
    else if(len >= 4 && buf[3] != 0)
    {
        if(buf[3] & 1) /* play */
        {
            if (audio_status() != AUDIO_STATUS_PLAY)
            {
                iap_remotebtn |= BUTTON_RC_PLAY;
                iap_repeatbtn = 2;
                iap_remotetick = false;
                iap_changedctr = 1;
            }
        }
        if(buf[3] & 2) /* pause */
        {
            if (audio_status() == AUDIO_STATUS_PLAY)
            {
                iap_remotebtn |= BUTTON_RC_PLAY;
                iap_repeatbtn = 2;
                iap_remotetick = false;
                iap_changedctr = 1;
            }
        }
        if((buf[3] & 128) && !iap_btnshuffle) /* shuffle */
        {
            iap_btnshuffle = true;
            if(!global_settings.playlist_shuffle)
            {
                global_settings.playlist_shuffle = 1;
                settings_save();
                if (audio_status() & AUDIO_STATUS_PLAY)
                    playlist_randomise(NULL, current_tick, true);
            }
            else if(global_settings.playlist_shuffle)
            {
                global_settings.playlist_shuffle = 0;
                settings_save();
                if (audio_status() & AUDIO_STATUS_PLAY)
                    playlist_sort(NULL, true);
            }
        }
        else
            iap_btnshuffle = false;
    }
    else if(len >= 5 && buf[4] != 0)
    {
        if((buf[4] & 1) && !iap_btnrepeat) /* repeat */
        {
            int oldmode = global_settings.repeat_mode;
            iap_btnrepeat = true;
        
            if (oldmode == REPEAT_ONE)
                    global_settings.repeat_mode = REPEAT_OFF;
            else if (oldmode == REPEAT_ALL)
                    global_settings.repeat_mode = REPEAT_ONE;
            else if (oldmode == REPEAT_OFF)
                    global_settings.repeat_mode = REPEAT_ALL;

            settings_save();
            if (audio_status() & AUDIO_STATUS_PLAY)
            audio_flush_and_reload_tracks();
        }
        else
            iap_btnrepeat = false;

        if(buf[4] & 16) /* ffwd */
        {
            iap_remotebtn |= BUTTON_RC_RIGHT;
        }
        if(buf[4] & 32) /* frwd */
        {
            iap_remotebtn |= BUTTON_RC_LEFT;
        }
    }
}

static void iap_handlepkt_mode3(unsigned int len, const unsigned char *buf)
{
    (void)len;    /* len currently unused */

    unsigned int cmd = buf[1];
    switch (cmd)
    {
        /* GetCurrentEQProfileIndex */
        case 0x01:
        {
            /* RetCurrentEQProfileIndex */
            unsigned char data[] = {0x03, 0x02, 0x00, 0x00, 0x00, 0x00};
            iap_send_pkt(data, sizeof(data));
            break;
        }

        /* SetRemoteEventNotification */
        case 0x08:
        {
            /* ACK */
            unsigned char data[] = {0x03, 0x00, 0x00, 0x08};
            iap_send_pkt(data, sizeof(data));
            break;
        }
        
        /* GetiPodStateInfo */
        case 0x0C:
        {
            /* request ipod volume */
            if (buf[2] == 0x04)
            {
                iap_set_remote_volume();
            }
            break;
        }
        
        /* SetiPodStateInfo */
        case 0x0E:
        {
            if (buf[2] == 0x04)
                global_settings.volume = (-58)+((int)buf[4]+1)/4;
                sound_set_volume(global_settings.volume);   /* indent BUG? */
            break;
        }
    }
}

static void cmd_ok_mode4(unsigned int cmd)
{
    unsigned char data[] = {0x04, 0x00, 0x01, 0x00, 0x00, 0x00};
    data[4] = (cmd >> 8) & 0xFF;
    data[5] = (cmd >> 0) & 0xFF;
    iap_send_pkt(data, sizeof(data));
}

static void get_playlist_name(unsigned char *dest,
                              unsigned long item_offset,
                              size_t max_length)
{
    if (item_offset == 0) return;
    DIR* dp;
    struct dirent* playlist_file = NULL;

    dp = opendir(global_settings.playlist_catalog_dir);

    char *extension;
    unsigned long nbr = 0;
    while ((nbr < item_offset) && ((playlist_file = readdir(dp)) != NULL))
    {
        /*Increment only if there is a playlist extension*/
        if ((extension=strrchr(playlist_file->d_name, '.')) != NULL){
            if ((strcmp(extension, ".m3u") == 0 || 
                strcmp(extension, ".m3u8") == 0))
                nbr++;
        }
    }
    if (playlist_file != NULL) {
        strlcpy(dest, playlist_file->d_name, max_length);
    }
    closedir(dp);
}

static void iap_handlepkt_mode4(unsigned int len, const unsigned char *buf)
{
    (void)len;    /* len currently unused */

    unsigned int cmd = (buf[1] << 8) | buf[2];
    switch (cmd)
    {
        /* GetAudioBookSpeed */
        case 0x0009:
        {
            /* ReturnAudioBookSpeed */
            unsigned char data[] = {0x04, 0x00, 0x0A, 0x00};
            data[3] = iap_updateflag ? 0 : 1;
            iap_send_pkt(data, sizeof(data));
            break;
        }
        
        /* SetAudioBookSpeed */
        case 0x000B:
        {
            iap_updateflag = buf[3] ? 0 : 1;
            /* respond with cmd ok packet */
            cmd_ok_mode4(cmd);
            break;
        }
        
        /* RequestProtocolVersion */
        case 0x0012:
        {
            /* ReturnProtocolVersion */
            unsigned char data[] = {0x04, 0x00, 0x13, 0x01, 0x0B};
            iap_send_pkt(data, sizeof(data));
            break;
        }

        /* SelectDBRecord */
        case 0x0017:
        {
            memcpy(cur_dbrecord, buf + 3, 5);
            cmd_ok_mode4(cmd);
            break;
        }

        /* GetNumberCategorizedDBRecords */
        case 0x0018:
        {
            /* ReturnNumberCategorizedDBRecords */
            unsigned char data[] = {0x04, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00};
            unsigned long num = 0;
            
            DIR* dp;
            unsigned long nbr_total_playlists = 0;
            struct dirent* playlist_file = NULL;
            char *extension;

            switch(buf[3]) /* type number */
            {
                case 0x01: /* total number of playlists */
                    dp = opendir(global_settings.playlist_catalog_dir);
                    while ((playlist_file = readdir(dp)) != NULL)
                    {
                    /*Increment only if there is a playlist extension*/
                        if ((extension=strrchr(playlist_file->d_name, '.'))
                        != NULL) {
                            if ((strcmp(extension, ".m3u") == 0 || 
                                strcmp(extension, ".m3u8") == 0))
                                nbr_total_playlists++;
                        }
                    }
                    closedir(dp);
                    /*Add 1 for the main playlist*/
                    num = nbr_total_playlists + 1;
                    break;
                case 0x05: /* total number of songs */
                    num = 1;
                    break;
            }
            put_u32(&data[3], num);
            iap_send_pkt(data, sizeof(data));
            break;
        }
        
        /* RetrieveCategorizedDatabaseRecords */
        case 0x001A:
        {
            /* ReturnCategorizedDatabaseRecord */
            unsigned char data[7 + MAX_PATH] = 
                            {0x04, 0x00, 0x1B, 0x00, 0x00, 0x00, 0x00,
                             'R', 'O', 'C', 'K', 'B', 'O', 'X', '\0'};
            
            unsigned long item_offset = get_u32(&buf[4]);

            get_playlist_name(data + 7, item_offset, MAX_PATH);
            /*Remove file extension*/
            char *dot=NULL;
            dot = (strrchr(data+7, '.'));
            if (dot != NULL)
                *dot = '\0';
            iap_send_pkt(data, 7 + strlen(data+7) + 1);
            break;
        }
        
        /* GetPlayStatus */
        case 0x001C:
        {
            /* ReturnPlayStatus */
            unsigned char data[] = {0x04, 0x00, 0x1D, 0x00, 0x00, 0x00, 
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            struct mp3entry *id3 = audio_current_track();
            unsigned long time_total = id3->length;
            unsigned long time_elapsed = id3->elapsed;
            int status = audio_status();
            put_u32(&data[3], time_total);
            put_u32(&data[7], time_elapsed);
            if (status == AUDIO_STATUS_PLAY)
                data[11] = 0x01; /* play */
            else if (status & AUDIO_STATUS_PAUSE)
                data[11] = 0x02; /* pause */ 
            iap_send_pkt(data, sizeof(data));
            break;
        }
        
        /* GetCurrentPlayingTrackIndex */
        case 0x001E:
        {
            /* ReturnCurrentPlayingTrackIndex */
            unsigned char data[] = {0x04, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00};
            long playlist_pos = playlist_next(0);
            playlist_pos -= playlist_get_first_index(NULL);
            if(playlist_pos < 0)
                playlist_pos += playlist_amount();
            put_u32(&data[3], playlist_pos);
            iap_send_pkt(data, sizeof(data));
            break;
        }
        
        /* GetIndexedPlayingTrackTitle */
        case 0x0020:
        /* GetIndexedPlayingTrackArtistName */
        case 0x0022:
        /* GetIndexedPlayingTrackAlbumName */
        case 0x0024:
        {
            unsigned char data[70] = {0x04, 0x00, 0xFF};
            struct mp3entry id3;
            int fd;
            size_t len;
            long tracknum = get_u32(&buf[3]);
            
            data[2] = cmd + 1;
            memcpy(&id3, audio_current_track(), sizeof(id3));
            tracknum += playlist_get_first_index(NULL);
            if(tracknum >= playlist_amount())
                tracknum -= playlist_amount();

            /* If the tracknumber is not the current one,
               read id3 from disk */
            if(playlist_next(0) != tracknum)
            {
                struct playlist_track_info info;
                playlist_get_track_info(NULL, tracknum, &info);
                fd = open(info.filename, O_RDONLY);
                memset(&id3, 0, sizeof(struct mp3entry));
                get_metadata(&id3, fd, info.filename);
                close(fd);
            }

            /* Return the requested track data */
            switch(cmd)
            {
                case 0x20:
                    len = strlcpy((char *)&data[3], id3.title, 64);
                    iap_send_pkt(data, 4+len);
                    break;
                case 0x22:
                    len = strlcpy((char *)&data[3], id3.artist, 64);
                    iap_send_pkt(data, 4+len);
                    break;
                case 0x24:
                    len = strlcpy((char *)&data[3], id3.album, 64);
                    iap_send_pkt(data, 4+len);
                    break;
            }
            break;
        }
        
        /* SetPlayStatusChangeNotification */
        case 0x0026:
        {
            iap_pollspeed = buf[3] ? 1 : 0;
            /* respond with cmd ok packet */
            cmd_ok_mode4(cmd);
            break;
        }
        
        /* PlayCurrentSelection */
        case 0x0028:
        {
            switch (cur_dbrecord[0])
            {
                case 0x01:
                {/*Playlist*/
                    unsigned long item_offset = get_u32(&cur_dbrecord[1]);
                    
                    unsigned char selected_playlist
                    [sizeof(global_settings.playlist_catalog_dir)
                    + 1
                    + MAX_PATH] = {0};

                    strcpy(selected_playlist,
                            global_settings.playlist_catalog_dir);
                    int len = strlen(selected_playlist);
                    selected_playlist[len] = '/';
                    get_playlist_name (selected_playlist + len + 1,
                                       item_offset,
                                       MAX_PATH);
                    ft_play_playlist(selected_playlist,
                                     global_settings.playlist_catalog_dir,
                                     strrchr(selected_playlist, '/') + 1);
                    break;
                }
            }
            cmd_ok_mode4(cmd);
            break;
        }
        
        /* PlayControl */
        case 0x0029:
        {
            switch(buf[3])
            {
                case 0x01: /* play/pause */
                    iap_remotebtn = BUTTON_RC_PLAY;
                    iap_repeatbtn = 2;
                    iap_remotetick = false;
                    iap_changedctr = 1;
                    break;
                case 0x02: /* stop */
                    iap_remotebtn = BUTTON_RC_PLAY|BUTTON_REPEAT;
                    iap_repeatbtn = 2;
                    iap_remotetick = false;
                    iap_changedctr = 1;
                    break;
                case 0x03: /* skip++ */
                    iap_remotebtn = BUTTON_RC_RIGHT;
                    iap_repeatbtn = 2;
                    iap_remotetick = false;
                    break;
                case 0x04: /* skip-- */
                    iap_remotebtn = BUTTON_RC_LEFT;
                    iap_repeatbtn = 2;
                    iap_remotetick = false;
                    break;
                case 0x05: /* ffwd */
                    iap_remotebtn = BUTTON_RC_RIGHT;
                    iap_remotetick = false;
                    if(iap_pollspeed) iap_pollspeed = 5;
                    break;
                case 0x06: /* frwd */
                    iap_remotebtn = BUTTON_RC_LEFT;
                    iap_remotetick = false;
                    if(iap_pollspeed) iap_pollspeed = 5;
                    break;
                case 0x07: /* end ffwd/frwd */
                    iap_remotebtn = BUTTON_NONE;
                    iap_remotetick = false;
                    if(iap_pollspeed) iap_pollspeed = 1;
                    break;
            }
            /* respond with cmd ok packet */
            cmd_ok_mode4(cmd);
            break;
        }
        
        /* GetShuffle */
        case 0x002C:
        {
            /* ReturnShuffle */
            unsigned char data[] = {0x04, 0x00, 0x2D, 0x00};
            data[3] = global_settings.playlist_shuffle ? 1 : 0;
            iap_send_pkt(data, sizeof(data));
            break;
        }
        
        /* SetShuffle */
        case 0x002E:
        {
            if(buf[3] && !global_settings.playlist_shuffle)
            {
                global_settings.playlist_shuffle = 1;
                settings_save();
                if (audio_status() & AUDIO_STATUS_PLAY)
                    playlist_randomise(NULL, current_tick, true);
            }
            else if(!buf[3] && global_settings.playlist_shuffle)
            {
                global_settings.playlist_shuffle = 0;
                settings_save();
                if (audio_status() & AUDIO_STATUS_PLAY)
                    playlist_sort(NULL, true);
            }

            /* respond with cmd ok packet */
            cmd_ok_mode4(cmd);
            break;
        }
        
        /* GetRepeat */
        case 0x002F:
        {
            /* ReturnRepeat */
            unsigned char data[] = {0x04, 0x00, 0x30, 0x00};
            if(global_settings.repeat_mode == REPEAT_OFF)
                data[3] = 0;
            else if(global_settings.repeat_mode == REPEAT_ONE)
                data[3] = 1;
            else
                data[3] = 2;
            iap_send_pkt(data, sizeof(data));
            break;
        }
        
        /* SetRepeat */
        case 0x0031:
        {
            int oldmode = global_settings.repeat_mode;
            if (buf[3] == 0)
                global_settings.repeat_mode = REPEAT_OFF;
            else if (buf[3] == 1)
                global_settings.repeat_mode = REPEAT_ONE;
            else if (buf[3] == 2)
                global_settings.repeat_mode = REPEAT_ALL;

            if (oldmode != global_settings.repeat_mode)
            {
                settings_save();
                if (audio_status() & AUDIO_STATUS_PLAY)
                    audio_flush_and_reload_tracks();
            }

            /* respond with cmd ok packet */
            cmd_ok_mode4(cmd);
            break;
        }
        
        /* GetMonoDisplayImageLimits */
        case 0x0033:
        {
            /* ReturnMonoDisplayImageLimits */
            unsigned char data[] = {0x04, 0x00, 0x34,
                                    LCD_WIDTH >> 8, LCD_WIDTH & 0xff,
                                    LCD_HEIGHT >> 8, LCD_HEIGHT & 0xff,
                                    0x01};
            iap_send_pkt(data, sizeof(data));
            break;
        }
        
        /* GetNumPlayingTracks */
        case 0x0035:
        {
            /* ReturnNumPlayingTracks */
            unsigned char data[] = {0x04, 0x00, 0x36, 0x00, 0x00, 0x00, 0x00};
            unsigned long playlist_amt = playlist_amount();
            put_u32(&data[3], playlist_amt);
            iap_send_pkt(data, sizeof(data));
            break;
        }
        
        /* SetCurrentPlayingTrack */
        case 0x0037:
        {
            int paused = (is_wps_fading() || (audio_status() & AUDIO_STATUS_PAUSE));
            long tracknum = get_u32(&buf[3]);
            
            audio_pause();
            audio_skip(tracknum - playlist_next(0));
            if (!paused)
                audio_resume();
            
            /* respond with cmd ok packet */
            cmd_ok_mode4(cmd);
            break;
        }
        
        default:
        {
            /* default response is with cmd ok packet */
            cmd_ok_mode4(cmd);
            break;
        }
    }
}

static void iap_handlepkt_mode7(unsigned int len, const unsigned char *buf)
{
    unsigned int cmd = buf[1];
    switch (cmd)
    {
        /* RetTunerCaps */
        case 0x02:
        {
            /* do nothing */
            
            /* GetAccessoryInfo */
            unsigned char data[] = {0x00, 0x27, 0x00};
            iap_send_pkt(data, sizeof(data));
            break;
        }
        
        /* RetTunerFreq */
        case 0x0A:
            /* fall through */
        /* TunerSeekDone */
        case 0x13:
        {
            rmt_tuner_freq(len, buf);
            break;
        }
        
        /* RdsReadyNotify, RDS station name 0x21 1E 00 + ASCII text*/
        case 0x21:
        {
            rmt_tuner_rds_data(len, buf);
            break;
        }
    }
}

void iap_handlepkt(void)
{
    struct state_t *s = &frame_state;

    if(!iap_setupflag) return;

    /* if we are waiting for a remote button to go out,
       delay the handling of the new packet */
    if(!iap_remotetick)
    {
        queue_post(&button_queue, SYS_IAP_HANDLEPKT, 0);
        return;
    }

    /* handle command by mode */
    unsigned char mode = s->payload[0];
    switch (mode) {
    case 0: iap_handlepkt_mode0(s->len, s->payload); break;
    case 2: iap_handlepkt_mode2(s->len, s->payload); break;
    case 3: iap_handlepkt_mode3(s->len, s->payload); break;
    case 4: iap_handlepkt_mode4(s->len, s->payload); break;
    case 7: iap_handlepkt_mode7(s->len, s->payload); break;
    }
}

int remote_control_rx(void)
{
    int btn = iap_remotebtn;
    if(iap_repeatbtn)
    {
        iap_repeatbtn--;
        if(!iap_repeatbtn)
        {
            iap_remotebtn = BUTTON_NONE;
            iap_remotetick = true;
        }
    }
    else
        iap_remotetick = true;

    return btn;
}

const unsigned char *iap_get_serbuf(void)
{
    return serbuf;
}

