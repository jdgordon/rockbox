/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2005-2007 Miika Pekkarinen
 * Copyright (C) 2007-2008 Nicolas Pennequin
 * Copyright (C) 2011      Michael Sevakis
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "panic.h"
#include "core_alloc.h"
#include "sound.h"
#include "ata.h"
#include "usb.h"
#include "codecs.h"
#include "codec_thread.h"
#include "voice_thread.h"
#include "metadata.h"
#include "cuesheet.h"
#include "buffering.h"
#include "talk.h"
#include "playlist.h"
#include "abrepeat.h"
#include "pcmbuf.h"
#include "playback.h"
#include "misc.h"
#include "settings.h"

#ifdef HAVE_TAGCACHE
#include "tagcache.h"
#endif

#ifdef AUDIO_HAVE_RECORDING
#include "pcm_record.h"
#endif

#ifdef HAVE_LCD_BITMAP
#ifdef HAVE_ALBUMART
#include "albumart.h"
#endif
#endif

/* TODO: The audio thread really is doing multitasking of acting like a
         consumer and producer of tracks. It may be advantageous to better
         logically separate the two functions. I won't go that far just yet. */

/* Internal support for voice playback */
#define PLAYBACK_VOICE

#if CONFIG_PLATFORM & PLATFORM_NATIVE
/* Application builds don't support direct code loading */
#define HAVE_CODEC_BUFFERING
#endif

/* Amount of guess-space to allow for codecs that must hunt and peck
 * for their correct seek target, 32k seems a good size */
#define AUDIO_REBUFFER_GUESS_SIZE    (1024*32)

/* Define LOGF_ENABLE to enable logf output in this file */
/* #define LOGF_ENABLE */
#include "logf.h"

/* Macros to enable logf for queues
   logging on SYS_TIMEOUT can be disabled */
#ifdef SIMULATOR
/* Define this for logf output of all queuing except SYS_TIMEOUT */
#define PLAYBACK_LOGQUEUES
/* Define this to logf SYS_TIMEOUT messages */
/*#define PLAYBACK_LOGQUEUES_SYS_TIMEOUT*/
#endif

#ifdef PLAYBACK_LOGQUEUES
#define LOGFQUEUE logf
#else
#define LOGFQUEUE(...)
#endif

#ifdef PLAYBACK_LOGQUEUES_SYS_TIMEOUT
#define LOGFQUEUE_SYS_TIMEOUT logf
#else
#define LOGFQUEUE_SYS_TIMEOUT(...)
#endif

/* Variables are commented with the threads that use them:
 * A=audio, C=codec, O=other. A suffix of "-" indicates that the variable is
 * read but not updated on that thread. Audio is the only user unless otherwise
 * specified.
 */

/** Miscellaneous **/
bool audio_is_initialized = false; /* (A,O-) */
extern struct codec_api ci;        /* (A,C) */

/** Possible arrangements of the main buffer **/
static enum audio_buffer_state
{
    AUDIOBUF_STATE_TRASHED     = -1,     /* trashed; must be reset */
    AUDIOBUF_STATE_INITIALIZED =  0,     /* voice+audio OR audio-only */
    AUDIOBUF_STATE_VOICED_ONLY =  1,     /* voice-only */
} buffer_state = AUDIOBUF_STATE_TRASHED; /* (A,O) */

/** Main state control **/
static bool ff_rw_mode SHAREDBSS_ATTR = false; /* Pre-ff-rewind mode (A,O-) */

enum play_status
{
    PLAY_STOPPED = 0,
    PLAY_PLAYING = AUDIO_STATUS_PLAY,
    PLAY_PAUSED  = AUDIO_STATUS_PLAY | AUDIO_STATUS_PAUSE,
} play_status = PLAY_STOPPED;

/* Sizeable things that only need exist during playback and not when stopped */
static struct audio_scratch_memory
{
    struct mp3entry codec_id3; /* (A,C) */
    struct mp3entry unbuffered_id3;
    struct cuesheet *curr_cue; /* Will follow this structure */
} * audio_scratch_memory = NULL;

/* These are used to store the current, next and optionally the peek-ahead
 * mp3entry's - this guarantees that the pointer returned by audio_current/
 * next_track will be valid for the full duration of the currently playing
 * track */
enum audio_id3_types
{
    /* These are allocated statically */
    PLAYING_ID3 = 0,
    NEXTTRACK_ID3,
#ifdef AUDIO_FAST_SKIP_PREVIEW
    /* The real playing metadata must has to be protected since it contains
       critical info for other features */
    PLAYING_PEEK_ID3,
#endif
    ID3_TYPE_NUM_STATIC,
    /* These go in the scratch memory */
    UNBUFFERED_ID3 = ID3_TYPE_NUM_STATIC,
    CODEC_ID3,
};
static struct mp3entry static_id3_entries[ID3_TYPE_NUM_STATIC]; /* (A,O) */

/* Peeking functions can yield and mess us up */
static struct mutex id3_mutex SHAREDBSS_ATTR; /* (A,O)*/


/** For Scrobbler support **/

/* Previous track elapsed time */
static unsigned long prev_track_elapsed = 0; /* (A,O-) */


/** For album art support **/
#define MAX_MULTIPLE_AA SKINNABLE_SCREENS_COUNT
#ifdef HAVE_ALBUMART

static struct albumart_slot
{
    struct dim dim;     /* Holds width, height of the albumart */
    int used;           /* Counter; increments if something uses it */
} albumart_slots[MAX_MULTIPLE_AA]; /* (A,O) */

#define FOREACH_ALBUMART(i) for(i = 0;i < MAX_MULTIPLE_AA; i++)
#endif /* HAVE_ALBUMART */


/** Information used for tracking buffer fills **/

/* Buffer and thread state tracking */
static enum filling_state
{
    STATE_IDLE = 0, /* audio is stopped: nothing to do */
    STATE_FILLING,  /* adding tracks to the buffer */
    STATE_FULL,     /* can't add any more tracks */
    STATE_END_OF_PLAYLIST, /* all remaining tracks have been added */
    STATE_FINISHED, /* all remaining tracks are fully buffered */
    STATE_ENDING,   /* audio playback is ending */
    STATE_ENDED,    /* audio playback is done */
#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
    STATE_USB,      /* USB mode, ignore most messages */
#endif
} filling = STATE_IDLE;

/* Track info - holds information about each track in the buffer */
struct track_info
{
    /* In per-track allocated order: */
    int id3_hid;                /* Metadata handle ID */
    int cuesheet_hid;           /* Parsed cuesheet handle ID */
#ifdef HAVE_ALBUMART
    int aa_hid[MAX_MULTIPLE_AA];/* Album art handle IDs */
#endif
#ifdef HAVE_CODEC_BUFFERING
    int codec_hid;              /* Buffered codec handle ID */
#endif
    int audio_hid;              /* Main audio data handle ID */
    size_t filesize;            /* File total length on disk
                                   TODO: This should be stored
                                         in the handle or the
                                         id3 and would use less
                                         ram */
};

/* Track list - holds info about all buffered tracks */
#if MEMORYSIZE >= 32
#define TRACK_LIST_LEN  128 /* Must be 2^int(+n) */
#elif MEMORYSIZE >= 16
#define TRACK_LIST_LEN   64
#elif MEMORYSIZE >= 8
#define TRACK_LIST_LEN   32
#else
#define TRACK_LIST_LEN   16
#endif

#define TRACK_LIST_MASK (TRACK_LIST_LEN-1)

static struct
{
    /* read, write and current are maintained unwrapped, limited only by the
       unsigned int range and wrap-safe comparisons are used */

    /* NOTE: there appears to be a bug in arm-elf-eabi-gcc 4.4.4 for ARMv4 where
       if 'end' follows 'start' in this structure, track_list_count performs
       'start - end' rather than 'end - start', giving negative count values...
       so leave it this way for now! */
    unsigned int end;           /* Next open position */
    unsigned int start;         /* First track in list */
    unsigned int current;       /* Currently decoding track */
    struct track_info tracks[TRACK_LIST_LEN]; /* Buffered track information */
} track_list; /* (A, O-) */


/* Playlist steps from playlist position to next track to be buffered */
static int playlist_peek_offset = 0;

/* Metadata handle of track load in progress (meaning all handles have not
   yet been opened for the track, id3 always exists or the track does not)

   Tracks are keyed by their metadata handles if track list pointers are
   insufficient to make comparisons */
static int in_progress_id3_hid = ERR_HANDLE_NOT_FOUND;

#ifdef HAVE_DISK_STORAGE
/* Buffer margin A.K.A. anti-skip buffer (in seconds) */
static size_t buffer_margin = 5;
#endif

/* Values returned for track loading */
enum track_load_status
{
    LOAD_TRACK_ERR_START_CODEC   = -6,
    LOAD_TRACK_ERR_FINISH_FAILED = -5,
    LOAD_TRACK_ERR_FINISH_FULL   = -4,
    LOAD_TRACK_ERR_BUSY          = -3,
    LOAD_TRACK_ERR_NO_MORE       = -2,
    LOAD_TRACK_ERR_FAILED        = -1,
    LOAD_TRACK_OK                =  0,
    LOAD_TRACK_READY             =  1,
};

/** Track change controls **/

/* What sort of skip is pending globally? */
enum track_skip_type
{
    /* Relative to what user is intended to see: */
    /* Codec: +0, Track List: +0, Playlist: +0 */
    TRACK_SKIP_NONE = 0,          /* no track skip */
    /* Codec: +1, Track List: +1, Playlist: +0 */
    TRACK_SKIP_AUTO,              /* codec-initiated skip */
    /* Codec: +1, Track List: +1, Playlist: +1 */
    TRACK_SKIP_AUTO_NEW_PLAYLIST, /* codec-initiated skip is new playlist */
    /* Codec: xx, Track List: +0, Playlist: +0 */
    TRACK_SKIP_AUTO_END_PLAYLIST, /* codec-initiated end of playlist */
    /* Manual skip: Never pends */
    TRACK_SKIP_MANUAL,            /* manual track skip */
    /* Manual skip: Never pends */
    TRACK_SKIP_DIR_CHANGE,        /* manual directory skip */
} skip_pending = TRACK_SKIP_NONE;

/* Note about TRACK_SKIP_AUTO_NEW_PLAYLIST:
   Fixing playlist code to be able to peek into the first song of
   the next playlist would fix any issues and this wouldn't need
   to be a special case since pre-advancing the playlist would be
   unneeded - it could be much more like TRACK_SKIP_AUTO and all
   actions that require reversal during an in-progress transition
   would work as expected */

/* Used to indicate status for the events. Must be separate to satisfy all
   clients so the correct metadata is read when sending the change events
   and also so that it is read correctly outside the events. */
static bool automatic_skip = false; /* (A, O-) */

/* Pending manual track skip offset */
static int skip_offset = 0; /* (A, O) */

/* Track change notification */
static struct
{
    unsigned int in;  /* Number of pcmbuf posts (audio isr) */
    unsigned int out; /* Number of times audio has read the difference */
} track_change = { 0, 0 };

/** Codec status **/
/* Did the codec notify us it finished while we were paused or while still
   in an automatic transition?

   If paused, it is necessary to defer a codec-initiated skip until resuming
   or else the track will move forward while not playing audio!

   If in-progress, skips should not build-up ahead of where the WPS is when
   really short tracks finish decoding.

   If it is forgotten, it will be missed altogether and playback will just sit
   there looking stupid and comatose until the user does something */
static bool codec_skip_pending = false;
static int  codec_skip_status;
static bool codec_seeking = false;          /* Codec seeking ack expected? */
static unsigned int position_key = 0;

/* Event queues */
static struct event_queue audio_queue SHAREDBSS_ATTR;

/* Audio thread */
static struct queue_sender_list audio_queue_sender_list SHAREDBSS_ATTR;
static long audio_stack[(DEFAULT_STACK_SIZE + 0x1000)/sizeof(long)];
static const char audio_thread_name[] = "audio";
static unsigned int audio_thread_id = 0;

/* Forward declarations */
enum audio_start_playback_flags
{
    AUDIO_START_RESTART = 0x1, /* "Restart" playback (flush _all_ tracks) */
    AUDIO_START_NEWBUF  = 0x2, /* Mark the audiobuffer as invalid */
};

static void audio_start_playback(size_t offset, unsigned int flags);
static void audio_stop_playback(void);
static void buffer_event_buffer_low_callback(void *data);
static void buffer_event_rebuffer_callback(void *data);
static void buffer_event_finished_callback(void *data);
void audio_pcmbuf_sync_position(void);


/**************************************/

/** --- audio_queue helpers --- **/
static void audio_queue_post(long id, intptr_t data)
{
    queue_post(&audio_queue, id, data);
}

static intptr_t audio_queue_send(long id, intptr_t data)
{
    return queue_send(&audio_queue, id, data);
}


/** --- MP3Entry --- **/

/* Does the mp3entry have enough info for us to use it? */
static struct mp3entry * valid_mp3entry(const struct mp3entry *id3)
{
    return id3 && (id3->length != 0 || id3->filesize != 0) &&
           id3->codectype != AFMT_UNKNOWN ? (struct mp3entry *)id3 : NULL;
}

/* Return a pointer to an mp3entry on the buffer, as it is */
static struct mp3entry * bufgetid3(int handle_id)
{
    if (handle_id < 0)
        return NULL;

    struct mp3entry *id3;
    ssize_t ret = bufgetdata(handle_id, 0, (void *)&id3);

    if (ret != sizeof(struct mp3entry))
        return NULL;

    return id3;
}

/* Read an mp3entry from the buffer, adjusted */
static bool bufreadid3(int handle_id, struct mp3entry *id3out)
{
    struct mp3entry *id3 = bufgetid3(handle_id);

    if (id3)
    {
        copy_mp3entry(id3out, id3);
        return true;
    }

    return false;
}

/* Lock the id3 mutex */
static void id3_mutex_lock(void)
{
    mutex_lock(&id3_mutex);
}

/* Unlock the id3 mutex */
static void id3_mutex_unlock(void)
{
    mutex_unlock(&id3_mutex);
}

/* Return one of the collection of mp3entry pointers - collect them all here */
static inline struct mp3entry * id3_get(enum audio_id3_types id3_num)
{
    switch (id3_num)
    {
    case UNBUFFERED_ID3:
        return &audio_scratch_memory->unbuffered_id3;
    case CODEC_ID3:
        return &audio_scratch_memory->codec_id3;
    default:
        return &static_id3_entries[id3_num];
    }
}

/* Copy an mp3entry into one of the mp3 entries */
static void id3_write(enum audio_id3_types id3_num,
                      const struct mp3entry *id3_src)
{
    struct mp3entry *dest_id3 = id3_get(id3_num);

    if (id3_src)
        copy_mp3entry(dest_id3, id3_src);
    else
        wipe_mp3entry(dest_id3);
}

/* Call id3_write "safely" because peek aheads can yield, even if the fast
   preview isn't enabled */
static void id3_write_locked(enum audio_id3_types id3_num,
                             const struct mp3entry *id3_src)
{
    id3_mutex_lock();
    id3_write(id3_num, id3_src);
    id3_mutex_unlock();
}


/** --- Track info --- **/

/* Close a handle and mark it invalid */
static void track_info_close_handle(int *hid_p)
{
    int hid = *hid_p;

    /* bufclose returns true if the handle is not found, or if it is closed
     * successfully, so these checks are safe on non-existant handles */
    if (hid >= 0)
        bufclose(hid);

    /* Always reset to "no handle" in case it was something else */
    *hid_p = ERR_HANDLE_NOT_FOUND;
}

/* Close all handles in a struct track_info and clear it */
static void track_info_close(struct track_info *info)
{
    /* Close them in the order they are allocated on the buffer to speed up
       the handle searching */
    track_info_close_handle(&info->id3_hid);
    track_info_close_handle(&info->cuesheet_hid);
#ifdef HAVE_ALBUMART
    int i;
    FOREACH_ALBUMART(i)
        track_info_close_handle(&info->aa_hid[i]);
#endif
#ifdef HAVE_CODEC_BUFFERING
    track_info_close_handle(&info->codec_hid);
#endif
    track_info_close_handle(&info->audio_hid);
    info->filesize = 0;
}

/* Invalidate all members to initial values - does not close handles */
static void track_info_wipe(struct track_info * info)
{
    info->id3_hid = ERR_HANDLE_NOT_FOUND;
    info->cuesheet_hid = ERR_HANDLE_NOT_FOUND;
#ifdef HAVE_ALBUMART
    int i;
    FOREACH_ALBUMART(i)
        info->aa_hid[i] = ERR_HANDLE_NOT_FOUND;
#endif
#ifdef HAVE_CODEC_BUFFERING
    info->codec_hid = ERR_HANDLE_NOT_FOUND;
#endif
    info->audio_hid = ERR_HANDLE_NOT_FOUND;
    info->filesize = 0;
}


/** --- Track list --- **/

/* Initialize the track list */
static void track_list_init(void)
{
    int i;
    for (i = 0; i < TRACK_LIST_LEN; i++)
        track_info_wipe(&track_list.tracks[i]);

    track_list.start = track_list.end = track_list.current;
}

/* Return number of items allocated in the list */
static unsigned int track_list_count(void)
{
    return track_list.end - track_list.start;
}

/* Return true if the list is empty */
static inline bool track_list_empty(void)
{
    return track_list.end == track_list.start;
}

/* Returns true if the list is holding the maximum number of items */
static bool track_list_full(void)
{
    return track_list.end - track_list.start >= TRACK_LIST_LEN;
}

/* Test if the index is within the allocated range */
static bool track_list_in_range(int pos)
{
    return (int)(pos - track_list.start) >= 0 &&
           (int)(pos - track_list.end) < 0;
}

static struct track_info * track_list_entry(int pos)
{
    return &track_list.tracks[pos & TRACK_LIST_MASK];
}

/* Return the info of the last allocation plus an offset, NULL if result is
   out of bounds */
static struct track_info * track_list_last(int offset)
{
    /* Last is before the end since the end isn't inclusive */
    unsigned int pos = track_list.end + offset - 1;

    if (!track_list_in_range(pos))
        return NULL;

    return track_list_entry(pos);
}

/* Allocate space at the end for another track if not full */
static struct track_info * track_list_alloc_track(void)
{
    if (track_list_full())
        return NULL;

    return track_list_entry(track_list.end++);
}

/* Remove the last track entry allocated in order to support backing out
   of a track load */
static void track_list_unalloc_track(void)
{
    if (track_list_empty())
        return;

    track_list.end--;

    if (track_list.current == track_list.end &&
        track_list.current != track_list.start)
    {
        /* Current _must_ remain within bounds */
        track_list.current--;
    }
}

/* Return current track plus an offset, NULL if result is out of bounds */
static struct track_info * track_list_current(int offset)
{
    unsigned int pos = track_list.current + offset;

    if (!track_list_in_range(pos))
        return NULL;

    return track_list_entry(pos);
}

/* Return current based upon what's intended that the user sees - not
   necessarily where decoding is taking place */
static struct track_info * track_list_user_current(int offset)
{
    if (skip_pending == TRACK_SKIP_AUTO ||
        skip_pending == TRACK_SKIP_AUTO_NEW_PLAYLIST)
    {
        offset--;
    }

    return track_list_current(offset);
}

/* Advance current track by an offset, return false if result is out of
   bounds */
static struct track_info * track_list_advance_current(int offset)
{
    unsigned int pos = track_list.current + offset;

    if (!track_list_in_range(pos))
        return NULL;

    track_list.current = pos;
    return track_list_entry(pos);
}

/* Clear tracks in the list, optionally preserving the current track -
   returns 'false' if the operation was changed */
enum track_clear_action
{
    TRACK_LIST_CLEAR_ALL = 0, /* Clear all tracks */
    TRACK_LIST_KEEP_CURRENT,  /* Keep current only; clear before + after */
    TRACK_LIST_KEEP_NEW       /* Keep current and those that follow */
};

static void track_list_clear(enum track_clear_action action)
{
    logf("%s(%d)", __func__, (int)action);

    /* Don't care now since rebuffering is imminent */
    buf_set_watermark(0);

    if (action != TRACK_LIST_CLEAR_ALL)
    {
        struct track_info *cur = track_list_current(0);

        if (!cur || cur->id3_hid < 0)
            action = TRACK_LIST_CLEAR_ALL; /* Nothing worthwhile keeping */
    }

    /* Noone should see this progressing */
    int start = track_list.start;
    int current = track_list.current;
    int end = track_list.end;

    track_list.start = current;

    switch (action)
    {
    case TRACK_LIST_CLEAR_ALL:
        /* Result: .start = .current, .end = .current */
        track_list.end = current;
        break;

    case TRACK_LIST_KEEP_CURRENT:
        /* Result: .start = .current, .end = .current + 1 */
        track_list.end = current + 1;
        break;

    case TRACK_LIST_KEEP_NEW:
        /* Result: .start = .current, .end = .end */
        end = current;
        break;
    }

    /* Close all open handles in the range except the for the current track
       if preserving that */
    while (start != end)
    {
        if (action != TRACK_LIST_KEEP_CURRENT || start != current)
        {
            struct track_info *info =
                &track_list.tracks[start & TRACK_LIST_MASK];

            /* If this is the in-progress load, abort it */
            if (in_progress_id3_hid >= 0 &&
                info->id3_hid == in_progress_id3_hid)
            {
                in_progress_id3_hid = ERR_HANDLE_NOT_FOUND;
            }

            track_info_close(info);
        }

        start++;
    }
}


/** --- Audio buffer -- **/

/* What size is needed for the scratch buffer? */
static size_t scratch_mem_size(void)
{
    size_t size = sizeof (struct audio_scratch_memory);

    if (global_settings.cuesheet)
        size += sizeof (struct cuesheet);

    return size;
}

/* Initialize the memory area where data is stored that is only used when
   playing audio and anything depending upon it */
static void scratch_mem_init(void *mem)
{
    audio_scratch_memory = (struct audio_scratch_memory *)mem;
    id3_write_locked(UNBUFFERED_ID3, NULL);
    id3_write(CODEC_ID3, NULL);
    ci.id3 = id3_get(CODEC_ID3);
    audio_scratch_memory->curr_cue = NULL;

    if (global_settings.cuesheet)
    {
        audio_scratch_memory->curr_cue =
            SKIPBYTES((struct cuesheet *)audio_scratch_memory,
                      sizeof (struct audio_scratch_memory));
    }
}

static int audiobuf_handle;
static size_t filebuflen;

size_t audio_buffer_available(void)
{
    if (audiobuf_handle > 0) /* if allocated return what we got */
        return filebuflen;
    return core_available();
}

/* Set up the audio buffer for playback
 * filebuflen must be pre-initialized with the maximum size */
static void audio_reset_buffer_noalloc(void* filebuf)
{
    /*
     * Layout audio buffer as follows:
     * [[|TALK]|SCRATCH|BUFFERING|PCM|[VOICE|]]
     */

    /* see audio_get_recording_buffer if this is modified */
    logf("%s()", __func__);

    /* If the setup of anything allocated before the file buffer is
       changed, do check the adjustments after the buffer_alloc call
       as it will likely be affected and need sliding over */

    /* Initially set up file buffer as all space available */
    size_t allocsize;

    /* Subtract whatever voice needs */
    allocsize = talkbuf_init(filebuf);
    allocsize = ALIGN_UP(allocsize, sizeof (intptr_t));
    if (allocsize > filebuflen)
        goto bufpanic;

    filebuf += allocsize;
    filebuflen -= allocsize;

    if (talk_voice_required())
    {
        /* Need a space for voice PCM output */
        allocsize = voicebuf_init(filebuf + filebuflen);

        allocsize = ALIGN_UP(allocsize, sizeof (intptr_t));
        if (allocsize > filebuflen)
            goto bufpanic;

        filebuflen -= allocsize;
    }

    /* Subtract whatever the pcm buffer says it used plus the guard buffer */
    allocsize = pcmbuf_init(filebuf + filebuflen);

    /* Make sure filebuflen is a pointer sized multiple after adjustment */
    allocsize = ALIGN_UP(allocsize, sizeof (intptr_t));
    if (allocsize > filebuflen)
        goto bufpanic;

    filebuflen -= allocsize;

    /* Scratch memory */
    allocsize = scratch_mem_size();
    if (allocsize > filebuflen)
        goto bufpanic;

    scratch_mem_init(filebuf);
    filebuf += allocsize;
    filebuflen -= allocsize;

    buffering_reset(filebuf, filebuflen);

    /* Clear any references to the file buffer */
    buffer_state = AUDIOBUF_STATE_INITIALIZED;

#if defined(ROCKBOX_HAS_LOGF) && defined(LOGF_ENABLE)
    /* Make sure everything adds up - yes, some info is a bit redundant but
       aids viewing and the summation of certain variables should add up to
       the location of others. */
    {
        logf("fbuf:   %08X", (unsigned)filebuf);
        logf("fbufe:  %08X", (unsigned)(filebuf + filebuflen));
        logf("sbuf:   %08X", (unsigned)audio_scratch_memory);
        logf("sbufe:  %08X", (unsigned)(audio_scratch_memory + allocsize));
    }
#endif

    return;

bufpanic:
    panicf("%s(): EOM (%zu > %zu)", __func__, allocsize, filebuflen);
}


/* Buffer must not move. */
static int shrink_callback(int handle, unsigned hints, void* start, size_t old_size)
{
    long offset = audio_current_track()->offset;
    int status = audio_status();
    /* TODO: Do it without stopping playback, if possible */
    /* don't call audio_hard_stop() as it frees this handle */
    if (thread_self() == audio_thread_id)
    {   /* inline case Q_AUDIO_STOP (audio_hard_stop() response
         * if we're in the audio thread */
        audio_stop_playback();
        queue_clear(&audio_queue);
    }
    else
        audio_queue_send(Q_AUDIO_STOP, 1);
#ifdef PLAYBACK_VOICE
    voice_stop();
#endif
    /* we should be free to change the buffer now */
    size_t wanted_size = (hints & BUFLIB_SHRINK_SIZE_MASK);
    ssize_t size = (ssize_t)old_size - wanted_size;
    /* set final buffer size before calling audio_reset_buffer_noalloc() */
    filebuflen = size;
    switch (hints & BUFLIB_SHRINK_POS_MASK)
    {
        case BUFLIB_SHRINK_POS_BACK:
            core_shrink(handle, start, size);
            audio_reset_buffer_noalloc(start);
            break;
        case BUFLIB_SHRINK_POS_FRONT:
            core_shrink(handle, start + wanted_size, size);
            audio_reset_buffer_noalloc(start + wanted_size);
            break;
    }
    if ((status & AUDIO_STATUS_PLAY) == AUDIO_STATUS_PLAY)
    {
        if (thread_self() == audio_thread_id)
            audio_start_playback(offset, 0);  /* inline Q_AUDIO_PLAY */
        else
            audio_play(offset);
    }

    return BUFLIB_CB_OK;
}

static struct buflib_callbacks ops = {
    .move_callback = NULL,
    .shrink_callback = shrink_callback,
};

static void audio_reset_buffer(void)
{
    if (audiobuf_handle > 0)
    {
        core_free(audiobuf_handle);
        audiobuf_handle = 0;
    }
    audiobuf_handle = core_alloc_maximum("audiobuf", &filebuflen, &ops);
    unsigned char *filebuf = core_get_data(audiobuf_handle);

    audio_reset_buffer_noalloc(filebuf);
}

/* Set the buffer margin to begin rebuffering when 'seconds' from empty */
static void audio_update_filebuf_watermark(int seconds)
{
    size_t bytes = 0;

#ifdef HAVE_DISK_STORAGE
    int spinup = ata_spinup_time();

    if (seconds == 0)
    {
        /* By current setting */
        seconds = buffer_margin;
    }
    else
    {
        /* New setting */
        buffer_margin = seconds;

        if (buf_get_watermark() == 0)
        {
            /* Write a watermark only if the audio thread already did so for
               itself or it will fail to set the event and the watermark - if
               it hasn't yet, it will use the new setting when it does */
            return;
        }
    }

    if (spinup)
        seconds += (spinup / HZ) + 1;
    else
        seconds += 5;

    seconds += buffer_margin;
#else
    /* flash storage */
    seconds = 1;
#endif

    /* Watermark is a function of the bitrate of the last track in the buffer */
    struct mp3entry *id3 = NULL;
    struct track_info *info = track_list_last(0);

    if (info)
        id3 = valid_mp3entry(bufgetid3(info->id3_hid));

    if (id3)
    {
        if (get_audio_base_data_type(id3->codectype) == TYPE_PACKET_AUDIO)
        {
            bytes = id3->bitrate * (1000/8) * seconds;
        }
        else
        {
            /* Bitrate has no meaning to buffering margin for atomic audio -
               rebuffer when it's the only track left unless it's the only
               track that fits, in which case we should avoid constant buffer
               low events */
            if (track_list_count() > 1)
                bytes = info->filesize + 1;
        }
    }
    else
    {
        /* Then set the minimum - this should not occur anyway */
        logf("fwmark: No id3 for last track (s%u/c%u/e%u)",
             track_list.start, track_list.current, track_list.end);
    }

    /* Actually setting zero disables the notification and we use that
       to detect that it has been reset */
    buf_set_watermark(MAX(bytes, 1));
    logf("fwmark: %lu", (unsigned long)bytes);
}


/** -- Track change notification -- **/

/* Check the pcmbuf track changes and return write the message into the event
   if there are any */
static inline bool audio_pcmbuf_track_change_scan(void)
{
    if (track_change.out != track_change.in)
    {
        track_change.out++;
        return true;
    }

    return false;
}

/* Clear outstanding track change posts */
static inline void audio_pcmbuf_track_change_clear(void)
{
    track_change.out = track_change.in;
}

/* Post a track change notification - called by audio ISR */
static inline void audio_pcmbuf_track_change_post(void)
{
    track_change.in++;
}


/** --- Helper functions --- **/

/* Removes messages that might end up in the queue before or while processing
   a manual track change. Responding to them would be harmful since they
   belong to a previous track's playback period. Anything that would generate
   the stale messages must first be put into a state where it will not do so.
 */
static void audio_clear_track_notifications(void)
{
    static const long filter_list[][2] =
    {
        /* codec messages */
        { Q_AUDIO_CODEC_SEEK_COMPLETE, Q_AUDIO_CODEC_COMPLETE },
        /* track change messages */
        { Q_AUDIO_TRACK_CHANGED, Q_AUDIO_TRACK_CHANGED },
    };

    const int filter_count = ARRAYLEN(filter_list) - 1;

    /* Remove any pcmbuf notifications */
    pcmbuf_monitor_track_change(false);
    audio_pcmbuf_track_change_clear();

    /* Scrub the audio queue of the old mold */
    while (queue_peek_ex(&audio_queue, NULL,
                         filter_count | QPEEK_REMOVE_EVENTS,
                         filter_list))
    {
        yield(); /* Not strictly needed, per se, ad infinitum, ra, ra */
    }
}

/* Takes actions based upon track load status codes */
static void audio_handle_track_load_status(int trackstat)
{
    switch (trackstat)
    {
    case LOAD_TRACK_ERR_NO_MORE:
        if (track_list_count() > 0)
            break;

    case LOAD_TRACK_ERR_START_CODEC:
        audio_queue_post(Q_AUDIO_CODEC_COMPLETE, CODEC_ERROR);
        break;

    default:
        break;
    }
}

/* Announce the end of playing the current track */
static void audio_playlist_track_finish(void)
{
    struct mp3entry *ply_id3 = id3_get(PLAYING_ID3);
    struct mp3entry *id3 = valid_mp3entry(ply_id3);

    playlist_update_resume_info(filling == STATE_ENDED ? NULL : id3);

    if (id3)
    {
        send_event(PLAYBACK_EVENT_TRACK_FINISH, id3);
        prev_track_elapsed = id3->elapsed;
    }
    else
    {
        prev_track_elapsed = 0;
    }
}

/* Announce the beginning of the new track */
static void audio_playlist_track_change(void)
{
    struct mp3entry *id3 = valid_mp3entry(id3_get(PLAYING_ID3));

    if (id3)
        send_event(PLAYBACK_EVENT_TRACK_CHANGE, id3);

    position_key = pcmbuf_get_position_key();

    playlist_update_resume_info(id3);
}

/* Change the data for the next track and send the event */
static void audio_update_and_announce_next_track(const struct mp3entry *id3_next)
{
    id3_write_locked(NEXTTRACK_ID3, id3_next);
    send_event(PLAYBACK_EVENT_NEXTTRACKID3_AVAILABLE,
               id3_get(NEXTTRACK_ID3));
}

/* Bring the user current mp3entry up to date and set a new offset for the
   buffered metadata */
static void playing_id3_sync(struct track_info *user_info, off_t offset)
{
    id3_mutex_lock();

    struct mp3entry *id3 = bufgetid3(user_info->id3_hid);
    struct mp3entry *playing_id3 = id3_get(PLAYING_ID3);

    pcm_play_lock();

    unsigned long e = playing_id3->elapsed;
    unsigned long o = playing_id3->offset;

    id3_write(PLAYING_ID3, id3);

    if (offset < 0)
    {
        playing_id3->elapsed = e;
        playing_id3->offset = o;
        offset = 0;
    }

    pcm_play_unlock();

    if (id3)
        id3->offset = offset;

    id3_mutex_unlock();
}

/* Wipe-out track metadata - current is optional */
static void wipe_track_metadata(bool current)
{
    id3_mutex_lock();

    if (current)
        id3_write(PLAYING_ID3, NULL);

    id3_write(NEXTTRACK_ID3, NULL);
    id3_write(UNBUFFERED_ID3, NULL);

    id3_mutex_unlock();
}

/* Called when buffering is completed on the last track handle */
static void filling_is_finished(void)
{
    logf("last track finished buffering");

    /* There's no more to load or watch for */
    buf_set_watermark(0);
    filling = STATE_FINISHED;
}

/* Stop the codec decoding or waiting for its data to be ready - returns
   'false' if the codec ended up stopped */
static bool halt_decoding_track(bool stop)
{
    /* If it was waiting for us to clear the buffer to make a rebuffer
       happen, it should cease otherwise codec_stop could deadlock waiting
       for the codec to go to its main loop - codec's request will now
       force-fail */
    bool retval = false;

    buf_signal_handle(ci.audio_hid, true);

    if (stop)
        codec_stop();
    else
        retval = codec_pause();

    audio_clear_track_notifications();

    /* We now know it's idle and not waiting for buffered data */
    buf_signal_handle(ci.audio_hid, false);

    codec_skip_pending = false;
    codec_seeking = false;

    return retval;
}

/* Wait for any in-progress fade to complete */
static void audio_wait_fade_complete(void)
{
    /* Just loop until it's done */
    while (pcmbuf_fading())
        sleep(0);
}

/* End the ff/rw mode */
static void audio_ff_rewind_end(void)
{
    /* A seamless seek (not calling audio_pre_ff_rewind) skips this
       section */
    if (ff_rw_mode)
    {
        ff_rw_mode = false;

        if (codec_seeking)
        {
            /* Clear the buffer */
            pcmbuf_play_stop();
            audio_pcmbuf_sync_position();
        }

        if (play_status != PLAY_PAUSED)
        {
            /* Seeking-while-playing, resume PCM playback */
            pcmbuf_pause(false);
        }
    }
}

/* Complete the codec seek */
static void audio_complete_codec_seek(void)
{
    /* If a seek completed while paused, 'paused' is true.
     * If seeking from seek mode, 'ff_rw_mode' is true. */
    if (codec_seeking)
    {
        audio_ff_rewind_end();
        codec_seeking = false; /* set _after_ the call! */
    }
    /* else it's waiting and we must repond */
}

/* Get the current cuesheet pointer */
static inline struct cuesheet * get_current_cuesheet(void)
{
    return audio_scratch_memory->curr_cue;
}

/* Read the cuesheet from the buffer */
static void buf_read_cuesheet(int handle_id)
{
    struct cuesheet *cue = get_current_cuesheet();

    if (!cue || handle_id < 0)
        return;

    bufread(handle_id, sizeof (struct cuesheet), cue);
}

/* Backend to peek/current/next track metadata interface functions -
   fill in the mp3entry with as much information as we may obtain about
   the track at the specified offset from the user current track -
   returns false if no information exists with us */
static bool audio_get_track_metadata(int offset, struct mp3entry *id3)
{
    if (play_status == PLAY_STOPPED)
        return false;

    if (id3->path[0] != '\0')
        return true; /* Already filled */

    struct track_info *info = track_list_user_current(offset);

    if (!info)
    {
        struct mp3entry *ub_id3 = id3_get(UNBUFFERED_ID3);

        if (offset > 0 && track_list_user_current(offset - 1))
        {
            /* Try the unbuffered id3 since we're moving forward */
            if (ub_id3->path[0] != '\0')
            {
                copy_mp3entry(id3, ub_id3);
                return true;
            }
        }
    }
    else if (bufreadid3(info->id3_hid, id3))
    {
        id3->cuesheet = NULL;
        return true;
    }

    /* We didn't find the ID3 metadata, so we fill it with the little info we
       have and return that */

    char path[MAX_PATH+1];
    if (playlist_peek(offset, path, sizeof (path)))
    {
#if defined(HAVE_TC_RAMCACHE) && defined(HAVE_DIRCACHE)
        /* Try to get it from the database */
        if (!tagcache_fill_tags(id3, path))
#endif
        {
            /* By now, filename is the only source of info */
            fill_metadata_from_path(id3, path);
        }

        return true;
    }

    wipe_mp3entry(id3);

    return false;
}

/* Get a resume rewind adjusted offset from the ID3 */
unsigned long resume_rewind_adjusted_offset(const struct mp3entry *id3)
{
    unsigned long offset = id3->offset;
    size_t resume_rewind = global_settings.resume_rewind *
                           id3->bitrate * (1000/8);

    if (offset < resume_rewind)
        offset = 0;
    else
        offset -= resume_rewind;

    return offset;
}

/* Get the codec into ram and initialize it - keep it if it's ready */
static bool audio_init_codec(struct track_info *track_info,
                             struct mp3entry *track_id3)
{
    int codt_loaded = get_audio_base_codec_type(codec_loaded());
    int hid = ERR_HANDLE_NOT_FOUND;

    if (codt_loaded != AFMT_UNKNOWN)
    {
        int codt = get_audio_base_codec_type(track_id3->codectype);

        if (codt == codt_loaded)
        {
            /* Codec is the same base type */
            logf("Reusing prev. codec: %d", track_id3->codectype);
#ifdef HAVE_CODEC_BUFFERING
            /* Close any buffered codec (we could have skipped directly to a
               format transistion that is the same format as the current track
               and the buffered one is no longer needed) */
            track_info_close_handle(&track_info->codec_hid);
#endif
            return true;
        }
        else
        {
            /* New codec - first make sure the old one's gone */
            logf("Removing prev. codec: %d", codt_loaded);
            codec_unload();
        }
    }

    logf("New codec: %d/%d", track_id3->codectype, codec_loaded());

#ifdef HAVE_CODEC_BUFFERING
    /* Codec thread will close the handle even if it fails and will load from
       storage if hid is not valid or the buffer load fails */
    hid = track_info->codec_hid;
    track_info->codec_hid = ERR_HANDLE_NOT_FOUND;
#endif

    return codec_load(hid, track_id3->codectype);
    (void)track_info; /* When codec buffering isn't supported */
}

/* Start the codec for the current track scheduled to be decoded */
static bool audio_start_codec(bool auto_skip)
{
    struct track_info *info = track_list_current(0);
    struct mp3entry *cur_id3 = valid_mp3entry(bufgetid3(info->id3_hid));

    if (!cur_id3)
        return false;

    buf_pin_handle(info->id3_hid, true);

    if (!audio_init_codec(info, cur_id3))
    {
        buf_pin_handle(info->id3_hid, false);
        return false;
    }

#ifdef HAVE_TAGCACHE
    bool autoresume_enable = global_settings.autoresume_enable;

    if (autoresume_enable && !cur_id3->offset)
    {
        /* Resume all manually selected tracks */
        bool resume = !auto_skip;

        /* Send the "buffer" event to obtain the resume position for the codec */
        send_event(PLAYBACK_EVENT_TRACK_BUFFER, cur_id3);

        if (!resume)
        {
            /* Automatic skip - do further tests to see if we should just
               ignore any autoresume position */
            int autoresume_automatic = global_settings.autoresume_automatic;

            switch (autoresume_automatic)
            {
            case AUTORESUME_NEXTTRACK_ALWAYS:
                /* Just resume unconditionally */
                resume = true;
                break;
            case AUTORESUME_NEXTTRACK_NEVER:
                /* Force-rewind it */
                break;
            default:
                /* Not "never resume" - pass resume filter? */
                resume = autoresumable(cur_id3);
            }
        }

        if (!resume)
            cur_id3->offset = 0;

        logf("%s: Set offset for %s to %lX\n", __func__,
             cur_id3->title, cur_id3->offset);
    }
#endif /* HAVE_TAGCACHE */

    /* Rewind the required amount - if an autoresume was done, this also rewinds
       that by the setting's amount

       It would be best to have bookkeeping about whether or not the track
       sounded or not since skipping to it or else skipping to it while paused
       and back again will cause accumulation of silent rewinds - that's not
       our job to track directly nor could it be in any reasonable way
     */
    cur_id3->offset = resume_rewind_adjusted_offset(cur_id3);

    /* Update the codec API with the metadata and track info */
    id3_write(CODEC_ID3, cur_id3);

    ci.audio_hid = info->audio_hid;
    ci.filesize = info->filesize;
    buf_set_base_handle(info->audio_hid);

    /* All required data is now available for the codec */
    codec_go();

#ifdef HAVE_TAGCACHE
    if (!autoresume_enable || cur_id3->offset)
#endif
    {
        /* Send the "buffer" event now */
        send_event(PLAYBACK_EVENT_TRACK_BUFFER, cur_id3);
    }

    buf_pin_handle(info->id3_hid, false);
    return true;

    (void)auto_skip; /* ifndef HAVE_TAGCACHE */
}


/** --- Audio thread --- **/

/* Load and parse a cuesheet for the file - returns false if the buffer
   is full */
static bool audio_load_cuesheet(struct track_info *info,
                                struct mp3entry *track_id3)
{
    struct cuesheet *cue = get_current_cuesheet();
    track_id3->cuesheet = NULL;

    if (cue && info->cuesheet_hid == ERR_HANDLE_NOT_FOUND)
    {
        /* If error other than a full buffer, then mark it "unsupported" to
           avoid reloading attempt */
        int hid = ERR_UNSUPPORTED_TYPE;
        char cuepath[MAX_PATH];

#ifdef HAVE_IO_PRIORITY
        buf_back_off_storage(true);
#endif
        if (look_for_cuesheet_file(track_id3->path, cuepath))
        {
            hid = bufalloc(NULL, sizeof (struct cuesheet), TYPE_CUESHEET);

            if (hid >= 0)
            {
                void *cuesheet = NULL;
                bufgetdata(hid, sizeof (struct cuesheet), &cuesheet);

                if (parse_cuesheet(cuepath, (struct cuesheet *)cuesheet))
                {
                    /* Indicate cuesheet is present (while track remains
                       buffered) */
                    track_id3->cuesheet = cue;
                }
                else
                {
                    bufclose(hid);
                    hid = ERR_UNSUPPORTED_TYPE;
                }
            }
        }

#ifdef HAVE_IO_PRIORITY
        buf_back_off_storage(false);
#endif
        if (hid == ERR_BUFFER_FULL)
        {
            logf("buffer is full for now (%s)", __func__);
            return false;
        }
        else
        {
            if (hid < 0)
                logf("Cuesheet loading failed");

            info->cuesheet_hid = hid;
        }
    }

    return true;
}

#ifdef HAVE_ALBUMART
/* Load any album art for the file - returns false if the buffer is full */
static bool audio_load_albumart(struct track_info *info,
                                struct mp3entry *track_id3)
{
    int i;
    FOREACH_ALBUMART(i)
    {
        struct bufopen_bitmap_data user_data;
        int *aa_hid = &info->aa_hid[i];
        int hid = ERR_UNSUPPORTED_TYPE;

        /* albumart_slots may change during a yield of bufopen,
         * but that's no problem */
        if (*aa_hid >= 0 || *aa_hid == ERR_UNSUPPORTED_TYPE ||
            !albumart_slots[i].used)
            continue;

        memset(&user_data, 0, sizeof(user_data));
        user_data.dim = &albumart_slots[i].dim;

#ifdef HAVE_IO_PRIORITY
        buf_back_off_storage(true);
#endif

        /* We can only decode jpeg for embedded AA */
        if (track_id3->embed_albumart && track_id3->albumart.type == AA_TYPE_JPG)
        {
            user_data.embedded_albumart = &track_id3->albumart;
            hid = bufopen(track_id3->path, 0, TYPE_BITMAP, &user_data);
        }

        if (hid < 0 && hid != ERR_BUFFER_FULL)
        {
            /* No embedded AA or it couldn't be loaded - try other sources */
            char path[MAX_PATH];

            if (find_albumart(track_id3, path, sizeof(path),
                              &albumart_slots[i].dim))
            {
                user_data.embedded_albumart = NULL;
                hid = bufopen(path, 0, TYPE_BITMAP, &user_data);
            }
        }

#ifdef HAVE_IO_PRIORITY
        buf_back_off_storage(false);
#endif
        if (hid == ERR_BUFFER_FULL)
        {
            logf("buffer is full for now (%s)", __func__);
            return false;
        }
        else
        {
            /* If error other than a full buffer, then mark it "unsupported"
               to avoid reloading attempt */
            if (hid < 0)
            {
                logf("Album art loading failed");
                hid = ERR_UNSUPPORTED_TYPE;
            }

            *aa_hid = hid;
        }
    }

    return true;
}
#endif /* HAVE_ALBUMART */

#ifdef HAVE_CODEC_BUFFERING
/* Load a codec for the file onto the buffer - assumes we're working from the
   currently loading track - not called for the current track */
static bool audio_buffer_codec(struct track_info *track_info,
                               struct mp3entry *track_id3)
{
    /* This will not be the current track -> it cannot be the first and the
       current track cannot be ahead of buffering -> there is a previous
       track entry which is either current or ahead of the current */
    struct track_info *prev_info = track_list_last(-1);
    struct mp3entry *prev_id3 = bufgetid3(prev_info->id3_hid);

    /* If the previous codec is the same as this one, there is no need to
       put another copy of it on the file buffer (in other words, only
       buffer codecs at format transitions) */
    if (prev_id3)
    {
        int codt = get_audio_base_codec_type(track_id3->codectype);
        int prev_codt = get_audio_base_codec_type(prev_id3->codectype);

        if (codt == prev_codt)
        {
            logf("Reusing prev. codec: %d", prev_id3->codectype);
            return true;
        }
    }
    /* else just load it (harmless) */

    /* Load the codec onto the buffer if possible */
    const char *codec_fn = get_codec_filename(track_id3->codectype);
    if (!codec_fn)
        return false;

    char codec_path[MAX_PATH+1]; /* Full path to codec */
    codec_get_full_path(codec_path, codec_fn);

    track_info->codec_hid = bufopen(codec_path, 0, TYPE_CODEC, NULL);

    if (track_info->codec_hid >= 0)
    {
        logf("Buffered codec: %d", afmt);
        return true;
    }

    return false;
}
#endif /* HAVE_CODEC_BUFFERING */

/* Load metadata for the next track (with bufopen). The rest of the track
   loading will be handled by audio_finish_load_track once the metadata has
   been actually loaded by the buffering thread.

   Each track is arranged in the buffer as follows:
        <id3|[cuesheet|][album art|][codec|]audio>

   The next will not be loaded until the previous succeeds if the buffer was
   full at the time. To put any metadata after audio would make those handles
   unmovable.
*/
static int audio_load_track(void)
{
    if (in_progress_id3_hid >= 0)
    {
        /* There must be an info pointer if the in-progress id3 is even there */
        struct track_info *info = track_list_last(0);

        if (info->id3_hid == in_progress_id3_hid)
        {
            if (filling == STATE_FILLING)
            {
                /* Haven't finished the metadata but the notification is
                   anticipated to come soon */
                logf("%s(): in progress ok: %d". __func__, info->id3_hid);
                return LOAD_TRACK_OK;
            }
            else if (filling == STATE_FULL)
            {
                /* Buffer was full trying to complete the load after the
                   metadata finished, so attempt to continue - older handles
                   should have been cleared already */
                logf("%s(): finishing load: %d". __func__, info->id3_hid);
                filling = STATE_FILLING;
                buffer_event_finished_callback(&info->id3_hid);
                return LOAD_TRACK_OK;
            }
        }

        /* Some old, stray buffering message */
        logf("%s(): already in progress: %d". __func__, info->id3_hid);
        return LOAD_TRACK_ERR_BUSY;
    }

    filling = STATE_FILLING;

    struct track_info *info = track_list_alloc_track();
    if (info == NULL)
    {
        /* List is full so stop buffering tracks - however, attempt to obtain
           metadata as the unbuffered id3 */
        logf("No free tracks");
        filling = STATE_FULL;
    }

    playlist_peek_offset++;

    logf("Buffering track: s%u/c%u/e%u/p%d",
         track_list.start, track_list.current, track_list.end,
         playlist_peek_offset);

    /* Get track name from current playlist read position */
    int fd = -1;
    char name_buf[MAX_PATH + 1];
    const char *trackname;

    while (1)
    {

        trackname = playlist_peek(playlist_peek_offset, name_buf,
                                  sizeof (name_buf));

        if (!trackname)
            break;

        /* Test for broken playlists by probing for the files */
        fd = open(trackname, O_RDONLY);
        if (fd >= 0)
            break;

        logf("Open failed");
        /* Skip invalid entry from playlist */
        playlist_skip_entry(NULL, playlist_peek_offset);

        /* Sync the playlist if it isn't finished */
        if (playlist_peek(playlist_peek_offset, NULL, 0))
            playlist_next(0);
    }

    if (!trackname)
    {
        /* No track - exhausted the playlist entries */
        logf("End-of-playlist");
        id3_write_locked(UNBUFFERED_ID3, NULL);

        if (filling != STATE_FULL)
            track_list_unalloc_track(); /* Free this entry */

        playlist_peek_offset--;         /* Maintain at last index */

        /* We can end up here after the real last track signals its completion
           and miss the transition to STATE_FINISHED esp. if dropping the last
           songs of a playlist late in their load (2nd stage) */
        info = track_list_last(0);

        if (info && buf_handle_remaining(info->audio_hid) == 0)
            filling_is_finished();
        else
            filling = STATE_END_OF_PLAYLIST;

        return LOAD_TRACK_ERR_NO_MORE;
    }

    /* Successfully opened the file - get track metadata */
    if (filling == STATE_FULL ||
        (info->id3_hid = bufopen(trackname, 0, TYPE_ID3, NULL)) < 0)
    {
        /* Buffer or track list is full */
        struct mp3entry *ub_id3;

        playlist_peek_offset--;

        /* Load the metadata for the first unbuffered track */
        ub_id3 = id3_get(UNBUFFERED_ID3);
        id3_mutex_lock();
        get_metadata(ub_id3, fd, trackname);
        id3_mutex_unlock();

        if (filling != STATE_FULL)
        {
            track_list_unalloc_track();
            filling = STATE_FULL;
        }

        logf("%s: buffer is full for now (%u tracks)", __func__,
             track_list_count());
    }
    else
    {
        /* Successful load initiation */
        info->filesize = filesize(fd);
        in_progress_id3_hid = info->id3_hid; /* Remember what's in-progress */
    }

    close(fd);
    return LOAD_TRACK_OK;
}

/* Second part of the track loading: We now have the metadata available, so we
   can load the codec, the album art and finally the audio data.
   This is called on the audio thread after the buffering thread calls the
   buffering_handle_finished_callback callback. */
static int audio_finish_load_track(struct track_info *info)
{
    int trackstat = LOAD_TRACK_OK;

    if (info->id3_hid != in_progress_id3_hid)
    {
        /* We must not be here if not! */
        logf("%s: wrong track %d/%d", __func__, info->id3_hid,
             in_progress_id3_hid);
        return LOAD_TRACK_ERR_BUSY;
    }

    /* The current track for decoding (there is always one if the list is
       populated) */
    struct track_info *cur_info = track_list_current(0);
    struct mp3entry *track_id3 = valid_mp3entry(bufgetid3(info->id3_hid));

    if (!track_id3)
    {
        /* This is an error condition. Track cannot be played without valid
           metadata; skip the track. */
        logf("No metadata");
        trackstat = LOAD_TRACK_ERR_FINISH_FAILED;
        goto audio_finish_load_track_exit;
    }

    /* Try to load a cuesheet for the track */
    if (!audio_load_cuesheet(info, track_id3))
    {
        /* No space for cuesheet on buffer, not an error */
        filling = STATE_FULL;
        goto audio_finish_load_track_exit;
    }

#ifdef HAVE_ALBUMART
    /* Try to load album art for the track */
    if (!audio_load_albumart(info, track_id3))
    {
        /* No space for album art on buffer, not an error */
        filling = STATE_FULL;
        goto audio_finish_load_track_exit;
    }
#endif

    /* All handles available to external routines are ready - audio and codec
       information is private */

    if (info == track_list_user_current(0))
    {
        /* Send only when the track handles could not all be opened ahead of
           time for the user's current track - otherwise everything is ready
           by the time PLAYBACK_EVENT_TRACK_CHANGE is sent */
        send_event(PLAYBACK_EVENT_CUR_TRACK_READY, id3_get(PLAYING_ID3));
    }

#ifdef HAVE_CODEC_BUFFERING
    /* Try to buffer a codec for the track */
    if (info != cur_info && !audio_buffer_codec(info, track_id3))
    {
        if (info->codec_hid == ERR_BUFFER_FULL)
        {
            /* No space for codec on buffer, not an error */
            filling = STATE_FULL;
            logf("buffer is full for now (%s)", __func__);
        }
        else
        {
            /* This is an error condition, either no codec was found, or
               reading the codec file failed part way through, either way,
               skip the track */
            logf("No codec for: %s", track_id3->path);
            trackstat = LOAD_TRACK_ERR_FINISH_FAILED;
        }

        goto audio_finish_load_track_exit;
    }
#endif /* HAVE_CODEC_BUFFERING */

    /** Finally, load the audio **/
    size_t file_offset = 0;
    track_id3->elapsed = 0;

    if (track_id3->offset >= info->filesize)
        track_id3->offset = 0;

    logf("%s: set offset for %s to %lu\n", __func__,
         id3->title, (unsigned long)offset);

    /* Adjust for resume rewind so we know what to buffer - starting the codec
       calls it again, so we don't save it (and they shouldn't accumulate) */
    size_t offset = resume_rewind_adjusted_offset(track_id3);

    enum data_type audiotype = get_audio_base_data_type(track_id3->codectype);

    if (audiotype == TYPE_ATOMIC_AUDIO)
        logf("Loading atomic %d", track_id3->codectype);

    if (format_buffers_with_offset(track_id3->codectype))
    {
        /* This format can begin buffering from any point */
        file_offset = offset;
    }

    logf("load track: %s", track_id3->path);

    if (file_offset > AUDIO_REBUFFER_GUESS_SIZE)
    {
        /* We can buffer later in the file, adjust the hunt-and-peck margin */
        file_offset -= AUDIO_REBUFFER_GUESS_SIZE;
    }
    else
    {
        /* No offset given or it is very minimal - begin at the first frame
           according to the metadata */
        file_offset = track_id3->first_frame_offset;
    }

    int hid = bufopen(track_id3->path, file_offset, audiotype, NULL);

    if (hid >= 0)
    {
        info->audio_hid = hid;

        if (info == cur_info)
        {
            /* This is the current track to decode - should be started now */
            trackstat = LOAD_TRACK_READY;
        }
    }
    else
    {
        /* Buffer could be full but not properly so if this is the only
           track! */
        if (hid == ERR_BUFFER_FULL && audio_track_count() > 1)
        {
            filling = STATE_FULL;
            logf("Buffer is full for now (%s)", __func__);
        }
        else
        {
            /* Nothing to play if no audio handle - skip this */
            logf("Could not add audio data handle");
            trackstat = LOAD_TRACK_ERR_FINISH_FAILED;
        }
    }

audio_finish_load_track_exit:
    if (trackstat < LOAD_TRACK_OK)
    {
        playlist_skip_entry(NULL, playlist_peek_offset);
        track_info_close(info);
        track_list_unalloc_track();

        if (playlist_peek(playlist_peek_offset, NULL, 0))
            playlist_next(0);

        playlist_peek_offset--;
    }

    if (filling != STATE_FULL)
    {
        /* Load next track - error or not */
        in_progress_id3_hid = ERR_HANDLE_NOT_FOUND;
        LOGFQUEUE("audio > audio Q_AUDIO_FILL_BUFFER");
        audio_queue_post(Q_AUDIO_FILL_BUFFER, 0);
    }
    else
    {
        /* Full */
        trackstat = LOAD_TRACK_ERR_FINISH_FULL;
    }

    return trackstat;
}

/* Start a new track load */
static int audio_fill_file_buffer(void)
{
    if (play_status == PLAY_STOPPED)
        return LOAD_TRACK_ERR_FAILED;

    trigger_cpu_boost();

    /* Must reset the buffer before use if trashed or voice only - voice
       file size shouldn't have changed so we can go straight from
       AUDIOBUF_STATE_VOICED_ONLY to AUDIOBUF_STATE_INITIALIZED */
    if (buffer_state != AUDIOBUF_STATE_INITIALIZED)
        audio_reset_buffer();

    logf("Starting buffer fill");

    int trackstat = audio_load_track();

    if (trackstat >= LOAD_TRACK_OK)
    {
        if (track_list_current(0) == track_list_user_current(0))
            playlist_next(0);

        if (filling == STATE_FULL && !track_list_user_current(1))
        {
            /* There are no user tracks on the buffer after this therefore
               this is the next track */
            audio_update_and_announce_next_track(id3_get(UNBUFFERED_ID3));
        }
    }

    return trackstat;
}

/* Discard unwanted tracks and start refill from after the specified playlist
   offset */
static int audio_reset_and_rebuffer(
    enum track_clear_action action, int peek_offset)
{
    logf("Forcing rebuffer: 0x%X, %d", flags, peek_offset);

    id3_write_locked(UNBUFFERED_ID3, NULL);

    /* Remove unwanted tracks - caller must have ensured codec isn't using
       any */
    track_list_clear(action);

    /* Refill at specified position (-1 starts at index offset 0) */
    playlist_peek_offset = peek_offset;

    /* Fill the buffer */
    return audio_fill_file_buffer();
}

/* Handle buffering events
   (Q_AUDIO_BUFFERING) */
static void audio_on_buffering(int event)
{
    enum track_clear_action action;
    int peek_offset;

    if (track_list_empty())
        return;

    switch (event)
    {
    case BUFFER_EVENT_BUFFER_LOW:
        if (filling != STATE_FULL && filling != STATE_END_OF_PLAYLIST)
            return; /* Should be nothing left to fill */

        /* Clear old tracks and continue buffering where it left off */
        action = TRACK_LIST_KEEP_NEW;
        peek_offset = playlist_peek_offset;
        break;

    case BUFFER_EVENT_REBUFFER:
        /* Remove all but the currently decoding track and redo buffering
           after that */
        action = TRACK_LIST_KEEP_CURRENT;
        peek_offset = (skip_pending == TRACK_SKIP_AUTO) ? 1 : 0;
        break;

    default:
        return;
    }

    switch (skip_pending)
    {
    case TRACK_SKIP_NONE:
    case TRACK_SKIP_AUTO:
    case TRACK_SKIP_AUTO_NEW_PLAYLIST:
        audio_reset_and_rebuffer(action, peek_offset);
        break;

    case TRACK_SKIP_AUTO_END_PLAYLIST:
        /* Already finished */
        break;

    default:
        /* Invalid */
        logf("Buffering call, inv. state: %d", (int)skip_pending);
    }
}

/* Handle starting the next track load
   (Q_AUDIO_FILL_BUFFER) */
static void audio_on_fill_buffer(void)
{
    audio_handle_track_load_status(audio_fill_file_buffer());
}

/* Handle posted load track finish event
   (Q_AUDIO_FINISH_LOAD_TRACK) */
static void audio_on_finish_load_track(int id3_hid)
{
    struct track_info *info = track_list_last(0);

    if (!info || !buf_is_handle(id3_hid))
        return;

    if (info == track_list_user_current(1))
    {
        /* Just loaded the metadata right after the current position */
        audio_update_and_announce_next_track(bufgetid3(info->id3_hid));
    }

    if (audio_finish_load_track(info) != LOAD_TRACK_READY)
        return; /* Not current track */

    bool is_user_current = info == track_list_user_current(0);

    if (is_user_current)
    {
        /* Copy cuesheet */
        buf_read_cuesheet(info->cuesheet_hid);
    }

    if (audio_start_codec(automatic_skip))
    {
        if (is_user_current)
        {
            /* Be sure all tagtree info is synchronized; it will be needed for the
               track finish event - the sync will happen when finalizing a track
               change otherwise */
            bool was_valid = valid_mp3entry(id3_get(PLAYING_ID3));

            playing_id3_sync(info, -1);

            if (!was_valid)
            {
                /* Playing id3 hadn't been updated yet because no valid track
                   was yet available - treat like the first track */
                audio_playlist_track_change();
            }
        }
    }
    else
    {
        audio_handle_track_load_status(LOAD_TRACK_ERR_START_CODEC);
    }
}

/* Called when handles other than metadata handles have finished buffering
   (Q_AUDIO_HANDLE_FINISHED) */
static void audio_on_handle_finished(int hid)
{
    /* Right now, only audio handles should end up calling this */
    if (filling == STATE_END_OF_PLAYLIST)
    {
        struct track_info *info = track_list_last(0);

        /* Really we don't know which order the handles will actually complete
           to zero bytes remaining since another thread is doing it - be sure
           it's the right one */
        if (info && info->audio_hid == hid)
        {
            /* This was the last track in the playlist and we now have all the
               data we need */
            filling_is_finished();
        }
    }
}

/* Called to make an outstanding track skip the current track and to send the
   transition events */
static void audio_finalise_track_change(void)
{
    switch (skip_pending)
    {
    case TRACK_SKIP_NONE: /* Manual skip */
        break;

    case TRACK_SKIP_AUTO:
    case TRACK_SKIP_AUTO_NEW_PLAYLIST:
    {
        int playlist_delta = skip_pending == TRACK_SKIP_AUTO ? 1 : 0;
        audio_playlist_track_finish();

        if (!playlist_peek(playlist_delta, NULL, 0))
        {
            /* Track ended up rejected - push things ahead like the codec blew
               it (because it was never started and now we're here where it
               should have been decoding the next track by now) - next, a
               directory change or end of playback will most likely happen */
            skip_pending = TRACK_SKIP_NONE;
            audio_handle_track_load_status(LOAD_TRACK_ERR_START_CODEC);
            return;
        }

        if (!playlist_delta)
            break;

        playlist_peek_offset -= playlist_delta;
        if (playlist_next(playlist_delta) >= 0)
            break;
            /* What!? Disappear? Hopeless bleak despair */
        }
        /* Fallthrough */
    case TRACK_SKIP_AUTO_END_PLAYLIST:
    default:            /* Invalid */
        filling = STATE_ENDED;
        audio_stop_playback();
        return;
    }

    struct track_info *info = track_list_current(0);
    struct mp3entry *track_id3 = NULL;

    id3_mutex_lock();

    /* Update the current cuesheet if any and enabled */
    if (info)
    {
        buf_read_cuesheet(info->cuesheet_hid);
        track_id3 = bufgetid3(info->id3_hid);
    }

    id3_write(PLAYING_ID3, track_id3);

    /* The skip is technically over */
    skip_pending = TRACK_SKIP_NONE;

    /* Sync the next track information */
    info = track_list_current(1);

    id3_write(NEXTTRACK_ID3, info ? bufgetid3(info->id3_hid) :
                                    id3_get(UNBUFFERED_ID3));

    id3_mutex_unlock();

    audio_playlist_track_change();
}

/* Actually begin a transition and take care of the codec change - may complete
   it now or ask pcmbuf for notification depending on the type */
static void audio_begin_track_change(enum pcm_track_change_type type,
                                     int trackstat)
{
    /* Even if the new track is bad, the old track must be finished off */
    pcmbuf_start_track_change(type);

    bool auto_skip = type != TRACK_CHANGE_MANUAL;

    if (!auto_skip)
    {
        /* Manual track change happens now */
        audio_finalise_track_change();
        pcmbuf_sync_position_update();

        if (play_status == PLAY_STOPPED)
            return; /* Stopped us */
    }

    if (trackstat >= LOAD_TRACK_OK)
    {
        struct track_info *info = track_list_current(0);

        if (info->audio_hid < 0)
            return;

        /* Everything needed for the codec is ready - start it */
        if (audio_start_codec(auto_skip))
        {
            if (!auto_skip)
                playing_id3_sync(info, -1);
            return;
        }

        trackstat = LOAD_TRACK_ERR_START_CODEC;
    }

    audio_handle_track_load_status(trackstat);
}

/* Transition to end-of-playlist state and begin wait for PCM to finish */
static void audio_monitor_end_of_playlist(void)
{
    skip_pending = TRACK_SKIP_AUTO_END_PLAYLIST;
    filling = STATE_ENDING;
    pcmbuf_start_track_change(TRACK_CHANGE_END_OF_DATA);
}

/* Codec has completed decoding the track
   (usually Q_AUDIO_CODEC_COMPLETE) */
static void audio_on_codec_complete(int status)
{
    logf("%s(%d)", __func__, status);

    if (play_status == PLAY_STOPPED)
        return;

    /* If it didn't notify us first, don't expect "seek complete" message
       since the codec can't post it now - do things like it would have
       done */
    audio_complete_codec_seek();

    if (play_status == PLAY_PAUSED || skip_pending != TRACK_SKIP_NONE)
    {
        /* Old-hay on the ip-skay - codec has completed decoding

           Paused: We're not sounding it, so just remember that it happened
                   and the resume will begin the transition

           Skipping: There was already a skip in progress, remember it and
                     allow no further progress until the PCM from the previous
                     song has finished
         */
        codec_skip_pending = true;
        codec_skip_status = status;
        return;
    }

    codec_skip_pending = false;

    int trackstat = LOAD_TRACK_OK;

    automatic_skip = true;
    skip_pending = TRACK_SKIP_AUTO;

    /* Does this track have an entry allocated? */
    struct track_info *info = track_list_advance_current(1);

    if (!info || info->audio_hid < 0)
    {
        bool end_of_playlist = false;

        if (info)
        {
            /* Track load is not complete - it might have stopped on a
               full buffer without reaching the audio handle or we just
               arrived at it early

               If this type is atomic and we couldn't get the audio,
               perhaps it would need to wrap to make the allocation and
               handles are in the way - to maximize the liklihood it can
               be allocated, clear all handles to reset the buffer and
               its indexes to 0 - for packet audio, this should not be an
               issue and a pointless full reload of all the track's
               metadata may be avoided */

            struct mp3entry *track_id3 = bufgetid3(info->id3_hid);

            if (track_id3 &&
                get_audio_base_data_type(track_id3->codectype)
                            == TYPE_PACKET_AUDIO)
            {
                /* Continue filling after this track */
                audio_reset_and_rebuffer(TRACK_LIST_KEEP_CURRENT, 1);
                audio_begin_track_change(TRACK_CHANGE_AUTO, trackstat);
                return;
            }
            /* else rebuffer at this track; status applies to the track we
               want */
        }
        else if (!playlist_peek(1, NULL, 0))
        {
            /* Play sequence is complete - directory change or other playlist
               resequencing - the playlist must now be advanced in order to
               continue since a peek ahead to the next track is not possible */
            skip_pending = TRACK_SKIP_AUTO_NEW_PLAYLIST;
            end_of_playlist = playlist_next(1) < 0;
        }

        if (!end_of_playlist)
        {
            trackstat = audio_reset_and_rebuffer(TRACK_LIST_CLEAR_ALL,
                            skip_pending == TRACK_SKIP_AUTO ? 0 : -1);

            if (trackstat == LOAD_TRACK_ERR_NO_MORE)
            {
                /* Failed to find anything after all - do playlist switchover
                   instead */
                skip_pending = TRACK_SKIP_AUTO_NEW_PLAYLIST;
                end_of_playlist = playlist_next(1) < 0;
            }
        }

        if (end_of_playlist)
        {
            audio_monitor_end_of_playlist();
            return;
        }
    }

    audio_begin_track_change(TRACK_CHANGE_AUTO, trackstat);
}

/* Called when codec completes seek operation
   (usually Q_AUDIO_CODEC_SEEK_COMPLETE) */
static void audio_on_codec_seek_complete(void)
{
    logf("%s()", __func__);
    audio_complete_codec_seek();
    codec_go();
}

/* Called when PCM track change has completed
   (Q_AUDIO_TRACK_CHANGED) */
static void audio_on_track_changed(void)
{
    /* Finish whatever is pending so that the WPS is in sync */
    audio_finalise_track_change();

    if (codec_skip_pending)
    {
        /* Codec got ahead completing a short track - complete the
           codec's skip and begin the next */
        codec_skip_pending = false;
        audio_on_codec_complete(codec_skip_status);
    }
}

/* Begin playback from an idle state, transition to a new playlist or
   invalidate the buffer and resume (if playing).
   (usually Q_AUDIO_PLAY, Q_AUDIO_REMAKE_AUDIO_BUFFER) */
static void audio_start_playback(size_t offset, unsigned int flags)
{
    enum play_status old_status = play_status;

    if (flags & AUDIO_START_NEWBUF)
    {
        /* Mark the buffer dirty - if not playing, it will be reset next
           time */
        if (buffer_state == AUDIOBUF_STATE_INITIALIZED)
            buffer_state = AUDIOBUF_STATE_VOICED_ONLY;
    }

    if (old_status != PLAY_STOPPED)
    {
        logf("%s(%lu): skipping", __func__, (unsigned long)offset);

        halt_decoding_track(true);

        automatic_skip = false;
        ff_rw_mode = false;

        if (flags & AUDIO_START_RESTART)
        {
            /* Clear out some stuff to resume the current track where it
               left off */
            pcmbuf_play_stop();
            offset = id3_get(PLAYING_ID3)->offset;
            track_list_clear(TRACK_LIST_CLEAR_ALL);
        }
        else
        {
            /* This is more-or-less treated as manual track transition */
            /* Save resume information for current track */
            audio_playlist_track_finish();
            track_list_clear(TRACK_LIST_CLEAR_ALL);

            /* Indicate manual track change */
            pcmbuf_start_track_change(TRACK_CHANGE_MANUAL);
            wipe_track_metadata(true);
        }

        /* Set after track finish event in case skip was in progress */
        skip_pending = TRACK_SKIP_NONE;
    }
    else
    {
        if (flags & AUDIO_START_RESTART)
            return; /* Must already be playing */

        /* Cold playback start from a stopped state */
        logf("%s(%lu): starting", __func__, offset);

        /* Set audio parameters */
#if INPUT_SRC_CAPS != 0
        audio_set_input_source(AUDIO_SRC_PLAYBACK, SRCF_PLAYBACK);
        audio_set_output_source(AUDIO_SRC_PLAYBACK);
#endif
#ifndef PLATFORM_HAS_VOLUME_CHANGE
        sound_set_volume(global_settings.volume);
#endif
        /* Be sure channel is audible */
        pcmbuf_fade(false, true);

        /* Update our state */
        play_status = PLAY_PLAYING;
    }

    /* Codec's position should be available as soon as it knows it */
    position_key = pcmbuf_get_position_key();
    pcmbuf_sync_position_update();

    /* Start fill from beginning of playlist */
    playlist_peek_offset = -1;
    buf_set_base_handle(-1);

    /* Officially playing */
    queue_reply(&audio_queue, 1);

    /* Add these now - finish event for the first id3 will most likely be sent
       immediately */
    add_event(BUFFER_EVENT_REBUFFER, false, buffer_event_rebuffer_callback);
    add_event(BUFFER_EVENT_FINISHED, false, buffer_event_finished_callback);

    if (old_status == PLAY_STOPPED)
    {
        /* Send coldstart event */
        send_event(PLAYBACK_EVENT_START_PLAYBACK, NULL);
    }

    /* Fill the buffer */
    int trackstat = audio_fill_file_buffer();

    if (trackstat >= LOAD_TRACK_OK)
    {
        /* This is the currently playing track - get metadata, stat */
        playing_id3_sync(track_list_current(0), offset);

        if (valid_mp3entry(id3_get(PLAYING_ID3)))
        {
            /* Only if actually changing tracks... */
            if (!(flags & AUDIO_START_RESTART))
                audio_playlist_track_change();
        }
    }
    else
    {
        /* Found nothing playable */
        audio_handle_track_load_status(trackstat);
    }
}

/* Stop playback and enter an idle state
   (usually Q_AUDIO_STOP) */
static void audio_stop_playback(void)
{
    logf("%s()", __func__);

    if (play_status == PLAY_STOPPED)
        return;

    bool do_fade = global_settings.fade_on_stop && filling != STATE_ENDED;

    pcmbuf_fade(do_fade, false);

    /* Wait for fade-out */
    audio_wait_fade_complete();

    /* Stop the codec and unload it */
    halt_decoding_track(true);
    pcmbuf_play_stop();
    codec_unload();

    /* Save resume information  - "filling" might have been set to
       "STATE_ENDED" by caller in order to facilitate end of playlist */
    audio_playlist_track_finish();

    skip_pending = TRACK_SKIP_NONE;
    automatic_skip = false;

    /* Close all tracks and mark them NULL */
    remove_event(BUFFER_EVENT_REBUFFER, buffer_event_rebuffer_callback);
    remove_event(BUFFER_EVENT_FINISHED, buffer_event_finished_callback);
    remove_event(BUFFER_EVENT_BUFFER_LOW, buffer_event_buffer_low_callback);

    track_list_clear(TRACK_LIST_CLEAR_ALL);

    /* Update our state */
    ff_rw_mode = false;
    play_status = PLAY_STOPPED;

    wipe_track_metadata(true);

    /* Go idle */
    filling = STATE_IDLE;
    cancel_cpu_boost();
}

/* Pause the playback of the current track
   (Q_AUDIO_PAUSE) */
static void audio_on_pause(bool pause)
{
    logf("%s(%s)", __func__, pause ? "true" : "false");

    if (play_status == PLAY_STOPPED || pause == (play_status == PLAY_PAUSED))
        return;

    play_status = pause ? PLAY_PAUSED : PLAY_PLAYING;

    if (!pause && codec_skip_pending)
    {
        /* Actually do the skip that is due - resets the status flag */
        audio_on_codec_complete(codec_skip_status);
    }

    bool do_fade = global_settings.fade_on_stop;

    pcmbuf_fade(do_fade, !pause);

    if (!ff_rw_mode && !(do_fade && pause))
    {
        /* Not in ff/rw mode - can actually change the audio state now */
        pcmbuf_pause(pause);
    }
}

/* Skip a certain number of tracks forwards or backwards
   (Q_AUDIO_SKIP) */
static void audio_on_skip(void)
{
    id3_mutex_lock();

    /* Eat the delta to keep it synced, even if not playing */
    int toskip = skip_offset;
    skip_offset = 0;

    logf("%s(): %d", __func__, toskip);

    id3_mutex_unlock();

    if (play_status == PLAY_STOPPED)
        return;

    /* Force codec to abort this track */
    halt_decoding_track(true);

    /* Kill the ff/rw halt */
    ff_rw_mode = false;

    /* Manual skip */
    automatic_skip = false;

    /* If there was an auto skip in progress, there will be residual
       advancement of the playlist and/or track list so compensation will be
       required in order to end up in the right spot */
    int track_list_delta = toskip;
    int playlist_delta = toskip;

    if (skip_pending != TRACK_SKIP_NONE)
    {
        if (skip_pending != TRACK_SKIP_AUTO_END_PLAYLIST)
            track_list_delta--;

        if (skip_pending == TRACK_SKIP_AUTO_NEW_PLAYLIST)
            playlist_delta--;
    }

    audio_playlist_track_finish();
    skip_pending = TRACK_SKIP_NONE;

    /* Update the playlist current track now */
    while (playlist_next(playlist_delta) < 0)
    {
        /* Manual skip out of range (because the playlist wasn't updated
           yet by us and so the check in audio_skip returned 'ok') - bring
           back into range */
        int d = toskip < 0 ? 1 : -1;

        while (!playlist_check(playlist_delta))
        {
            if (playlist_delta == d)
            {
                /* Had to move the opposite direction to correct, which is
                   wrong - this is the end */
                filling = STATE_ENDED;
                audio_stop_playback();
                return;
            }

            playlist_delta += d;
            track_list_delta += d;
        }
    }

    /* Adjust things by how much the playlist was manually moved */
    playlist_peek_offset -= playlist_delta;

    struct track_info *info = track_list_advance_current(track_list_delta);
    int trackstat = LOAD_TRACK_OK;

    if (!info || info->audio_hid < 0)
    {
        /* We don't know the next track thus we know we don't have it */
        trackstat = audio_reset_and_rebuffer(TRACK_LIST_CLEAR_ALL, -1);
    }

    audio_begin_track_change(TRACK_CHANGE_MANUAL, trackstat);
}

/* Skip to the next/previous directory
   (Q_AUDIO_DIR_SKIP) */
static void audio_on_dir_skip(int direction)
{
    logf("%s(%d)", __func__, direction);

    id3_mutex_lock();
    skip_offset = 0;
    id3_mutex_unlock();

    if (play_status == PLAY_STOPPED)
        return;

    /* Force codec to abort this track */
    halt_decoding_track(true);

    /* Kill the ff/rw halt */
    ff_rw_mode = false;

    /* Manual skip */
    automatic_skip = false;

    audio_playlist_track_finish();

    /* Unless automatic and gapless, skips do not pend */
    skip_pending = TRACK_SKIP_NONE;

    /* Regardless of the return value we need to rebuffer. If it fails the old
       playlist will resume, else the next dir will start playing. */
    playlist_next_dir(direction);

    wipe_track_metadata(false);

    int trackstat = audio_reset_and_rebuffer(TRACK_LIST_CLEAR_ALL, -1);

    if (trackstat == LOAD_TRACK_ERR_NO_MORE)
    {
        /* The day the music died - finish-off whatever is playing and call it
           quits */
        audio_monitor_end_of_playlist();
        return;
    }

    audio_begin_track_change(TRACK_CHANGE_MANUAL, trackstat);
}

/* Enter seek mode in order to start a seek
   (Q_AUDIO_PRE_FF_REWIND) */
static void audio_on_pre_ff_rewind(void)
{
    logf("%s()", __func__);

    if (play_status == PLAY_STOPPED || ff_rw_mode)
        return;

    ff_rw_mode = true;

    audio_wait_fade_complete();

    if (play_status == PLAY_PAUSED)
        return;

    pcmbuf_pause(true);
}

/* Seek the playback of the current track to the specified time
   (Q_AUDIO_FF_REWIND) */
static void audio_on_ff_rewind(long time)
{
    logf("%s(%ld)", __func__, time);

    if (play_status == PLAY_STOPPED)
        return;

    enum track_skip_type pending = skip_pending;

    switch (pending)
    {
    case TRACK_SKIP_NONE:              /* The usual case */
    case TRACK_SKIP_AUTO:              /* Have to back it out (fun!) */
    case TRACK_SKIP_AUTO_END_PLAYLIST: /* Still have the last codec used */
    {
        struct mp3entry *id3 = id3_get(PLAYING_ID3);
        struct mp3entry *ci_id3 = id3_get(CODEC_ID3);

        automatic_skip = false;

        /* Send event before clobbering the time */
        /* FIXME: Nasty, but the tagtree expects this so that rewinding and
           then skipping back to this track resumes properly. Something else
           should be sent. We're not _really_ finishing the track are we? */
        if (time == 0)
            send_event(PLAYBACK_EVENT_TRACK_FINISH, id3);

        id3->elapsed = time;
        queue_reply(&audio_queue, 1);

        bool haltres = halt_decoding_track(pending == TRACK_SKIP_AUTO);

        /* Need this set in case ff/rw mode + error but _after_ the codec
           halt that will reset it */
        codec_seeking = true;

        /* If in transition, key will have changed - sync to it */
        position_key = pcmbuf_get_position_key();

        if (pending == TRACK_SKIP_AUTO)
        {
            if (!track_list_advance_current(-1))
            {
                /* Not in list - must rebuffer at the current playlist index */
                if (audio_reset_and_rebuffer(TRACK_LIST_CLEAR_ALL, -1)
                        < LOAD_TRACK_OK)
                {
                    /* Codec is stopped */
                    break;
                }
            }
        }

        /* Set after audio_fill_file_buffer to disable playing id3 clobber if
           rebuffer is needed */
        skip_pending = TRACK_SKIP_NONE;
        struct track_info *cur_info = track_list_current(0);

        /* Track must complete the loading _now_ since a codec and audio
           handle are needed in order to do the seek */
        if (cur_info->audio_hid < 0 &&
            audio_finish_load_track(cur_info) != LOAD_TRACK_READY)
        {
            /* Call above should push any load sequence - no need for
               halt_decoding_track here if no skip was pending here because
               there would not be a codec started if no audio handle was yet
               opened */
            break;
        }

        if (pending == TRACK_SKIP_AUTO)
        {
            if (!bufreadid3(cur_info->id3_hid, ci_id3) ||
                !audio_init_codec(cur_info, ci_id3))
            {
                /* We should have still been able to get it - skip it and move
                   onto the next one - like it or not this track is broken */
                break;
            }

            /* Set the codec API to the correct metadata and track info */
            ci.audio_hid = cur_info->audio_hid;
            ci.filesize = cur_info->filesize;
            buf_set_base_handle(cur_info->audio_hid);
        }

        if (!haltres)
        {
            /* If codec must be (re)started, reset the offset */
            ci_id3->offset = 0;
        }

        codec_seek(time);
        return;
        }

    case TRACK_SKIP_AUTO_NEW_PLAYLIST:
    {
        /* We cannot do this because the playlist must be reversed by one
           and it doesn't always return the same song when going backwards
           across boundaries as forwards (either because of randomization
           or inconsistency in deciding what the previous track should be),
           therefore the whole operation would often end up as nonsense -
           lock out seeking for a couple seconds */

        /* Sure as heck cancel seek mode too! */
        audio_ff_rewind_end();
        return;
        }

    default:
        /* Won't see this */
        return;
    }

    if (play_status == PLAY_STOPPED)
    {
        /* Playback ended because of an error completing a track load */
        return;
    }

    /* Always fake it as a codec start error which will handle mode
       cancellations and skip to the next track */
    audio_handle_track_load_status(LOAD_TRACK_ERR_START_CODEC);
}

/* Invalidates all but currently playing track
   (Q_AUDIO_FLUSH) */
static void audio_on_audio_flush(void)
{
    logf("%s", __func__);

    if (track_list_empty())
        return; /* Nothing to flush out */

    switch (skip_pending)
    {
    case TRACK_SKIP_NONE:
    case TRACK_SKIP_AUTO_END_PLAYLIST:
        /* Remove all but the currently playing track from the list and
           refill after that */
        track_list_clear(TRACK_LIST_KEEP_CURRENT);
        playlist_peek_offset = 0;
        id3_write_locked(UNBUFFERED_ID3, NULL);
        audio_update_and_announce_next_track(NULL);

        /* Ignore return since it's about the next track, not this one */
        audio_fill_file_buffer();

        if (skip_pending == TRACK_SKIP_NONE)
            break;

        /* There's now a track after this one now - convert to auto skip -
           no skip should pend right now because multiple flush messages can
           be fired which would cause a restart in the below cases */
        skip_pending = TRACK_SKIP_NONE;
        audio_clear_track_notifications();
        audio_queue_post(Q_AUDIO_CODEC_COMPLETE, CODEC_OK);
        break;

    case TRACK_SKIP_AUTO:
    case TRACK_SKIP_AUTO_NEW_PLAYLIST:
        /* Precisely removing what it already decoded for the next track is
           not possible so a restart is required in order to continue the
           currently playing track without the now invalid future track
           playing */
        audio_start_playback(0, AUDIO_START_RESTART);
        break;

    default: /* Nothing else is a state */
        break;
    }
}

#ifdef AUDIO_HAVE_RECORDING
/* Load the requested encoder type
   (Q_AUDIO_LOAD_ENCODER) */
static void audio_on_load_encoder(int afmt)
{
    bool res = true;

    if (play_status != PLAY_STOPPED)
        audio_stop_playback(); /* Can't load both types at once */
    else
        codec_unload(); /* Encoder still loaded, stop and unload it */

    if (afmt != AFMT_UNKNOWN)
    {
        res = codec_load(-1, afmt | CODEC_TYPE_ENCODER);
        if (res)
            codec_go(); /* These are run immediately */
    }

    queue_reply(&audio_queue, res);
}
#endif /* AUDIO_HAVE_RECORDING */

static void audio_thread(void)
{
    struct queue_event ev;

    pcm_postinit();

    while (1)
    {
        switch (filling)
        {
        /* Active states */
        case STATE_FULL:
        case STATE_END_OF_PLAYLIST:
            if (buf_get_watermark() == 0)
            {
                /* End of buffering for now, let's calculate the watermark,
                   register for a low buffer event and unboost */
                audio_update_filebuf_watermark(0);
                add_event(BUFFER_EVENT_BUFFER_LOW, true,
                          buffer_event_buffer_low_callback);
            }
            /* Fall-through */
        case STATE_FINISHED:
            /* All data was buffered */
            cancel_cpu_boost();
            /* Fall-through */
        case STATE_FILLING:
        case STATE_ENDING:
            if (audio_pcmbuf_track_change_scan())
            {
                /* Transfer notification to audio queue event */
                ev.id = Q_AUDIO_TRACK_CHANGED;
                ev.data = 1;
            }
            else
            {
                /* If doing auto skip, poll pcmbuf track notifications a bit
                   faster to promply detect the transition */
                queue_wait_w_tmo(&audio_queue, &ev,
                                 skip_pending == TRACK_SKIP_NONE ?
                                    HZ/2 : HZ/10);
            }
            break;

        /* Idle states */
        default:
            queue_wait(&audio_queue, &ev);

#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
            switch (ev.id)
            {
#ifdef AUDIO_HAVE_RECORDING
            /* Must monitor the encoder message for recording so it can remove
               it if we process the insertion before it does. It cannot simply
               be removed from under recording however. */
            case Q_AUDIO_LOAD_ENCODER:
                break;
#endif
            case SYS_USB_DISCONNECTED:
                filling = STATE_IDLE;
                break;

            default:
                if (filling == STATE_USB)
                    continue;
            }
#endif /* CONFIG_PLATFORM */
        }

        switch (ev.id)
        {
        /** Codec and track change messages **/
        case Q_AUDIO_CODEC_COMPLETE:
            /* Codec is done processing track and has gone idle */
            LOGFQUEUE("audio < Q_AUDIO_CODEC_COMPLETE: %ld", (long)ev.data);
            audio_on_codec_complete(ev.data);
            break;

        case Q_AUDIO_CODEC_SEEK_COMPLETE:
            /* Codec is done seeking */
            LOGFQUEUE("audio < Q_AUDIO_SEEK_COMPLETE");
            audio_on_codec_seek_complete();
            break;

        case Q_AUDIO_TRACK_CHANGED:
            /* PCM track change done */
            LOGFQUEUE("audio < Q_AUDIO_TRACK_CHANGED");
            audio_on_track_changed();
            break;

        /** Control messages **/
        case Q_AUDIO_PLAY:
            LOGFQUEUE("audio < Q_AUDIO_PLAY");
            audio_start_playback(ev.data, 0);
            break;

        case Q_AUDIO_STOP:
            LOGFQUEUE("audio < Q_AUDIO_STOP");
            audio_stop_playback();
            if (ev.data != 0)
                queue_clear(&audio_queue);
            break;

        case Q_AUDIO_PAUSE:
            LOGFQUEUE("audio < Q_AUDIO_PAUSE");
            audio_on_pause(ev.data);
            break;

        case Q_AUDIO_SKIP:
            LOGFQUEUE("audio < Q_AUDIO_SKIP");
            audio_on_skip();
            break;

        case Q_AUDIO_DIR_SKIP:
            LOGFQUEUE("audio < Q_AUDIO_DIR_SKIP");
            audio_on_dir_skip(ev.data);
            break;

        case Q_AUDIO_PRE_FF_REWIND:
            LOGFQUEUE("audio < Q_AUDIO_PRE_FF_REWIND");
            audio_on_pre_ff_rewind();
            break;

        case Q_AUDIO_FF_REWIND:
            LOGFQUEUE("audio < Q_AUDIO_FF_REWIND");
            audio_on_ff_rewind(ev.data);
            break;

        case Q_AUDIO_FLUSH:
            LOGFQUEUE("audio < Q_AUDIO_FLUSH: %d", (int)ev.data);
            audio_on_audio_flush();
            break;

        /** Buffering messages **/
        case Q_AUDIO_BUFFERING:
            /* some buffering event */
            LOGFQUEUE("audio < Q_AUDIO_BUFFERING: %d", (int)ev.data);
            audio_on_buffering(ev.data);
            break;

        case Q_AUDIO_FILL_BUFFER:
            /* continue buffering next track */
            LOGFQUEUE("audio < Q_AUDIO_FILL_BUFFER");
            audio_on_fill_buffer();
            break;

        case Q_AUDIO_FINISH_LOAD_TRACK:
            /* metadata is buffered */
            LOGFQUEUE("audio < Q_AUDIO_FINISH_LOAD_TRACK");
            audio_on_finish_load_track(ev.data);
            break;

        case Q_AUDIO_HANDLE_FINISHED:
            /* some other type is buffered */
            LOGFQUEUE("audio < Q_AUDIO_HANDLE_FINISHED");
            audio_on_handle_finished(ev.data);
            break;

        /** Miscellaneous messages **/
        case Q_AUDIO_REMAKE_AUDIO_BUFFER:
            /* buffer needs to be reinitialized */
            LOGFQUEUE("audio < Q_AUDIO_REMAKE_AUDIO_BUFFER");
            audio_start_playback(0, AUDIO_START_RESTART | AUDIO_START_NEWBUF);
            break;

#ifdef HAVE_DISK_STORAGE
        case Q_AUDIO_UPDATE_WATERMARK:
            /* buffering watermark needs updating */
            LOGFQUEUE("audio < Q_AUDIO_UPDATE_WATERMARK: %d", (int)ev.data);
            audio_update_filebuf_watermark(ev.data);
            break;
#endif /* HAVE_DISK_STORAGE */

#ifdef AUDIO_HAVE_RECORDING
        case Q_AUDIO_LOAD_ENCODER:
            /* load an encoder for recording */
            LOGFQUEUE("audio < Q_AUDIO_LOAD_ENCODER: %d", (int)ev.data);
            audio_on_load_encoder(ev.data);
            break;
#endif /* AUDIO_HAVE_RECORDING */

#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
        case SYS_USB_CONNECTED:
            LOGFQUEUE("audio < SYS_USB_CONNECTED");
            audio_stop_playback();
#ifdef PLAYBACK_VOICE
            voice_stop();
#endif
            filling = STATE_USB;
            usb_acknowledge(SYS_USB_CONNECTED_ACK);
            break;
#endif /* (CONFIG_PLATFORM & PLATFORM_NATIVE) */

        case SYS_TIMEOUT:
            LOGFQUEUE_SYS_TIMEOUT("audio < SYS_TIMEOUT");
            break;

        default:
            /* LOGFQUEUE("audio < default : %08lX", ev.id); */
            break;
        } /* end switch */
    } /* end while */
}


/* --- Buffering callbacks --- */

/* Called when fullness is below the watermark level */
static void buffer_event_buffer_low_callback(void *data)
{
    logf("low buffer callback");
    LOGFQUEUE("buffering > audio Q_AUDIO_BUFFERING: buffer low");
    audio_queue_post(Q_AUDIO_BUFFERING, BUFFER_EVENT_BUFFER_LOW);
    (void)data;
}

/* Called when handles must be discarded in order to buffer new data */
static void buffer_event_rebuffer_callback(void *data)
{
    logf("rebuffer callback");
    LOGFQUEUE("buffering > audio Q_AUDIO_BUFFERING: rebuffer");
    audio_queue_post(Q_AUDIO_BUFFERING, BUFFER_EVENT_REBUFFER);
    (void)data;
}

/* A handle has completed buffering and all required data is available */
static void buffer_event_finished_callback(void *data)
{
    int hid = *(const int *)data;
    const enum data_type htype = buf_handle_data_type(hid);

    logf("handle %d finished buffering (type:%u)", hid, (unsigned)htype);

    /* Limit queue traffic */
    switch (htype)
    {
    case TYPE_ID3:
        /* The metadata handle for the last loaded track has been buffered.
           We can ask the audio thread to load the rest of the track's data. */
        LOGFQUEUE("buffering > audio Q_AUDIO_FINISH_LOAD_TRACK: %d", hid);
        audio_queue_post(Q_AUDIO_FINISH_LOAD_TRACK, hid);
        break;

    case TYPE_PACKET_AUDIO:
        /* Strip any useless trailing tags that are left. */
        strip_tags(hid);
        /* Fall-through */
    case TYPE_ATOMIC_AUDIO:
        LOGFQUEUE("buffering > audio Q_AUDIO_HANDLE_FINISHED: %d", hid);
        audio_queue_post(Q_AUDIO_HANDLE_FINISHED, hid);
        break;

    default:
        /* Don't care to know about these */
        break;
    }
}


/** -- Codec callbacks -- **/

/* Update elapsed time for next PCM insert */
void audio_codec_update_elapsed(unsigned long elapsed)
{
#ifdef AB_REPEAT_ENABLE
    ab_position_report(elapsed);
#endif
    /* Save in codec's id3 where it is used at next pcm insert */
    id3_get(CODEC_ID3)->elapsed = elapsed;
}

/* Update offset for next PCM insert */
void audio_codec_update_offset(size_t offset)
{
    /* Save in codec's id3 where it is used at next pcm insert */
    id3_get(CODEC_ID3)->offset = offset;
}

/* Codec has finished running */
void audio_codec_complete(int status)
{
#ifdef AB_REPEAT_ENABLE
    if (status >= CODEC_OK)
    {
        /* Normal automatic skip */
        ab_end_of_track_report();
    }
#endif

    LOGFQUEUE("codec > audio Q_AUDIO_CODEC_COMPLETE: %d", status);
    audio_queue_post(Q_AUDIO_CODEC_COMPLETE, status);
}

/* Codec has finished seeking */
void audio_codec_seek_complete(void)
{
    LOGFQUEUE("codec > audio Q_AUDIO_CODEC_SEEK_COMPLETE");
    audio_queue_post(Q_AUDIO_CODEC_SEEK_COMPLETE, 0);
}


/** --- Pcmbuf callbacks --- **/

/* Update the elapsed and offset from the information cached during the
   PCM buffer insert */
void audio_pcmbuf_position_callback(unsigned long elapsed, off_t offset,
                                    unsigned int key)
{
    if (key == position_key)
    {
        struct mp3entry *id3 = id3_get(PLAYING_ID3);
        id3->elapsed = elapsed;
        id3->offset = offset;
    }
}

/* Synchronize position info to the codec's */
void audio_pcmbuf_sync_position(void)
{
    audio_pcmbuf_position_callback(ci.id3->elapsed, ci.id3->offset,
                                   pcmbuf_get_position_key());
}

/* Post message from pcmbuf that the end of the previous track has just
 * been played */
void audio_pcmbuf_track_change(bool pcmbuf)
{
    if (pcmbuf)
    {
        /* Notify of the change in special-purpose semaphore object */
        LOGFQUEUE("pcmbuf > pcmbuf Q_AUDIO_TRACK_CHANGED");
        audio_pcmbuf_track_change_post();
    }
    else
    {
        /* Safe to post directly to the queue */
        LOGFQUEUE("pcmbuf > audio Q_AUDIO_TRACK_CHANGED");
        audio_queue_post(Q_AUDIO_TRACK_CHANGED, 0);
    }
}

/* May pcmbuf start PCM playback when the buffer is full enough? */
bool audio_pcmbuf_may_play(void)
{
    return play_status == PLAY_PLAYING && !ff_rw_mode;
}


/** -- External interfaces -- **/

/* Return the playback and recording status */
int audio_status(void)
{
    unsigned int ret = play_status;

#ifdef AUDIO_HAVE_RECORDING
    /* Do this here for constitency with mpeg.c version */
    ret |= pcm_rec_status();
#endif

    return (int)ret;
}

/* Clear all accumulated audio errors for playback and recording */
void audio_error_clear(void)
{
#ifdef AUDIO_HAVE_RECORDING
    pcm_rec_error_clear();
#endif
}

/* Get a copy of the id3 data for the for current track + offset + skip delta */
bool audio_peek_track(struct mp3entry *id3, int offset)
{
    bool retval = false;

    id3_mutex_lock();

    if (play_status != PLAY_STOPPED)
    {
        id3->path[0] = '\0'; /* Null path means it should be filled now */
        retval = audio_get_track_metadata(offset + skip_offset, id3) &&
                id3->path[0] != '\0';
    }

    id3_mutex_unlock();

    return retval;
}

/* Return the mp3entry for the currently playing track */
struct mp3entry * audio_current_track(void)
{
    struct mp3entry *id3;

    id3_mutex_lock();

#ifdef AUDIO_FAST_SKIP_PREVIEW
    if (skip_offset != 0)
    {
        /* This is a peekahead */
        id3 = id3_get(PLAYING_PEEK_ID3);
        audio_peek_track(id3, 0);
    }
    else
#endif
    {
        /* Normal case */
        id3 = id3_get(PLAYING_ID3);
        audio_get_track_metadata(0, id3);
    }

    id3_mutex_unlock();

    return id3;
}

/* Obtains the mp3entry for the next track from the current */
struct mp3entry * audio_next_track(void)
{
    struct mp3entry *id3 = id3_get(NEXTTRACK_ID3);

    id3_mutex_lock();

#ifdef AUDIO_FAST_SKIP_PREVIEW
    if (skip_offset != 0)
    {
        /* This is a peekahead */
        if (!audio_peek_track(id3, 1))
            id3 = NULL;
    }
    else
#endif
    {
        /* Normal case */
        if (!audio_get_track_metadata(1, id3))
            id3 = NULL;
    }

    id3_mutex_unlock();

    return id3;
}

/* Start playback at the specified offset */
void audio_play(long offset)
{
    logf("audio_play");

#ifdef PLAYBACK_VOICE
    /* Truncate any existing voice output so we don't have spelling
     * etc. over the first part of the played track */
    talk_force_shutup();
#endif

    LOGFQUEUE("audio >| audio Q_AUDIO_PLAY: %ld", offset);
    audio_queue_send(Q_AUDIO_PLAY, offset);
}

/* Stop playback if playing */
void audio_stop(void)
{
    LOGFQUEUE("audio >| audio Q_AUDIO_STOP");
    audio_queue_send(Q_AUDIO_STOP, 0);
}

/* Pause playback if playing */
void audio_pause(void)
{
    LOGFQUEUE("audio >| audio Q_AUDIO_PAUSE");
    audio_queue_send(Q_AUDIO_PAUSE, true);
}

/* This sends a stop message and the audio thread will dump all its
   subsequent messages */
void audio_hard_stop(void)
{
    /* Stop playback */
    LOGFQUEUE("audio >| audio Q_AUDIO_STOP: 1");
    audio_queue_send(Q_AUDIO_STOP, 1);
#ifdef PLAYBACK_VOICE
    voice_stop();
#endif
    if (audiobuf_handle > 0)
        audiobuf_handle = core_free(audiobuf_handle);
}

/* Resume playback if paused */
void audio_resume(void)
{
    LOGFQUEUE("audio >| audio Q_AUDIO_PAUSE resume");
    audio_queue_send(Q_AUDIO_PAUSE, false);
}

/* Skip the specified number of tracks forward or backward from the current */
void audio_skip(int offset)
{
    id3_mutex_lock();

    /* If offset has to be backed-out to stay in range, no skip is done */
    int accum = skip_offset + offset;

    while (offset != 0 && !playlist_check(accum))
    {
        offset += offset < 0 ? 1 : -1;
        accum = skip_offset + offset;
    }

    if (offset != 0)
    {
        /* Accumulate net manual skip count since the audio thread last
           processed one */
        skip_offset = accum;

        system_sound_play(SOUND_TRACK_SKIP);

        LOGFQUEUE("audio > audio Q_AUDIO_SKIP %d", offset);

#ifdef AUDIO_FAST_SKIP_PREVIEW
        /* Do this before posting so that the audio thread can correct us
           when things settle down - additionally, if audio gets a message
           and the delta is zero, the Q_AUDIO_SKIP handler (audio_on_skip)
           handler a skip event with the correct info but doesn't skip */
        send_event(PLAYBACK_EVENT_TRACK_SKIP, NULL);
#endif /* AUDIO_FAST_SKIP_PREVIEW */

        /* Playback only needs the final state even if more than one is
           processed because it wasn't removed in time */
        queue_remove_from_head(&audio_queue, Q_AUDIO_SKIP);
        audio_queue_post(Q_AUDIO_SKIP, 0);
    }
    else
    {
        /* No more tracks */
        system_sound_play(SOUND_TRACK_NO_MORE);
    }

    id3_mutex_unlock();
}

/* Skip one track forward from the current */
void audio_next(void)
{
    audio_skip(1);
}

/* Skip one track backward from the current */
void audio_prev(void)
{
    audio_skip(-1);
}

/* Move one directory forward */
void audio_next_dir(void)
{
    LOGFQUEUE("audio > audio Q_AUDIO_DIR_SKIP 1");
    audio_queue_post(Q_AUDIO_DIR_SKIP, 1);
}

/* Move one directory backward */
void audio_prev_dir(void)
{
    LOGFQUEUE("audio > audio Q_AUDIO_DIR_SKIP -1");
    audio_queue_post(Q_AUDIO_DIR_SKIP, -1);
}

/* Pause playback in order to start a seek that flushes the old audio */
void audio_pre_ff_rewind(void)
{
    LOGFQUEUE("audio > audio Q_AUDIO_PRE_FF_REWIND");
    audio_queue_post(Q_AUDIO_PRE_FF_REWIND, 0);
}

/* Seek to the new time in the current track */
void audio_ff_rewind(long time)
{
    LOGFQUEUE("audio > audio Q_AUDIO_FF_REWIND");
    audio_queue_post(Q_AUDIO_FF_REWIND, time);
}

/* Clear all but the currently playing track then rebuffer */
void audio_flush_and_reload_tracks(void)
{
    LOGFQUEUE("audio > audio Q_AUDIO_FLUSH");
    audio_queue_post(Q_AUDIO_FLUSH, 0);
}

/* Return the pointer to the main audio buffer, optionally preserving
   voicing */
unsigned char * audio_get_buffer(bool talk_buf, size_t *buffer_size)
{
    unsigned char *buf;

    if (audio_is_initialized)
    {
        audio_hard_stop();
    }
    /* else buffer_state will be AUDIOBUF_STATE_TRASHED at this point */

    if (buffer_size == NULL)
    {
        /* Special case for talk_init to use since it already knows it's
           trashed */
        buffer_state = AUDIOBUF_STATE_TRASHED;
        return NULL;
    }

    /* make sure buffer is freed and re-allocated to simplify code below
     * (audio_hard_stop() likely has done that already) */
    if (audiobuf_handle > 0)
        audiobuf_handle = core_free(audiobuf_handle);

    audiobuf_handle = core_alloc_maximum("audiobuf", &filebuflen, &ops);
    buf = core_get_data(audiobuf_handle);

    if (talk_buf || buffer_state == AUDIOBUF_STATE_TRASHED
           || !talk_voice_required())
    {
        logf("get buffer: talk, audio");
        /* Ok to use everything from audiobuf - voice is loaded,
           the talk buffer is not needed because voice isn't being used, or
           could be AUDIOBUF_STATE_TRASHED already. If state is
           AUDIOBUF_STATE_VOICED_ONLY, no problem as long as memory isn't
           written without the caller knowing what's going on. Changing certain
           settings may move it to a worse condition but the memory in use by
           something else will remain undisturbed.
         */
        if (buffer_state != AUDIOBUF_STATE_TRASHED)
        {
            talk_buffer_steal();
            buffer_state = AUDIOBUF_STATE_TRASHED;
        }
    }
    else
    {
        logf("get buffer: audio");
        /* Safe to just return this if already AUDIOBUF_STATE_VOICED_ONLY or
           still AUDIOBUF_STATE_INITIALIZED */
        /* Skip talk buffer and move pcm buffer to end to maximize available
           contiguous memory - no audio running means voice will not need the
           swap space */
        size_t talkbuf_size;
        buf += talkbuf_size = talkbuf_init(buf);
        filebuflen -= talkbuf_size;
        filebuflen -= voicebuf_init(buf + filebuflen);

        buffer_state = AUDIOBUF_STATE_VOICED_ONLY;
    }

    *buffer_size = filebuflen;
    return buf;
}

#ifdef HAVE_RECORDING
/* Stop audio, voice and obtain all available buffer space */
unsigned char * audio_get_recording_buffer(size_t *buffer_size)
{
    audio_hard_stop();
    return audio_get_buffer(true, buffer_size);
}
#endif /* HAVE_RECORDING */

/* Restore audio buffer to a particular state (one more valid than the current
   state) */
bool audio_restore_playback(int type)
{
    switch (type)
    {
    case AUDIO_WANT_PLAYBACK:
        if (buffer_state != AUDIOBUF_STATE_INITIALIZED)
            audio_reset_buffer();
        return true;
    case AUDIO_WANT_VOICE:
        if (buffer_state == AUDIOBUF_STATE_TRASHED)
            audio_reset_buffer();
        return true;
    default:
        return false;
    }
}

/* Has the playback buffer been completely claimed? */
bool audio_buffer_state_trashed(void)
{
    return buffer_state == AUDIOBUF_STATE_TRASHED;
}


/** --- Miscellaneous public interfaces --- **/

#ifdef HAVE_ALBUMART
/* Return which album art handle is current for the user in the given slot */
int playback_current_aa_hid(int slot)
{
    if ((unsigned)slot < MAX_MULTIPLE_AA)
    {
        struct track_info *info = track_list_user_current(skip_offset);

        if (!info && abs(skip_offset) <= 1)
        {
            /* Give the actual position a go */
            info = track_list_user_current(0);
        }

        if (info)
            return info->aa_hid[slot];
    }

    return ERR_HANDLE_NOT_FOUND;
}

/* Find an album art slot that doesn't match the dimensions of another that
   is already claimed - increment the use count if it is */
int playback_claim_aa_slot(struct dim *dim)
{
    int i;

    /* First try to find a slot already having the size to reuse it since we
       don't want albumart of the same size buffered multiple times */
    FOREACH_ALBUMART(i)
    {
        struct albumart_slot *slot = &albumart_slots[i];

        if (slot->dim.width == dim->width &&
            slot->dim.height == dim->height)
        {
            slot->used++;
            return i;
        }
    }

    /* Size is new, find a free slot */
    FOREACH_ALBUMART(i)
    {
        if (!albumart_slots[i].used)
        {
            albumart_slots[i].used++;
            albumart_slots[i].dim = *dim;
            return i;
        }
    }

    /* Sorry, no free slot */
    return -1;
}

/* Invalidate the albumart_slot - decrement the use count if > 0 */
void playback_release_aa_slot(int slot)
{
    if ((unsigned)slot < MAX_MULTIPLE_AA)
    {
        struct albumart_slot *aa_slot = &albumart_slots[slot];

        if (aa_slot->used > 0)
            aa_slot->used--;
    }
}
#endif /* HAVE_ALBUMART */


#ifdef HAVE_RECORDING
/* Load an encoder and run it */
bool audio_load_encoder(int afmt)
{
#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
    LOGFQUEUE("audio >| Q_AUDIO_LOAD_ENCODER: %d", afmt);
    return audio_queue_send(Q_AUDIO_LOAD_ENCODER, afmt) != 0;
#else
    (void)afmt;
    return true;
#endif
}

/* Stop an encoder and unload it */
void audio_remove_encoder(void)
{
#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
    LOGFQUEUE("audio >| Q_AUDIO_LOAD_ENCODER: NULL");
    audio_queue_send(Q_AUDIO_LOAD_ENCODER, AFMT_UNKNOWN);
#endif
}
#endif /* HAVE_RECORDING */

/* Is an automatic skip in progress? If called outside transition callbacks,
   indicates the last skip type at the time it was processed and isn't very
   meaningful. */
bool audio_automatic_skip(void)
{
    return automatic_skip;
}

/* Would normally calculate byte offset from an elapsed time but is not
   used on SWCODEC */
int audio_get_file_pos(void)
{
    return 0;
}

/* Return the elapsed time of the track previous to the current */
unsigned long audio_prev_elapsed(void)
{
    return prev_track_elapsed;
}

/* Return total file buffer length after accounting for the talk buf */
size_t audio_get_filebuflen(void)
{
    return buf_length();
}

/* How many tracks exist on the buffer - full or partial */
int audio_track_count(void)
    __attribute__((alias("track_list_count")));

/* Return total ringbuffer space occupied - ridx to widx */
long audio_filebufused(void)
{
    return buf_used();
}


/** -- Settings -- **/

/* Enable or disable cuesheet support and allocate/don't allocate the
   extra associated resources */
void audio_set_cuesheet(int enable)
{
    if (play_status == PLAY_STOPPED || !enable != !get_current_cuesheet())
    {
        LOGFQUEUE("audio >| audio Q_AUDIO_REMAKE_AUDIO_BUFFER");
        audio_queue_send(Q_AUDIO_REMAKE_AUDIO_BUFFER, 0);
    }
}

#ifdef HAVE_DISK_STORAGE
/* Set the audio antiskip buffer margin by index */
void audio_set_buffer_margin(int setting)
{
    static const unsigned short lookup[] =
        { 5, 15, 30, 60, 120, 180, 300, 600 };

    if ((unsigned)setting >= ARRAYLEN(lookup))
        setting = 0;

    logf("buffer margin: %u", (unsigned)lookup[setting]);

    LOGFQUEUE("audio > audio Q_AUDIO_UPDATE_WATERMARK: %u",
              (unsigned)lookup[setting]);
    audio_queue_post(Q_AUDIO_UPDATE_WATERMARK, lookup[setting]);
}
#endif /* HAVE_DISK_STORAGE */

#ifdef HAVE_CROSSFADE
/* Take necessary steps to enable or disable the crossfade setting */
void audio_set_crossfade(int enable)
{
    /* Tell it the next setting to use */
    pcmbuf_request_crossfade_enable(enable);

    /* Return if size hasn't changed or this is too early to determine
       which in the second case there's no way we could be playing
       anything at all */
    if (!pcmbuf_is_same_size())
    {
        LOGFQUEUE("audio >| audio Q_AUDIO_REMAKE_AUDIO_BUFFER");
        audio_queue_send(Q_AUDIO_REMAKE_AUDIO_BUFFER, 0);
    }
}
#endif /* HAVE_CROSSFADE */


/** -- Startup -- **/

/* Initialize the audio system - called from init() in main.c */
void audio_init(void)
{
    /* Can never do this twice */
    if (audio_is_initialized)
    {
        logf("audio: already initialized");
        return;
    }

    logf("audio: initializing");

    /* Initialize queues before giving control elsewhere in case it likes
       to send messages. Thread creation will be delayed however so nothing
       starts running until ready if something yields such as talk_init. */
    queue_init(&audio_queue, true);

    mutex_init(&id3_mutex);

    pcm_init();

    codec_thread_init();

    /* This thread does buffer, so match its priority */
    audio_thread_id = create_thread(audio_thread, audio_stack,
                  sizeof(audio_stack), 0, audio_thread_name
                  IF_PRIO(, MIN(PRIORITY_BUFFERING, PRIORITY_USER_INTERFACE))
                  IF_COP(, CPU));

    queue_enable_queue_send(&audio_queue, &audio_queue_sender_list,
                            audio_thread_id);

#ifdef PLAYBACK_VOICE
    voice_thread_init();
#endif

    /* audio_reset_buffer must know the size of voice buffer so init
       talk first */
    talk_init();

#ifdef HAVE_CROSSFADE
    /* Set crossfade setting for next buffer init which should be about... */
    pcmbuf_request_crossfade_enable(global_settings.crossfade);
#endif

    /* Initialize the buffering system */
    track_list_init();
    buffering_init();
    /* ...now! Set up the buffers */
    audio_reset_buffer();

    /* Probably safe to say */
    audio_is_initialized = true;

    sound_settings_apply();
#ifdef HAVE_DISK_STORAGE
    audio_set_buffer_margin(global_settings.buffer_margin);
#endif
}
