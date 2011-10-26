/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2002 Heikki Hannikainen
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
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "lcd.h"
#include "lang.h"
#include "menu.h"
#include "debug_menu.h"
#include "kernel.h"
#include "structec.h"
#include "action.h"
#include "debug.h"
#include "thread.h"
#include "powermgmt.h"
#include "system.h"
#include "font.h"
#include "audio.h"
#include "mp3_playback.h"
#include "settings.h"
#include "list.h"
#include "statusbar.h"
#include "dir.h"
#include "panic.h"
#include "screens.h"
#include "misc.h"
#include "splash.h"
#include "shortcuts.h"
#include "dircache.h"
#include "viewport.h"
#ifdef HAVE_TAGCACHE
#include "tagcache.h"
#endif
#include "lcd-remote.h"
#include "crc32.h"
#include "logf.h"
#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
#include "disk.h"
#include "adc.h"
#include "power.h"
#include "usb.h"
#include "rtc.h"
#include "storage.h"
#include "fat.h"
#include "eeprom_24cxx.h"
#if (CONFIG_STORAGE & STORAGE_MMC) || (CONFIG_STORAGE & STORAGE_SD)
#include "sdmmc.h"
#endif
#if (CONFIG_STORAGE & STORAGE_ATA)
#include "ata.h"
#endif
#if CONFIG_TUNER
#include "tuner.h"
#include "radio.h"
#endif
#endif

#ifdef HAVE_LCD_BITMAP
#include "scrollbar.h"
#include "peakmeter.h"
#endif
#include "logfdisp.h"
#include "core_alloc.h"
#if CONFIG_CODEC == SWCODEC
#include "pcmbuf.h"
#include "buffering.h"
#include "playback.h"
#if defined(HAVE_SPDIF_OUT) || defined(HAVE_SPDIF_IN)
#include "spdif.h"
#endif
#endif
#ifdef IRIVER_H300_SERIES
#include "pcf50606.h"   /* for pcf50606_read */
#endif
#ifdef IAUDIO_X5
#include "ds2411.h"
#endif
#include "hwcompat.h"
#include "button.h"
#if CONFIG_RTC == RTC_PCF50605
#include "pcf50605.h"
#endif
#include "appevents.h"
#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
#include "debug-target.h"
#endif

#if defined(SANSA_E200) || defined(SANSA_C200) || defined(PHILIPS_SA9200) \
      || (CONFIG_CPU == AS3525 && defined(CONFIG_CHARGING)) \
      || CONFIG_CPU == AS3525v2
#include "ascodec.h"
#include "as3514.h"
#endif

#ifdef IPOD_NANO2G
#include "pmu-target.h"
#endif

#ifdef HAVE_USBSTACK     
#include "usb_core.h"    
#endif

#if defined(IPOD_ACCESSORY_PROTOCOL)
#include "iap.h"
#endif

/*---------------------------------------------------*/
/*    SPECIAL DEBUG STUFF                            */
/*---------------------------------------------------*/
extern struct thread_entry threads[MAXTHREADS];

static char thread_status_char(unsigned status)
{
    static const char thread_status_chars[THREAD_NUM_STATES+1] =
    {
        [0 ... THREAD_NUM_STATES] = '?',
        [STATE_RUNNING]           = 'R',
        [STATE_BLOCKED]           = 'B',
        [STATE_SLEEPING]          = 'S',
        [STATE_BLOCKED_W_TMO]     = 'T',
        [STATE_FROZEN]            = 'F',
        [STATE_KILLED]            = 'K',
    };

    if (status > THREAD_NUM_STATES)
        status = THREAD_NUM_STATES;

    return thread_status_chars[status];
}

static const char* threads_getname(int selected_item, void *data,
                                   char *buffer, size_t buffer_len)
{
    (void)data;
    struct thread_entry *thread;
    char name[32];

#if NUM_CORES > 1
    if (selected_item < (int)NUM_CORES)
    {
        snprintf(buffer, buffer_len, "Idle (%d): %2d%%", selected_item,
                 idle_stack_usage(selected_item));
        return buffer;
    }

    selected_item -= NUM_CORES;
#endif

    thread = &threads[selected_item];

    if (thread->state == STATE_KILLED)
    {
        snprintf(buffer, buffer_len, "%2d: ---", selected_item);
        return buffer;
    }

    thread_get_name(name, 32, thread);

    snprintf(buffer, buffer_len,
             "%2d: " IF_COP("(%d) ") "%c%c " IF_PRIO("%d %d ") "%2d%% %s",
             selected_item,
             IF_COP(thread->core,)
#ifdef HAVE_SCHEDULER_BOOSTCTRL
             (thread->cpu_boost) ? '+' :
#endif
                 ((thread->state == STATE_RUNNING) ? '*' : ' '),
             thread_status_char(thread->state),
             IF_PRIO(thread->base_priority, thread->priority, )
             thread_stack_usage(thread), name);

    return buffer;
}

static int dbg_threads_action_callback(int action, struct gui_synclist *lists)
{
    (void)lists;
#ifdef ROCKBOX_HAS_LOGF
    if (action == ACTION_STD_OK)
    {
        int selpos = gui_synclist_get_sel_pos(lists);
#if NUM_CORES > 1
        if (selpos >= NUM_CORES)
            remove_thread(threads[selpos - NUM_CORES].id);
#else
        remove_thread(threads[selpos].id);
#endif
        return ACTION_REDRAW;
    }
#endif /* ROCKBOX_HAS_LOGF */
    if (action == ACTION_NONE)
        action = ACTION_REDRAW;
    return action;
}
/* Test code!!! */
static bool dbg_os(void)
{
    struct simplelist_info info;
    simplelist_info_init(&info, IF_COP("Core and ") "Stack usage:",
#if NUM_CORES == 1
                            MAXTHREADS,
#else
                            MAXTHREADS+NUM_CORES,
#endif
                            NULL);
#ifndef ROCKBOX_HAS_LOGF
    info.hide_selection = true;
    info.scroll_all = true;
#endif
    info.action_callback = dbg_threads_action_callback;
    info.get_name = threads_getname;
    return simplelist_show_list(&info);
}

#ifdef HAVE_LCD_BITMAP
#if CONFIG_CODEC != SWCODEC
#ifndef SIMULATOR
static bool dbg_audio_thread(void)
{
    struct audio_debug d;

    lcd_setfont(FONT_SYSFIXED);

    while(1)
    {
        if (action_userabort(HZ/5))
            return false;

        audio_get_debugdata(&d);

        lcd_clear_display();

        lcd_putsf(0, 0, "read: %x", d.audiobuf_read);
        lcd_putsf(0, 1, "write: %x", d.audiobuf_write);
        lcd_putsf(0, 2, "swap: %x", d.audiobuf_swapwrite);
        lcd_putsf(0, 3, "playing: %d", d.playing);
        lcd_putsf(0, 4, "playable: %x", d.playable_space);
        lcd_putsf(0, 5, "unswapped: %x", d.unswapped_space);

        /* Playable space left */
        gui_scrollbar_draw(&screens[SCREEN_MAIN],0, 6*8, 112, 4, d.audiobuflen, 0,
                  d.playable_space, HORIZONTAL);

        /* Show the watermark limit */
        gui_scrollbar_draw(&screens[SCREEN_MAIN],0, 6*8+4, 112, 4, d.audiobuflen, 0,
                  d.low_watermark_level, HORIZONTAL);

        lcd_putsf(0, 7, "wm: %x - %x",
                    d.low_watermark_level, d.lowest_watermark_level);

        lcd_update();
    }
    lcd_setfont(FONT_UI);
    return false;
}
#endif /* !SIMULATOR */
#else /* CONFIG_CODEC == SWCODEC */
static unsigned int ticks, freq_sum;
#ifndef CPU_MULTI_FREQUENCY
static unsigned int boost_ticks;
#endif

static void dbg_audio_task(void)
{
#ifdef CPUFREQ_NORMAL
#ifndef CPU_MULTI_FREQUENCY
    if(FREQ > CPUFREQ_NORMAL)
        boost_ticks++;
#endif
    freq_sum += FREQ/1000000; /* in MHz */
#endif
    ticks++;
}

static bool dbg_buffering_thread(void)
{
    int button;
    int line;    
    bool done = false;
    size_t bufused;
    size_t bufsize = pcmbuf_get_bufsize();
    int pcmbufdescs = pcmbuf_descs();
    struct buffering_debug d;
    size_t filebuflen = audio_get_filebuflen();
    /* This is a size_t, but call it a long so it puts a - when it's bad. */

#ifndef CPU_MULTI_FREQUENCY
    boost_ticks = 0;
#endif
    ticks = freq_sum = 0;

    tick_add_task(dbg_audio_task);
    
    FOR_NB_SCREENS(i)
        screens[i].setfont(FONT_SYSFIXED);
        
    while(!done)
    {
        button = get_action(CONTEXT_STD,HZ/5);
        switch(button)
        {
            case ACTION_STD_NEXT:
                audio_next();
                break;
            case ACTION_STD_PREV:
                audio_prev();
                break;
            case ACTION_STD_CANCEL:
                done = true;
                break;
        }

        buffering_get_debugdata(&d);
        bufused = bufsize - pcmbuf_free();

        FOR_NB_SCREENS(i) 
        {
            line = 0;
            screens[i].clear_display();


            screens[i].putsf(0, line++, "pcm: %6ld/%ld", (long) bufused, (long) bufsize);

            gui_scrollbar_draw(&screens[i],0, line*8, screens[i].lcdwidth, 6,
                               bufsize, 0, bufused, HORIZONTAL);
            line++;

            screens[i].putsf(0, line++, "alloc: %6ld/%ld", audio_filebufused(),
                            (long) filebuflen);

#if LCD_HEIGHT > 80 || (defined(HAVE_REMOTE_LCD) && LCD_REMOTE_HEIGHT > 80)
            if (screens[i].lcdheight > 80)
            {
                gui_scrollbar_draw(&screens[i],0, line*8, screens[i].lcdwidth, 6,
                                   filebuflen, 0, audio_filebufused(), HORIZONTAL);
                line++;

                screens[i].putsf(0, line++, "real:  %6ld/%ld", (long)d.buffered_data,
                                (long)filebuflen);

                gui_scrollbar_draw(&screens[i],0, line*8, screens[i].lcdwidth, 6,
                                   filebuflen, 0, (long)d.buffered_data, HORIZONTAL);
                line++;
            }
#endif

            screens[i].putsf(0, line++, "usefl: %6ld/%ld", (long)(d.useful_data),
                                                       (long)filebuflen);

#if LCD_HEIGHT > 80 || (defined(HAVE_REMOTE_LCD) && LCD_REMOTE_HEIGHT > 80)
            if (screens[i].lcdheight > 80)
            {
                gui_scrollbar_draw(&screens[i],0, line*8, screens[i].lcdwidth, 6,
                                   filebuflen, 0, d.useful_data, HORIZONTAL);
                line++;
            }
#endif

            screens[i].putsf(0, line++, "data_rem: %ld", (long)d.data_rem);

            screens[i].putsf(0, line++, "track count: %2d", audio_track_count());

            screens[i].putsf(0, line++, "handle count: %d", (int)d.num_handles);

#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
            screens[i].putsf(0, line++, "cpu freq: %3dMHz",
                             (int)((FREQ + 500000) / 1000000));
#endif

            if (ticks > 0)
            {
                int avgclock   = freq_sum * 10 / ticks;      /* in 100 kHz */
#ifdef CPU_MULTI_FREQUENCY
                int boostquota = (avgclock * 100 - CPUFREQ_NORMAL/1000) /
                    ((CPUFREQ_MAX - CPUFREQ_NORMAL) / 1000000); /* in 0.1 % */
#else
                int boostquota = boost_ticks * 1000 / ticks; /* in 0.1 % */
#endif
                screens[i].putsf(0, line++, "boost:%3d.%d%% (%d.%dMHz)",
                                 boostquota/10, boostquota%10, avgclock/10, avgclock%10);
            }

            screens[i].putsf(0, line++, "pcmbufdesc: %2d/%2d",
                             pcmbuf_used_descs(), pcmbufdescs);
            screens[i].putsf(0, line++, "watermark: %6d",
                             (int)(d.watermark));

            screens[i].update();
        }
    }

    tick_remove_task(dbg_audio_task);
    
    FOR_NB_SCREENS(i)
        screens[i].setfont(FONT_UI);

    return false;
}
#endif /* CONFIG_CODEC */
#endif /* HAVE_LCD_BITMAP */

static const char* bf_getname(int selected_item, void *data,
                                   char *buffer, size_t buffer_len)
{
    (void)data;
    core_print_block_at(selected_item, buffer, buffer_len);
    return buffer;
}

static int bf_action_cb(int action, struct gui_synclist* list)
{
    if (action == ACTION_STD_OK)
    {
        if (gui_synclist_get_sel_pos(list) == 0 && core_test_free())
        {
            splash(HZ, "Freed test handle. New alloc should trigger compact");
        }
        else
        {
            splash(HZ/1, "Attempting a 64k allocation");
            int handle = core_alloc("test", 64<<10);
            splash(HZ/2, (handle > 0) ? "Success":"Fail");
            /* for some reason simplelist doesn't allow adding items here if
             * info.get_name is given, so use normal list api */
            gui_synclist_set_nb_items(list, core_get_num_blocks());
            if (handle > 0)
                core_free(handle);
        }
        action = ACTION_REDRAW;
    }
    else if (action == ACTION_NONE)
        action = ACTION_REDRAW;
    return action;
}

static bool dbg_buflib_allocs(void)
{
    struct simplelist_info info;
    simplelist_info_init(&info, "mem allocs", core_get_num_blocks(), NULL);
    info.get_name = bf_getname;
    info.action_callback = bf_action_cb;
    info.timeout = TIMEOUT_BLOCK;
    return simplelist_show_list(&info);
}

#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
static const char* dbg_partitions_getname(int selected_item, void *data,
                                          char *buffer, size_t buffer_len)
{
    (void)data;
    int partition = selected_item/2;
    struct partinfo* p = disk_partinfo(partition);
    if (selected_item%2)
    {
        snprintf(buffer, buffer_len, "   T:%x %ld MB", p->type, p->size / ( 2048 / ( SECTOR_SIZE / 512 )));
    }
    else
    {
        snprintf(buffer, buffer_len, "P%d: S:%lx", partition, p->start);
    }
    return buffer;
}

bool dbg_partitions(void)
{
    struct simplelist_info info;
    simplelist_info_init(&info, "Partition Info", 4, NULL);
    info.selection_size = 2;
    info.hide_selection = true;
    info.scroll_all = true;
    info.get_name = dbg_partitions_getname;
    return simplelist_show_list(&info);
}
#endif /* PLATFORM_NATIVE */

#if defined(CPU_COLDFIRE) && defined(HAVE_SPDIF_OUT)
static bool dbg_spdif(void)
{
    int line;
    unsigned int control;
    int x;
    char *s;
    int category;
    int generation;
    unsigned int interruptstat;
    bool valnogood, symbolerr, parityerr;
    bool done = false;
    bool spdif_src_on;
    int spdif_source = spdif_get_output_source(&spdif_src_on);
    spdif_set_output_source(AUDIO_SRC_SPDIF IF_SPDIF_POWER_(, true));

    lcd_clear_display();
    lcd_setfont(FONT_SYSFIXED);

#ifdef HAVE_SPDIF_POWER
    spdif_power_enable(true); /* We need SPDIF power for both sending & receiving */
#endif

    while (!done)
    {
        line = 0;

        control = EBU1RCVCCHANNEL1;
        interruptstat = INTERRUPTSTAT;
        INTERRUPTCLEAR = 0x03c00000;

        valnogood = (interruptstat & 0x01000000)?true:false;
        symbolerr = (interruptstat & 0x00800000)?true:false;
        parityerr = (interruptstat & 0x00400000)?true:false;

        lcd_putsf(0, line++, "Val: %s Sym: %s Par: %s",
                 valnogood?"--":"OK",
                 symbolerr?"--":"OK",
                 parityerr?"--":"OK");

        lcd_putsf(0, line++, "Status word: %08x", (int)control);

        line++;

        x = control >> 31;
        lcd_putsf(0, line++, "PRO: %d (%s)",
                 x, x?"Professional":"Consumer");

        x = (control >> 30) & 1;
        lcd_putsf(0, line++, "Audio: %d (%s)",
                 x, x?"Non-PCM":"PCM");

        x = (control >> 29) & 1;
        lcd_putsf(0, line++, "Copy: %d (%s)",
                 x, x?"Permitted":"Inhibited");

        x = (control >> 27) & 7;
        switch(x)
        {
        case 0:
            s = "None";
            break;
        case 1:
            s = "50/15us";
            break;
        default:
            s = "Reserved";
            break;
        }
        lcd_putsf(0, line++, "Preemphasis: %d (%s)", x, s);

        x = (control >> 24) & 3;
        lcd_putsf(0, line++, "Mode: %d", x);

        category = (control >> 17) & 127;
        switch(category)
        {
        case 0x00:
            s = "General";
            break;
        case 0x40:
            s = "Audio CD";
            break;
        default:
            s = "Unknown";
        }
        lcd_putsf(0, line++, "Category: 0x%02x (%s)", category, s);

        x = (control >> 16) & 1;
        generation = x;
        if(((category & 0x70) == 0x10) ||
           ((category & 0x70) == 0x40) ||
           ((category & 0x78) == 0x38))
        {
            generation = !generation;
        }
        lcd_putsf(0, line++, "Generation: %d (%s)",
                 x, generation?"Original":"No ind.");

        x = (control >> 12) & 15;
        lcd_putsf(0, line++, "Source: %d", x);


        x = (control >> 8) & 15;
        switch(x)
        {
        case 0:
            s = "Unspecified";
            break;
        case 8:
            s = "A (Left)";
            break;
        case 4:
            s = "B (Right)";
            break;
        default:
            s = "";
            break;
        }
        lcd_putsf(0, line++, "Channel: %d (%s)", x, s);

        x = (control >> 4) & 15;
        switch(x)
        {
        case 0:
            s = "44.1kHz";
            break;
        case 0x4:
            s = "48kHz";
            break;
        case 0xc:
            s = "32kHz";
            break;
        }
        lcd_putsf(0, line++, "Frequency: %d (%s)", x, s);

        x = (control >> 2) & 3;
        lcd_putsf(0, line++, "Clock accuracy: %d", x);
        line++;

#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
        lcd_putsf(0, line++, "Measured freq: %ldHz",
                 spdif_measure_frequency());
#endif

        lcd_update();

        if (action_userabort(HZ/10))
            break;
    }

    spdif_set_output_source(spdif_source IF_SPDIF_POWER_(, spdif_src_on));

#ifdef HAVE_SPDIF_POWER
    spdif_power_enable(global_settings.spdif_enable);
#endif

    lcd_setfont(FONT_UI);
    return false;
}
#endif /* CPU_COLDFIRE */

#if (CONFIG_RTC == RTC_PCF50605) && (CONFIG_PLATFORM & PLATFORM_NATIVE)
static bool dbg_pcf(void)
{
    int line;

#ifdef HAVE_LCD_BITMAP
    lcd_setfont(FONT_SYSFIXED);
#endif
    lcd_clear_display();

    while(1)
    {
        line = 0;

        lcd_putsf(0, line++, "DCDC1:  %02x", pcf50605_read(0x1b));
        lcd_putsf(0, line++, "DCDC2:  %02x", pcf50605_read(0x1c));
        lcd_putsf(0, line++, "DCDC3:  %02x", pcf50605_read(0x1d));
        lcd_putsf(0, line++, "DCDC4:  %02x", pcf50605_read(0x1e));
        lcd_putsf(0, line++, "DCDEC1: %02x", pcf50605_read(0x1f));
        lcd_putsf(0, line++, "DCDEC2: %02x", pcf50605_read(0x20));
        lcd_putsf(0, line++, "DCUDC1: %02x", pcf50605_read(0x21));
        lcd_putsf(0, line++, "DCUDC2: %02x", pcf50605_read(0x22));
        lcd_putsf(0, line++, "IOREGC: %02x", pcf50605_read(0x23));
        lcd_putsf(0, line++, "D1REGC: %02x", pcf50605_read(0x24));
        lcd_putsf(0, line++, "D2REGC: %02x", pcf50605_read(0x25));
        lcd_putsf(0, line++, "D3REGC: %02x", pcf50605_read(0x26));
        lcd_putsf(0, line++, "LPREG1: %02x", pcf50605_read(0x27));
        lcd_update();
        if (button_get_w_tmo(HZ/10) == (DEBUG_CANCEL|BUTTON_REL))
        {
            lcd_setfont(FONT_UI);
            return false;
        }
    }

    lcd_setfont(FONT_UI);
    return false;
}
#endif

#ifdef HAVE_ADJUSTABLE_CPU_FREQ
static bool dbg_cpufreq(void)
{
    int line;
    int button;

#ifdef HAVE_LCD_BITMAP
    lcd_setfont(FONT_SYSFIXED);
#endif
    lcd_clear_display();

    while(1)
    {
        line = 0;

        lcd_putsf(0, line++, "Frequency: %ld", FREQ);
        lcd_putsf(0, line++, "boost_counter: %d", get_cpu_boost_counter());

        lcd_update();
        button = get_action(CONTEXT_STD,HZ/10);

        switch(button)
        {
            case ACTION_STD_PREV:
                cpu_boost(true);
                break;

            case ACTION_STD_NEXT:
                cpu_boost(false);
                break;

            case ACTION_STD_OK:
                while (get_cpu_boost_counter() > 0)
                    cpu_boost(false);
                set_cpu_frequency(CPUFREQ_DEFAULT);
                break;

            case ACTION_STD_CANCEL:
                lcd_setfont(FONT_UI);
                return false;
        }
    }
    lcd_setfont(FONT_UI);
    return false;
}
#endif /* HAVE_ADJUSTABLE_CPU_FREQ */

#if defined(HAVE_TSC2100) && (CONFIG_PLATFORM & PLATFORM_NATIVE)
#include "tsc2100.h"
static const char* tsc2100_debug_getname(int selected_item, void * data,
                                         char *buffer, size_t buffer_len)
{
    int *page = (int*)data;
    bool reserved = false;
    switch (*page)
    {
        case 0:
            if ((selected_item > 0x0a)  ||
                (selected_item == 0x04) ||
                (selected_item == 0x08))
                reserved = true;
            break;
        case 1:
            if ((selected_item > 0x05) ||
                (selected_item == 0x02))
                reserved = true;
            break;
        case 2:
            if (selected_item > 0x1e)
                reserved = true;
            break;
    }
    if (reserved)
        snprintf(buffer, buffer_len, "%02x: RSVD", selected_item);
    else
        snprintf(buffer, buffer_len, "%02x: %04x", selected_item,
                    tsc2100_readreg(*page, selected_item)&0xffff);
    return buffer;
}
static int tsc2100debug_action_callback(int action, struct gui_synclist *lists)
{
    int *page = (int*)lists->data;
    if (action == ACTION_STD_OK)
    {
        *page = (*page+1)%3;
        snprintf(lists->title, 32,
                 "tsc2100 registers - Page %d", *page);
        return ACTION_REDRAW;
    }
    return action;
}
static bool tsc2100_debug(void)
{
    int page = 0;
    char title[32] = "tsc2100 registers - Page 0";
    struct simplelist_info info;
    simplelist_info_init(&info, title, 32, &page);
    info.timeout = HZ/100;
    info.get_name = tsc2100_debug_getname;
    info.action_callback= tsc2100debug_action_callback;
    return simplelist_show_list(&info);
}
#endif
#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
#ifdef HAVE_LCD_BITMAP
/*
 * view_battery() shows a automatically scaled graph of the battery voltage
 * over time. Usable for estimating battery life / charging rate.
 * The power_history array is updated in power_thread of powermgmt.c.
 */

#define BAT_LAST_VAL  MIN(LCD_WIDTH, POWER_HISTORY_LEN)
#define BAT_YSPACE    (LCD_HEIGHT - 20)


static bool view_battery(void)
{
    int view = 0;
    int i, x, y, y1, y2, grid, graph;
    unsigned short maxv, minv;

    lcd_setfont(FONT_SYSFIXED);

    while(1)
    {
        lcd_clear_display();
        switch (view) {
            case 0: /* voltage history graph */
                /* Find maximum and minimum voltage for scaling */
                minv = power_history[0];
                maxv = minv + 1;
                for (i = 1; i < BAT_LAST_VAL && power_history[i]; i++) {
                    if (power_history[i] > maxv)
                        maxv = power_history[i];
                    if (power_history[i] < minv)
                        minv = power_history[i];
                }
                
                /* adjust grid scale */ 
                if ((maxv - minv) > 50)
                    grid = 50;
                else
                    grid = 5;
                
                /* print header */                
                lcd_putsf(0, 0, "battery %d.%03dV", power_history[0] / 1000,
                         power_history[0] % 1000);
                lcd_putsf(0, 1, "%d.%03d-%d.%03dV (%2dmV)",
                          minv / 1000, minv % 1000, maxv / 1000, maxv % 1000,
                          grid);
                
                i = 1;
                while ((y = (minv - (minv % grid)+i*grid)) < maxv)
                {
                    graph = ((y-minv)*BAT_YSPACE)/(maxv-minv);
                    graph = LCD_HEIGHT-1 - graph;
             
                    /* draw dotted horizontal grid line */      
                    for (x=0; x<LCD_WIDTH;x=x+2)
                        lcd_drawpixel(x,graph);

                    i++;
                }
                
                x = 0;
                /* draw plot of power history
                 * skip empty entries
                 */
                for (i = BAT_LAST_VAL - 1; i > 0; i--) 
                {
                    if (power_history[i] && power_history[i-1])
                    {
                        y1 = (power_history[i] - minv) * BAT_YSPACE / 
                            (maxv - minv);
                        y1 = MIN(MAX(LCD_HEIGHT-1 - y1, 20), 
                                 LCD_HEIGHT-1);
                        y2 = (power_history[i-1] - minv) * BAT_YSPACE /
                            (maxv - minv);
                        y2 = MIN(MAX(LCD_HEIGHT-1 - y2, 20),
                                 LCD_HEIGHT-1);

                        lcd_set_drawmode(DRMODE_SOLID);

                        /* make line thicker */
                        lcd_drawline(((x*LCD_WIDTH)/(BAT_LAST_VAL)), 
                                     y1, 
                                     (((x+1)*LCD_WIDTH)/(BAT_LAST_VAL)), 
                                     y2);
                        lcd_drawline(((x*LCD_WIDTH)/(BAT_LAST_VAL))+1, 
                                     y1+1, 
                                     (((x+1)*LCD_WIDTH)/(BAT_LAST_VAL))+1, 
                                     y2+1);
                        x++;
                    }
                }
                break;

            case 1: /* status: */
#if CONFIG_CHARGING >= CHARGING_MONITOR
                lcd_putsf(0, 0, "Pwr status: %s",
                         charging_state() ? "charging" : "discharging");
#else 
                lcd_puts(0, 0, "Power status:");
#endif
                battery_read_info(&y, NULL);
                lcd_putsf(0, 1, "Battery: %d.%03d V", y / 1000, y % 1000);
#ifdef ADC_EXT_POWER
                y = (adc_read(ADC_EXT_POWER) * EXT_SCALE_FACTOR) / 1000;
                lcd_putsf(0, 2, "External: %d.%03d V", y / 1000, y % 1000);
#endif
#if CONFIG_CHARGING
#if defined ARCHOS_RECORDER
                lcd_putsf(0, 3, "Chgr: %s %s",
                         charger_inserted() ? "present" : "absent",
                         charger_enabled() ? "on" : "off");
                lcd_putsf(0, 5, "short delta: %d", short_delta);
                lcd_putsf(0, 6, "long delta: %d", long_delta);
                lcd_puts(0, 7, power_message);
                lcd_putsf(0, 8, "USB Inserted: %s",
                         usb_inserted() ? "yes" : "no");
#elif defined IPOD_NANO || defined IPOD_VIDEO
                int usb_pwr  = (GPIOL_INPUT_VAL & 0x10)?true:false;
                int ext_pwr  = (GPIOL_INPUT_VAL & 0x08)?false:true;
                int dock     = (GPIOA_INPUT_VAL & 0x10)?true:false;
                int charging = (GPIOB_INPUT_VAL & 0x01)?false:true;
                int headphone= (GPIOA_INPUT_VAL & 0x80)?true:false;

                lcd_putsf(0, 3, "USB pwr:   %s",
                            usb_pwr ? "present" : "absent");
                lcd_putsf(0, 4, "EXT pwr:   %s",
                            ext_pwr ? "present" : "absent");
                lcd_putsf(0, 5, "Battery:   %s",
                    charging ? "charging" : (usb_pwr||ext_pwr) ? "charged" : "discharging");
                lcd_putsf(0, 6, "Dock mode: %s",
                         dock    ? "enabled" : "disabled");
                lcd_putsf(0, 7, "Headphone: %s",
                         headphone ? "connected" : "disconnected");
#ifdef IPOD_VIDEO
                if(probed_ramsize == 64)
                    x = (adc_read(ADC_4066_ISTAT) * 2400) / (1024 * 2);
                else
#endif
                    x = (adc_read(ADC_4066_ISTAT) * 2400) / (1024 * 3);
                lcd_putsf(0, 8, "Ibat: %d mA", x);
                lcd_putsf(0, 9, "Vbat * Ibat: %d mW", x * y / 1000);
#elif defined TOSHIBA_GIGABEAT_S
                int line = 3;
                unsigned int st;

                static const unsigned char * const chrgstate_strings[] =
                {
                    "Disabled",
                    "Error",
                    "Discharging",
                    "Precharge",
                    "Constant Voltage",
                    "Constant Current",
                    "<unknown>",
                };

                lcd_putsf(0, line++, "Charger: %s",
                         charger_inserted() ? "present" : "absent");

                st = power_input_status() &
                     (POWER_INPUT_CHARGER | POWER_INPUT_BATTERY);
                lcd_putsf(0, line++, "%s%s",
                         (st & POWER_INPUT_MAIN_CHARGER) ? " Main" : "",
                         (st & POWER_INPUT_USB_CHARGER) ? " USB" : "");

                y = ARRAYLEN(chrgstate_strings) - 1;

                switch (charge_state)
                {
                case CHARGE_STATE_DISABLED: y--;
                case CHARGE_STATE_ERROR:    y--;
                case DISCHARGING:           y--;
                case TRICKLE:               y--;
                case TOPOFF:                y--;
                case CHARGING:              y--;
                default:;
                }

                lcd_putsf(0, line++, "State: %s", chrgstate_strings[y]);

                lcd_putsf(0, line++, "Battery Switch: %s",
                         (st & POWER_INPUT_BATTERY) ? "On" : "Off");

                y = chrgraw_adc_voltage();
                lcd_putsf(0, line++, "CHRGRAW: %d.%03d V",
                         y / 1000, y % 1000);

                y = application_supply_adc_voltage();
                lcd_putsf(0, line++, "BP     : %d.%03d V",
                         y / 1000, y % 1000);

                y = battery_adc_charge_current();
                if (y < 0) x = '-', y = -y;
                else       x = ' ';
                lcd_putsf(0, line++, "CHRGISN:%c%d mA", x, y);

                y = cccv_regulator_dissipation();
                lcd_putsf(0, line++, "P CCCV : %d mW", y);

                y = battery_charge_current();
                if (y < 0) x = '-', y = -y;
                else       x = ' ';
                lcd_putsf(0, line++, "I Charge:%c%d mA", x, y);

                y = battery_adc_temp();

                if (y != INT_MIN) {
                    lcd_putsf(0, line++, "T Battery: %dC (%dF)", y,
                               (9*y + 160) / 5);
                } else {
                    /* Conversion disabled */
                    lcd_puts(0, line++, "T Battery: ?");
                }
                    
#elif defined(SANSA_E200) || defined(SANSA_C200) || CONFIG_CPU == AS3525 || \
      CONFIG_CPU == AS3525v2
                static const char * const chrgstate_strings[] =
                {
                    [CHARGE_STATE_DISABLED - CHARGE_STATE_DISABLED]= "Disabled",
                    [CHARGE_STATE_ERROR - CHARGE_STATE_DISABLED] = "Error",
                    [DISCHARGING - CHARGE_STATE_DISABLED]       = "Discharging",
                    [CHARGING - CHARGE_STATE_DISABLED]          = "Charging",
                };
                const char *str = NULL;

                lcd_putsf(0, 3, "Charger: %s",
                         charger_inserted() ? "present" : "absent");

                y = charge_state - CHARGE_STATE_DISABLED;
                if ((unsigned)y < ARRAYLEN(chrgstate_strings))
                    str = chrgstate_strings[y];

                lcd_putsf(0, 4, "State: %s",
                         str ? str : "<unknown>");

                lcd_putsf(0, 5, "CHARGER: %02X", ascodec_read_charger());
#elif defined(IPOD_NANO2G)
                y = pmu_read_battery_voltage();
                lcd_putsf(17, 1, "RAW: %d.%03d V", y / 1000, y % 1000);
                y = pmu_read_battery_current();
                lcd_putsf(0, 2, "Battery current: %d mA", y);
                lcd_putsf(0, 3, "PWRCON: %08x %08x", PWRCON, PWRCONEXT);
                lcd_putsf(0, 4, "CLKCON: %08x %03x %03x", CLKCON, CLKCON2, CLKCON3);
                lcd_putsf(0, 5, "PLL: %06x %06x %06x", PLL0PMS, PLL1PMS, PLL2PMS);
                x = pmu_read(0x1b) & 0xf;
                y = pmu_read(0x1a) * 25 + 625;
                lcd_putsf(0, 6, "AUTO: %x / %d mV", x, y);
                x = pmu_read(0x1f) & 0xf;
                y = pmu_read(0x1e) * 25 + 625;
                lcd_putsf(0, 7, "DOWN1: %x / %d mV", x, y);
                x = pmu_read(0x23) & 0xf;
                y = pmu_read(0x22) * 25 + 625;
                lcd_putsf(0, 8, "DOWN2: %x / %d mV", x, y);
                x = pmu_read(0x27) & 0xf;
                y = pmu_read(0x26) * 100 + 900;
                lcd_putsf(0, 9, "MEMLDO: %x / %d mV", x, y);
                for (i = 0; i < 6; i++)
                {
                    x = pmu_read(0x2e + (i << 1)) & 0xf;
                    y = pmu_read(0x2d + (i << 1)) * 100 + 900;
                    lcd_putsf(0, 10 + i, "LDO%d: %x / %d mV", i + 1, x, y);
                }
#else
                lcd_putsf(0, 3, "Charger: %s",
                         charger_inserted() ? "present" : "absent");
#endif /* target type */
#endif /* CONFIG_CHARGING */
                break;

            case 2: /* voltage deltas: */
                lcd_puts(0, 0, "Voltage deltas:");

                for (i = 0; i <= 6; i++) {
                    y = power_history[i] - power_history[i+1];
                    lcd_putsf(0, i+1, "-%d min: %s%d.%03d V", i,
                             (y < 0) ? "-" : "", ((y < 0) ? y * -1 : y) / 1000,
                             ((y < 0) ? y * -1 : y ) % 1000);
                }
                break;

            case 3: /* remaining time estimation: */

#ifdef ARCHOS_RECORDER
                lcd_putsf(0, 0, "charge_state: %d", charge_state);

                lcd_putsf(0, 1, "Cycle time: %d m", powermgmt_last_cycle_startstop_min);

                lcd_putsf(0, 2, "Lvl@cyc st: %d%%", powermgmt_last_cycle_level);

                lcd_putsf(0, 3, "P=%2d I=%2d", pid_p, pid_i);

                lcd_putsf(0, 4, "Trickle sec: %d/60", trickle_sec);
#endif /* ARCHOS_RECORDER */

                lcd_putsf(0, 5, "Last PwrHist: %d.%03dV",
                    power_history[0] / 1000,
                    power_history[0] % 1000);

                lcd_putsf(0, 6, "battery level: %d%%", battery_level());

                lcd_putsf(0, 7, "Est. remain: %d m", battery_time());
                break;
        }

        lcd_update();

        switch(get_action(CONTEXT_STD,HZ/2))
        {
            case ACTION_STD_PREV:
                if (view)
                    view--;
                break;

            case ACTION_STD_NEXT:
                if (view < 3)
                    view++;
                break;

            case ACTION_STD_CANCEL:
                lcd_setfont(FONT_UI);
                return false;
        }
    }
    lcd_setfont(FONT_UI);
    return false;
}

#endif /* HAVE_LCD_BITMAP */
#endif

#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
#if (CONFIG_STORAGE & STORAGE_MMC) || (CONFIG_STORAGE & STORAGE_SD)

#if (CONFIG_STORAGE & STORAGE_MMC)
#define CARDTYPE "MMC"
#elif (CONFIG_STORAGE & STORAGE_SD)
#define CARDTYPE "microSD"
#endif

static int disk_callback(int btn, struct gui_synclist *lists)
{
    tCardInfo *card;
    int *cardnum = (int*)lists->data;
    unsigned char card_name[6];
    unsigned char pbuf[32];
    char *title = lists->title;
    static const unsigned char i_vmin[] = { 0, 1, 5, 10, 25, 35, 60, 100 };
    static const unsigned char i_vmax[] = { 1, 5, 10, 25, 35, 45, 80, 200 };
    static const unsigned char * const kbit_units[] = { "kBit/s", "MBit/s", "GBit/s" };
    static const unsigned char * const nsec_units[] = { "ns", "µs", "ms" };
#if (CONFIG_STORAGE & STORAGE_MMC)
    static const char * const mmc_spec_vers[] = { "1.0-1.2", "1.4", "2.0-2.2",
        "3.1-3.31", "4.0" };
#endif

    if ((btn == ACTION_STD_OK) || (btn == SYS_FS_CHANGED) || (btn == ACTION_REDRAW))
    {
#ifdef HAVE_HOTSWAP
        if (btn == ACTION_STD_OK)
        {
            *cardnum ^= 0x1; /* change cards */
        }
#endif

        simplelist_set_line_count(0);

        card = card_get_info(*cardnum);

        if (card->initialized > 0)
        {
            unsigned i;
            for (i=0; i<sizeof(card_name); i++)
            {
                card_name[i] = card_extract_bits(card->cid, (103-8*i), 8);
            }
            strlcpy(card_name, card_name, sizeof(card_name));
            simplelist_addline(SIMPLELIST_ADD_LINE,
                    "%s Rev %d.%d", card_name,
                    (int) card_extract_bits(card->cid, 63, 4),
                    (int) card_extract_bits(card->cid, 59, 4));
            simplelist_addline(SIMPLELIST_ADD_LINE,
                    "Prod: %d/%d",
#if (CONFIG_STORAGE & STORAGE_SD)
                    (int) card_extract_bits(card->cid, 11, 4),
                    (int) card_extract_bits(card->cid, 19, 8) + 2000
#elif (CONFIG_STORAGE & STORAGE_MMC)
                    (int) card_extract_bits(card->cid, 15, 4),
                    (int) card_extract_bits(card->cid, 11, 4) + 1997
#endif
                    );
            simplelist_addline(SIMPLELIST_ADD_LINE,
#if (CONFIG_STORAGE & STORAGE_SD)
                    "Ser#: 0x%08lx",
                    card_extract_bits(card->cid, 55, 32)
#elif (CONFIG_STORAGE & STORAGE_MMC)
                    "Ser#: 0x%04lx",
                    card_extract_bits(card->cid, 47, 16)
#endif
                    );

            simplelist_addline(SIMPLELIST_ADD_LINE, "M=%02x, "
#if (CONFIG_STORAGE & STORAGE_SD)
                    "O=%c%c",
                    (int) card_extract_bits(card->cid, 127, 8),
                    card_extract_bits(card->cid, 119, 8),
                    card_extract_bits(card->cid, 111, 8)
#elif (CONFIG_STORAGE & STORAGE_MMC)
                    "O=%04x",
                    (int) card_extract_bits(card->cid, 127, 8),
                    (int) card_extract_bits(card->cid, 119, 16)
#endif
                    );

#if (CONFIG_STORAGE & STORAGE_MMC)
            int temp = card_extract_bits(card->csd, 125, 4);
            simplelist_addline(SIMPLELIST_ADD_LINE,
                     "MMC v%s", temp < 5 ?
                            mmc_spec_vers[temp] : "?.?");
#endif
            simplelist_addline(SIMPLELIST_ADD_LINE,
                    "Blocks: 0x%08lx", card->numblocks);
            output_dyn_value(pbuf, sizeof pbuf, card->speed / 1000,
                                            kbit_units, false);
            simplelist_addline(SIMPLELIST_ADD_LINE,
                    "Speed: %s", pbuf);
            output_dyn_value(pbuf, sizeof pbuf, card->taac,
                            nsec_units, false);
            simplelist_addline(SIMPLELIST_ADD_LINE,
                    "Taac: %s", pbuf);
            simplelist_addline(SIMPLELIST_ADD_LINE,
                    "Nsac: %d clk", card->nsac);
            simplelist_addline(SIMPLELIST_ADD_LINE,
                    "R2W: *%d", card->r2w_factor);
#if (CONFIG_STORAGE & STORAGE_SD)
            int csd_structure = card_extract_bits(card->csd, 127, 2);
            if (csd_structure == 0) /* CSD version 1.0 */
#endif
            {
            simplelist_addline(SIMPLELIST_ADD_LINE,
                    "IRmax: %d..%d mA",
                    i_vmin[card_extract_bits(card->csd, 61, 3)],
                    i_vmax[card_extract_bits(card->csd, 58, 3)]);
            simplelist_addline(SIMPLELIST_ADD_LINE,
                    "IWmax: %d..%d mA",
                    i_vmin[card_extract_bits(card->csd, 55, 3)],
                    i_vmax[card_extract_bits(card->csd, 52, 3)]);
            }
        }
        else if (card->initialized == 0)
        {
            simplelist_addline(SIMPLELIST_ADD_LINE, "Not Found!");
        }
#if (CONFIG_STORAGE & STORAGE_SD)
        else /* card->initialized < 0 */
        {
            simplelist_addline(SIMPLELIST_ADD_LINE, "Init Error! (%d)", card->initialized);
        }
#endif
        snprintf(title, 16, "[" CARDTYPE " %d]", *cardnum);
        gui_synclist_set_title(lists, title, Icon_NOICON);
        gui_synclist_set_nb_items(lists, simplelist_get_line_count());
        gui_synclist_select_item(lists, 0);
        btn = ACTION_REDRAW;
    }
    return btn;
}
#elif  (CONFIG_STORAGE & STORAGE_ATA) 
static int disk_callback(int btn, struct gui_synclist *lists)
{
    (void)lists;
    int i;
    char buf[128];
    unsigned short* identify_info = ata_get_identify();
    bool timing_info_present = false;
    (void)btn;

    simplelist_set_line_count(0);

    for (i=0; i < 20; i++)
        ((unsigned short*)buf)[i]=htobe16(identify_info[i+27]);
    buf[40]=0;
    /* kill trailing space */
    for (i=39; i && buf[i]==' '; i--)
        buf[i] = 0;
    simplelist_addline(SIMPLELIST_ADD_LINE, "Model: %s", buf);
    for (i=0; i < 4; i++)
        ((unsigned short*)buf)[i]=htobe16(identify_info[i+23]);
    buf[8]=0;
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "Firmware: %s", buf);
    snprintf(buf, sizeof buf, "%ld MB",
             ((unsigned long)identify_info[61] << 16 |
              (unsigned long)identify_info[60]) / 2048 );
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "Size: %s", buf);
    unsigned long free;
    fat_size( IF_MV2(0,) NULL, &free );
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "Free: %ld MB", free / 1024);
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "Spinup time: %d ms", storage_spinup_time() * (1000/HZ));
    i = identify_info[83] & (1<<3);
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "Power mgmt: %s", i ? "enabled" : "unsupported");
    i = identify_info[83] & (1<<9);
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "Noise mgmt: %s", i ? "enabled" : "unsupported");
    i = identify_info[82] & (1<<6);
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "Read-ahead: %s", i ? "enabled" : "unsupported");
    timing_info_present = identify_info[53] & (1<<1);
    if(timing_info_present) {
        char pio3[2], pio4[2];pio3[1] = 0;
        pio4[1] = 0;
        pio3[0] = (identify_info[64] & (1<<0)) ? '3' : 0;
        pio4[0] = (identify_info[64] & (1<<1)) ? '4' : 0;
        simplelist_addline(SIMPLELIST_ADD_LINE,
                 "PIO modes: 0 1 2 %s %s", pio3, pio4);
    }
    else {
        simplelist_addline(SIMPLELIST_ADD_LINE,
                 "No PIO mode info");
    }
    timing_info_present = identify_info[53] & (1<<1);
    if(timing_info_present) {
        simplelist_addline(SIMPLELIST_ADD_LINE,
                 "Cycle times %dns/%dns",
                 identify_info[67],
                 identify_info[68] );
    } else {
        simplelist_addline(SIMPLELIST_ADD_LINE,
                 "No timing info");
    }
    int sector_size = 512;
    if((identify_info[106] & 0xe000) == 0x6000)
        sector_size *= BIT_N(identify_info[106] & 0x000f);
    simplelist_addline(SIMPLELIST_ADD_LINE,
            "Physical sector size: %d", sector_size);
#ifdef HAVE_ATA_DMA
    if (identify_info[63] & (1<<0)) {
        char mdma0[2], mdma1[2], mdma2[2];
        mdma0[1] = mdma1[1] = mdma2[1] = 0;
        mdma0[0] = (identify_info[63] & (1<<0)) ? '0' : 0;
        mdma1[0] = (identify_info[63] & (1<<1)) ? '1' : 0;
        mdma2[0] = (identify_info[63] & (1<<2)) ? '2' : 0;
        simplelist_addline(SIMPLELIST_ADD_LINE,
                 "MDMA modes: %s %s %s", mdma0, mdma1, mdma2);
        simplelist_addline(SIMPLELIST_ADD_LINE,
                 "MDMA Cycle times %dns/%dns",
                 identify_info[65],
                 identify_info[66] );
    }
    else {
        simplelist_addline(SIMPLELIST_ADD_LINE,
                "No MDMA mode info");
    }
    if (identify_info[53] & (1<<2)) {
        char udma0[2], udma1[2], udma2[2], udma3[2], udma4[2], udma5[2], udma6[2];
        udma0[1] = udma1[1] = udma2[1] = udma3[1] = udma4[1] = udma5[1] = udma6[1] = 0;
        udma0[0] = (identify_info[88] & (1<<0)) ? '0' : 0;
        udma1[0] = (identify_info[88] & (1<<1)) ? '1' : 0;
        udma2[0] = (identify_info[88] & (1<<2)) ? '2' : 0;
        udma3[0] = (identify_info[88] & (1<<3)) ? '3' : 0;
        udma4[0] = (identify_info[88] & (1<<4)) ? '4' : 0;
        udma5[0] = (identify_info[88] & (1<<5)) ? '5' : 0;
        udma6[0] = (identify_info[88] & (1<<6)) ? '6' : 0;
        simplelist_addline(SIMPLELIST_ADD_LINE,
                 "UDMA modes: %s %s %s %s %s %s %s", udma0, udma1, udma2,
                                                     udma3, udma4, udma5, udma6);
    }
    else {
        simplelist_addline(SIMPLELIST_ADD_LINE,
                "No UDMA mode info");
    }
#endif /* HAVE_ATA_DMA */
    timing_info_present = identify_info[53] & (1<<1);
    if(timing_info_present) {
        i = identify_info[49] & (1<<11);
        simplelist_addline(SIMPLELIST_ADD_LINE,
            "IORDY support: %s", i ? "yes" : "no");
        i = identify_info[49] & (1<<10);
        simplelist_addline(SIMPLELIST_ADD_LINE,
                "IORDY disable: %s", i ? "yes" : "no");
    } else {
        simplelist_addline(SIMPLELIST_ADD_LINE,
                "No timing info");
    }
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "Cluster size: %d bytes", fat_get_cluster_size(IF_MV(0)));
#ifdef HAVE_ATA_DMA
    i = ata_get_dma_mode();
    if (i == 0) {
        simplelist_addline(SIMPLELIST_ADD_LINE,
                 "DMA not enabled");
    } else {
        simplelist_addline(SIMPLELIST_ADD_LINE,
                 "DMA mode: %s %c",
                (i & 0x40) ? "UDMA" : "MDMA",
                '0' + (i & 7));
    }
#endif /* HAVE_ATA_DMA */
    return btn;
}
#else /* No SD, MMC or ATA */
static int disk_callback(int btn, struct gui_synclist *lists)
{
    (void)btn;
    (void)lists;
    struct storage_info info;
    storage_get_info(0,&info);
    simplelist_addline(SIMPLELIST_ADD_LINE, "Vendor: %s", info.vendor);
    simplelist_addline(SIMPLELIST_ADD_LINE, "Model: %s", info.product);
    simplelist_addline(SIMPLELIST_ADD_LINE, "Firmware: %s", info.revision);
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "Size: %ld MB", info.num_sectors*(info.sector_size/512)/2024);
    unsigned long free;
    fat_size( IF_MV2(0,) NULL, &free );
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "Free: %ld MB", free / 1024);
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "Cluster size: %d bytes", fat_get_cluster_size(IF_MV(0)));
    return btn;
}
#endif

#if  (CONFIG_STORAGE & STORAGE_ATA) 
static bool dbg_identify_info(void)
{
    int fd = creat("/identify_info.bin", 0666);
    if(fd >= 0)
    {
#ifdef ROCKBOX_LITTLE_ENDIAN
        ecwrite(fd, ata_get_identify(), SECTOR_SIZE/2, "s", true);
#else
        write(fd, ata_get_identify(), SECTOR_SIZE);
#endif
        close(fd);
    }
    return false;
}
#endif

static bool dbg_disk_info(void)
{
    struct simplelist_info info;
    simplelist_info_init(&info, "Disk Info", 1, NULL);
#if (CONFIG_STORAGE & STORAGE_MMC) || (CONFIG_STORAGE & STORAGE_SD)
    char title[16];
    int card = 0;
    info.callback_data = (void*)&card;
    info.title = title;
#endif
    info.action_callback = disk_callback;
    info.hide_selection = true;
    info.scroll_all = true;
    return simplelist_show_list(&info);
}
#endif /* PLATFORM_NATIVE */

#ifdef HAVE_DIRCACHE
static int dircache_callback(int btn, struct gui_synclist *lists)
{
    (void)btn; (void)lists;
    simplelist_set_line_count(0);
    simplelist_addline(SIMPLELIST_ADD_LINE, "Cache initialized: %s",
             dircache_is_enabled() ? "Yes" : "No");
    simplelist_addline(SIMPLELIST_ADD_LINE, "Cache size: %d B",
             dircache_get_cache_size());
    simplelist_addline(SIMPLELIST_ADD_LINE, "Last size: %d B",
             global_status.dircache_size);
    simplelist_addline(SIMPLELIST_ADD_LINE, "Limit: %d B",
             DIRCACHE_LIMIT);
    simplelist_addline(SIMPLELIST_ADD_LINE, "Reserve: %d/%d B",
             dircache_get_reserve_used(), DIRCACHE_RESERVE);
    simplelist_addline(SIMPLELIST_ADD_LINE, "Scanning took: %d s",
             dircache_get_build_ticks() / HZ);
    simplelist_addline(SIMPLELIST_ADD_LINE, "Entry count: %d",
             dircache_get_entry_count());
    return btn;
}

static bool dbg_dircache_info(void)
{
    struct simplelist_info info;
    simplelist_info_init(&info, "Dircache Info", 7, NULL);
    info.action_callback = dircache_callback;
    info.hide_selection = true;
    info.scroll_all = true;
    return simplelist_show_list(&info);
}

#endif /* HAVE_DIRCACHE */

#ifdef HAVE_TAGCACHE
static int database_callback(int btn, struct gui_synclist *lists)
{
    (void)lists;
    struct tagcache_stat *stat = tagcache_get_stat();
    static bool synced = false;

    simplelist_set_line_count(0);

    simplelist_addline(SIMPLELIST_ADD_LINE, "Initialized: %s",
             stat->initialized ? "Yes" : "No");
    simplelist_addline(SIMPLELIST_ADD_LINE, "DB Ready: %s",
             stat->ready ? "Yes" : "No");
    simplelist_addline(SIMPLELIST_ADD_LINE, "RAM Cache: %s",
             stat->ramcache ? "Yes" : "No");
    simplelist_addline(SIMPLELIST_ADD_LINE, "RAM: %d/%d B",
             stat->ramcache_used, stat->ramcache_allocated);
    simplelist_addline(SIMPLELIST_ADD_LINE, "Progress: %d%% (%d entries)",
             stat->progress, stat->processed_entries);
    simplelist_addline(SIMPLELIST_ADD_LINE, "Curfile: %s",
                       stat->curentry ? stat->curentry : "---");
    simplelist_addline(SIMPLELIST_ADD_LINE, "Commit step: %d",
             stat->commit_step);
    simplelist_addline(SIMPLELIST_ADD_LINE, "Commit delayed: %s",
             stat->commit_delayed ? "Yes" : "No");

    simplelist_addline(SIMPLELIST_ADD_LINE, "Queue length: %d", 
             stat->queue_length);
    
    if (synced)
    {
        synced = false;
        tagcache_screensync_event();
    }

    if (!btn && stat->curentry)
    {
        synced = true;
        return ACTION_REDRAW;
    }

    if (btn == ACTION_STD_CANCEL)
        tagcache_screensync_enable(false);

    return btn;
}
static bool dbg_tagcache_info(void)
{
    struct simplelist_info info;
    simplelist_info_init(&info, "Database Info", 8, NULL);
    info.action_callback = database_callback;
    info.hide_selection = true;
    info.scroll_all = true;
    
    /* Don't do nonblock here, must give enough processing time
       for tagcache thread. */
    /* info.timeout = TIMEOUT_NOBLOCK; */
    info.timeout = 1;
    tagcache_screensync_enable(true);
    return simplelist_show_list(&info);
}
#endif

#if CONFIG_CPU == SH7034
static bool dbg_save_roms(void)
{
    int fd;
    int oldmode = system_memory_guard(MEMGUARD_NONE);

    fd = creat("/internal_rom_0000-FFFF.bin", 0666);
    if(fd >= 0)
    {
        write(fd, (void *)0, 0x10000);
        close(fd);
    }

    fd = creat("/internal_rom_2000000-203FFFF.bin", 0666);
    if(fd >= 0)
    {
        write(fd, (void *)0x2000000, 0x40000);
        close(fd);
    }

    system_memory_guard(oldmode);
    return false;
}
#elif defined CPU_COLDFIRE
static bool dbg_save_roms(void)
{
    int fd;
    int oldmode = system_memory_guard(MEMGUARD_NONE);

#if defined(IRIVER_H100_SERIES)
    fd = creat("/internal_rom_000000-1FFFFF.bin", 0666);
#elif defined(IRIVER_H300_SERIES)
    fd = creat("/internal_rom_000000-3FFFFF.bin", 0666);
#elif defined(IAUDIO_X5) || defined(IAUDIO_M5) || defined(IAUDIO_M3)
    fd = creat("/internal_rom_000000-3FFFFF.bin", 0666);
#elif defined(MPIO_HD200) || defined(MPIO_HD300)
    fd = creat("/internal_rom_000000-1FFFFF.bin", 0666);
#endif
    if(fd >= 0)
    {
        write(fd, (void *)0, FLASH_SIZE);
        close(fd);
    }
    system_memory_guard(oldmode);

#ifdef HAVE_EEPROM
    fd = creat("/internal_eeprom.bin", 0666);
    if (fd >= 0)
    {
        int old_irq_level;
        char buf[EEPROM_SIZE];
        int err;

        old_irq_level = disable_irq_save();

        err = eeprom_24cxx_read(0, buf, sizeof buf);

        restore_irq(old_irq_level);

        if (err)
            splashf(HZ*3, "Eeprom read failure (%d)", err);
        else
        {
            write(fd, buf, sizeof buf);
        }

        close(fd);
    }
#endif

    return false;
}
#elif defined(CPU_PP) && !(CONFIG_STORAGE & STORAGE_SD)
static bool dbg_save_roms(void)
{
    int fd;

    fd = creat("/internal_rom_000000-0FFFFF.bin", 0666);
    if(fd >= 0)
    {
        write(fd, (void *)0x20000000, FLASH_SIZE);
        close(fd);
    }

    return false;
}
#elif CONFIG_CPU == IMX31L
static bool dbg_save_roms(void)
{
    int fd;

    fd = creat("/flash_rom_A0000000-A01FFFFF.bin", 0666);
    if (fd >= 0)
    {
        write(fd, (void*)0xa0000000, FLASH_SIZE);
        close(fd);
    }

    return false;
}
#elif defined(CPU_TCC780X)
static bool dbg_save_roms(void)
{
    int fd;

    fd = creat("/eeprom_E0000000-E0001FFF.bin", 0666);
    if (fd >= 0)
    {
        write(fd, (void*)0xe0000000, 0x2000);
        close(fd);
    }

    return false;
}
#endif /* CPU */

#ifndef SIMULATOR
#if CONFIG_TUNER

#ifdef CONFIG_TUNER_MULTI
static int tuner_type = 0;
#define IF_TUNER_TYPE(type) if(tuner_type==type)
#else
#define IF_TUNER_TYPE(type)
#endif

static int radio_callback(int btn, struct gui_synclist *lists)
{
    (void)lists;
    if (btn == ACTION_STD_CANCEL)
        return btn;
    simplelist_set_line_count(1);

#if (CONFIG_TUNER & LV24020LP)
    simplelist_addline(SIMPLELIST_ADD_LINE,
                        "CTRL_STAT: %02X", lv24020lp_get(LV24020LP_CTRL_STAT) );
    simplelist_addline(SIMPLELIST_ADD_LINE,
                        "RADIO_STAT: %02X", lv24020lp_get(LV24020LP_REG_STAT) );
    simplelist_addline(SIMPLELIST_ADD_LINE,
                        "MSS_FM: %d kHz", lv24020lp_get(LV24020LP_MSS_FM) );
    simplelist_addline(SIMPLELIST_ADD_LINE,
                        "MSS_IF: %d Hz", lv24020lp_get(LV24020LP_MSS_IF) );
    simplelist_addline(SIMPLELIST_ADD_LINE,
                        "MSS_SD: %d Hz", lv24020lp_get(LV24020LP_MSS_SD) );
    simplelist_addline(SIMPLELIST_ADD_LINE,
                        "if_set: %d Hz", lv24020lp_get(LV24020LP_IF_SET) );
    simplelist_addline(SIMPLELIST_ADD_LINE,
                        "sd_set: %d Hz", lv24020lp_get(LV24020LP_SD_SET) );
#endif /* LV24020LP */
#if (CONFIG_TUNER & S1A0903X01)
    simplelist_addline(SIMPLELIST_ADD_LINE,
                        "Samsung regs: %08X", s1a0903x01_get(RADIO_ALL));
    /* This one doesn't return dynamic data atm */
#endif /* S1A0903X01 */
#if (CONFIG_TUNER & TEA5767)
    struct tea5767_dbg_info nfo;
    tea5767_dbg_info(&nfo);
    simplelist_addline(SIMPLELIST_ADD_LINE, "Philips regs:");
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "   Read: %02X %02X %02X %02X %02X",
             (unsigned)nfo.read_regs[0], (unsigned)nfo.read_regs[1],
             (unsigned)nfo.read_regs[2], (unsigned)nfo.read_regs[3],
             (unsigned)nfo.read_regs[4]);
    simplelist_addline(SIMPLELIST_ADD_LINE,
             "   Write: %02X %02X %02X %02X %02X",
             (unsigned)nfo.write_regs[0], (unsigned)nfo.write_regs[1],
             (unsigned)nfo.write_regs[2], (unsigned)nfo.write_regs[3],
             (unsigned)nfo.write_regs[4]);
#endif /* TEA5767 */
#if (CONFIG_TUNER & SI4700)
    IF_TUNER_TYPE(SI4700)
    {
        struct si4700_dbg_info nfo;
        int i;
        si4700_dbg_info(&nfo);
        simplelist_addline(SIMPLELIST_ADD_LINE, "SI4700 regs:");
        for (i = 0; i < 16; i += 4) {
            simplelist_addline(SIMPLELIST_ADD_LINE,"%02X: %04X %04X %04X %04X",
                i, nfo.regs[i], nfo.regs[i+1], nfo.regs[i+2], nfo.regs[i+3]);
        }
    }
#endif /* SI4700 */
#if (CONFIG_TUNER & RDA5802)
    IF_TUNER_TYPE(RDA5802)
    {
        struct rda5802_dbg_info nfo;
        int i;
        rda5802_dbg_info(&nfo);
        simplelist_addline(SIMPLELIST_ADD_LINE, "RDA5802 regs:");
        for (i = 0; i < 16; i += 4) {
            simplelist_addline(SIMPLELIST_ADD_LINE,"%02X: %04X %04X %04X %04X",
                i, nfo.regs[i], nfo.regs[i+1], nfo.regs[i+2], nfo.regs[i+3]);
        }
    }
#endif /* RDA55802 */
    return ACTION_REDRAW;
}
static bool dbg_fm_radio(void)
{
    struct simplelist_info info;
#ifdef CONFIG_TUNER_MULTI
    tuner_type = tuner_detect_type();
#endif    
    info.scroll_all = true;
    simplelist_info_init(&info, "FM Radio", 1, NULL);
    simplelist_set_line_count(0);
    simplelist_addline(SIMPLELIST_ADD_LINE, "HW detected: %s",
                       radio_hardware_present() ? "yes" : "no");

    info.action_callback = radio_hardware_present()?radio_callback : NULL;
    info.hide_selection = true;
    return simplelist_show_list(&info);
}
#endif /* CONFIG_TUNER */
#endif /* !SIMULATOR */

#ifdef HAVE_LCD_BITMAP
extern bool do_screendump_instead_of_usb;

static bool dbg_screendump(void)
{
    do_screendump_instead_of_usb = !do_screendump_instead_of_usb;
    splashf(HZ, "Screendump %s",
                 do_screendump_instead_of_usb?"enabled":"disabled");
    return false;
}
#endif /* HAVE_LCD_BITMAP */

extern bool write_metadata_log;

static bool dbg_metadatalog(void)
{
    write_metadata_log = !write_metadata_log;
    splashf(HZ, "Metadata log %s",
                 write_metadata_log?"enabled":"disabled");
    return false;
}

#if CONFIG_CPU == SH7034 || defined(CPU_COLDFIRE)
static bool dbg_set_memory_guard(void)
{
    static const struct opt_items names[MAXMEMGUARD] = {
        { "None",             -1 },
        { "Flash ROM writes", -1 },
        { "Zero area (all)",  -1 }
    };
    int mode = system_memory_guard(MEMGUARD_KEEP);

    set_option( "Catch mem accesses", &mode, INT, names, MAXMEMGUARD, NULL);
    system_memory_guard(mode);

    return false;
}
#endif /* CONFIG_CPU == SH7034 || defined(CPU_COLDFIRE) */

#if defined(HAVE_EEPROM) && !defined(HAVE_EEPROM_SETTINGS)
static bool dbg_write_eeprom(void)
{
    int fd;
    int rc;
    int old_irq_level;
    char buf[EEPROM_SIZE];
    int err;

    fd = open("/internal_eeprom.bin", O_RDONLY);

    if (fd >= 0)
    {
        rc = read(fd, buf, EEPROM_SIZE);

        if(rc == EEPROM_SIZE)
        {
            old_irq_level = disable_irq_save();

            err = eeprom_24cxx_write(0, buf, sizeof buf);
            if (err)
                splashf(HZ*3, "Eeprom write failure (%d)", err);
            else
                splash(HZ*3, "Eeprom written successfully");

            restore_irq(old_irq_level);
        }
        else
        {
            splashf(HZ*3, "File read error (%d)",rc);
        }
        close(fd);
    }
    else
    {
        splash(HZ*3, "Failed to open 'internal_eeprom.bin'");
    }

    return false;
}
#endif /* defined(HAVE_EEPROM) && !defined(HAVE_EEPROM_SETTINGS) */
#ifdef CPU_BOOST_LOGGING
static bool cpu_boost_log(void)
{
    int i = 0,j=0;
    int count = cpu_boost_log_getcount();
    int lines = LCD_HEIGHT/SYSFONT_HEIGHT;
    char *str;
    bool done;
    lcd_setfont(FONT_SYSFIXED);
    str = cpu_boost_log_getlog_first();
    while (i < count)
    {
        lcd_clear_display();
        for(j=0; j<lines; j++,i++)
        {
            if (!str)
                str = cpu_boost_log_getlog_next();
            if (str)
            {
                if(strlen(str) > LCD_WIDTH/SYSFONT_WIDTH)
                    lcd_puts_scroll(0, j, str);
                else
                    lcd_puts(0, j,str);
            }
            str = NULL;
        }
        lcd_update();
        done = false;
        while (!done)
        {
            switch(get_action(CONTEXT_STD,TIMEOUT_BLOCK))
            {
                case ACTION_STD_OK:
                case ACTION_STD_PREV:
                case ACTION_STD_NEXT:
                    done = true;
                break;
                case ACTION_STD_CANCEL:
                    i = count;
                    done = true;
                break;
            }
        }
    }
    lcd_stop_scroll();
    get_action(CONTEXT_STD,TIMEOUT_BLOCK);
    lcd_setfont(FONT_UI);
    return false;
}
#endif

#if (defined(HAVE_WHEEL_ACCELERATION) && (CONFIG_KEYPAD==IPOD_4G_PAD) \
     && !defined(IPOD_MINI) && !defined(SIMULATOR))
extern bool wheel_is_touched;
extern int old_wheel_value;
extern int new_wheel_value;
extern int wheel_delta;
extern unsigned int accumulated_wheel_delta;
extern unsigned int wheel_velocity;

static bool dbg_scrollwheel(void)
{
    unsigned int speed;

    lcd_setfont(FONT_SYSFIXED);

    while (1)
    {
        if (action_userabort(HZ/10))
            break;

        lcd_clear_display();

        /* show internal variables of scrollwheel driver */
        lcd_putsf(0, 0, "wheel touched: %s", (wheel_is_touched) ? "true" : "false");
        lcd_putsf(0, 1, "new position: %2d", new_wheel_value);
        lcd_putsf(0, 2, "old position: %2d", old_wheel_value);
        lcd_putsf(0, 3, "wheel delta: %2d", wheel_delta);
        lcd_putsf(0, 4, "accumulated delta: %2d", accumulated_wheel_delta);
        lcd_putsf(0, 5, "velo [deg/s]: %4d", (int)wheel_velocity);

        /* show effective accelerated scrollspeed */
        speed = button_apply_acceleration( (1<<31)|(1<<24)|wheel_velocity);
        lcd_putsf(0, 6, "accel. speed: %4d", speed);

        lcd_update();
    }
    lcd_setfont(FONT_UI);
    return false;
}
#endif

#if defined (HAVE_USBSTACK)

#if defined(ROCKBOX_HAS_LOGF) && defined(USB_ENABLE_SERIAL)
static bool toggle_usb_core_driver(int driver, char *msg)
{
    bool enabled = !usb_core_driver_enabled(driver);

    usb_core_enable_driver(driver,enabled);
    splashf(HZ, "%s %s", msg, enabled?"enabled":"disabled");

    return false;
}

static bool toggle_usb_serial(void)
{
    return toggle_usb_core_driver(USB_DRIVER_SERIAL,"USB Serial");
}
#endif

#endif

#if CONFIG_USBOTG == USBOTG_ISP1583
extern int dbg_usb_num_items(void);
extern const char* dbg_usb_item(int selected_item, void *data,
                                char *buffer, size_t buffer_len);

static int isp1583_action_callback(int action, struct gui_synclist *lists)
{
    (void)lists;
    if (action == ACTION_NONE)
        action = ACTION_REDRAW;
    return action;
}

static bool dbg_isp1583(void)
{
    struct simplelist_info isp1583;
    isp1583.scroll_all = true;
    simplelist_info_init(&isp1583, "ISP1583", dbg_usb_num_items(), NULL);
    isp1583.timeout = HZ/100; 
    isp1583.hide_selection = true;
    isp1583.get_name = dbg_usb_item;
    isp1583.action_callback = isp1583_action_callback;
    return simplelist_show_list(&isp1583);
}
#endif

#if defined(CREATIVE_ZVx) && !defined(SIMULATOR)
extern int pic_dbg_num_items(void);
extern const char* pic_dbg_item(int selected_item, void *data,
                                char *buffer, size_t buffer_len);

static int pic_action_callback(int action, struct gui_synclist *lists)
{
    (void)lists;
    if (action == ACTION_NONE)
        action = ACTION_REDRAW;
    return action;
}

static bool dbg_pic(void)
{
    struct simplelist_info pic;
    pic.scroll_all = true;
    simplelist_info_init(&pic, "PIC", pic_dbg_num_items(), NULL);
    pic.timeout = HZ/100; 
    pic.hide_selection = true;
    pic.get_name = pic_dbg_item;
    pic.action_callback = pic_action_callback;
    return simplelist_show_list(&pic);
}
#endif


/****** The menu *********/
struct the_menu_item {
    unsigned char *desc; /* string or ID */
    bool (*function) (void); /* return true if USB was connected */
};
static const struct the_menu_item menuitems[] = {
#if CONFIG_CPU == SH7034 || defined(CPU_COLDFIRE) || \
    (defined(CPU_PP) && !(CONFIG_STORAGE & STORAGE_SD)) || \
    CONFIG_CPU == IMX31L || defined(CPU_TCC780X)
        { "Dump ROM contents", dbg_save_roms },
#endif
#if CONFIG_CPU == SH7034 || defined(CPU_COLDFIRE) || defined(CPU_PP) \
    || CONFIG_CPU == S3C2440 || CONFIG_CPU == IMX31L || CONFIG_CPU == AS3525 \
    || CONFIG_CPU == DM320 || defined(CPU_S5L870X) || CONFIG_CPU == AS3525v2 \
    || CONFIG_CPU == RK27XX
        { "View I/O ports", dbg_ports },
#endif
#if (CONFIG_RTC == RTC_PCF50605) && (CONFIG_PLATFORM & PLATFORM_NATIVE)
        { "View PCF registers", dbg_pcf },
#endif
#if defined(HAVE_TSC2100) && (CONFIG_PLATFORM & PLATFORM_NATIVE)
        { "TSC2100 debug", tsc2100_debug },
#endif
#ifdef HAVE_ADJUSTABLE_CPU_FREQ
        { "CPU frequency", dbg_cpufreq },
#endif
#if CONFIG_CPU == IMX31L
        { "DVFS/DPTC", __dbg_dvfs_dptc },
#endif
#if defined(IRIVER_H100_SERIES) && !defined(SIMULATOR)
        { "S/PDIF analyzer", dbg_spdif },
#endif
#if CONFIG_CPU == SH7034 || defined(CPU_COLDFIRE)
        { "Catch mem accesses", dbg_set_memory_guard },
#endif
        { "View OS stacks", dbg_os },
#ifdef HAVE_LCD_BITMAP
#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
        { "View battery", view_battery },
#endif
        { "Screendump", dbg_screendump },
#endif
#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
        { "View HW info", dbg_hw_info },
#endif
#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
        { "View partitions", dbg_partitions },
#endif
#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
        { "View disk info", dbg_disk_info },
#if (CONFIG_STORAGE & STORAGE_ATA)
        { "Dump ATA identify info", dbg_identify_info},
#endif
#endif
        { "Metadata log", dbg_metadatalog },
#ifdef HAVE_DIRCACHE
        { "View dircache info", dbg_dircache_info },
#endif
#ifdef HAVE_TAGCACHE
        { "View database info", dbg_tagcache_info },
#endif
#ifdef HAVE_LCD_BITMAP
#if CONFIG_CODEC == SWCODEC
        { "View buffering thread", dbg_buffering_thread },
#elif !defined(SIMULATOR)
        { "View audio thread", dbg_audio_thread },
#endif
#ifdef PM_DEBUG
        { "pm histogram", peak_meter_histogram},
#endif /* PM_DEBUG */
#endif /* HAVE_LCD_BITMAP */
        { "View buflib allocs", dbg_buflib_allocs },
#ifndef SIMULATOR
#if CONFIG_TUNER
        { "FM Radio", dbg_fm_radio },
#endif
#endif
#if defined(HAVE_EEPROM) && !defined(HAVE_EEPROM_SETTINGS)
        { "Write back EEPROM", dbg_write_eeprom },
#endif
#if CONFIG_USBOTG == USBOTG_ISP1583
        { "View ISP1583 info", dbg_isp1583 },
#endif
#if defined(CREATIVE_ZVx) && !defined(SIMULATOR)
        { "View PIC info", dbg_pic },
#endif
#ifdef ROCKBOX_HAS_LOGF
        {"Show Log File", logfdisplay },
        {"Dump Log File", logfdump },
#endif
#if defined(HAVE_USBSTACK)
#if defined(ROCKBOX_HAS_LOGF) && defined(USB_ENABLE_SERIAL)
        {"USB Serial driver (logf)", toggle_usb_serial },
#endif
#endif /* HAVE_USBSTACK */
#ifdef CPU_BOOST_LOGGING
        {"cpu_boost log",cpu_boost_log},
#endif
#if (defined(HAVE_WHEEL_ACCELERATION) && (CONFIG_KEYPAD==IPOD_4G_PAD) \
     && !defined(IPOD_MINI) && !defined(SIMULATOR))
        {"Debug scrollwheel", dbg_scrollwheel },
#endif
    };
static int menu_action_callback(int btn, struct gui_synclist *lists)
{
    int selection = gui_synclist_get_sel_pos(lists);
    if (btn == ACTION_STD_OK)
    {
        FOR_NB_SCREENS(i)
           viewportmanager_theme_enable(i, false, NULL);
        menuitems[selection].function();
        btn = ACTION_REDRAW;
        FOR_NB_SCREENS(i)
            viewportmanager_theme_undo(i, false);
    }
    else if (btn == ACTION_STD_CONTEXT)
    {
        MENUITEM_STRINGLIST(menu_items, "Debug Menu", NULL, ID2P(LANG_ADD_TO_FAVES));
        if (do_menu(&menu_items, NULL, NULL, false) == 0)
            shortcuts_add(SHORTCUT_DEBUGITEM, menuitems[selection].desc);
        return ACTION_STD_CANCEL;
    }
    return btn;
}

static const char* dbg_menu_getname(int item, void * data,
                                    char *buffer, size_t buffer_len)
{
    (void)data; (void)buffer; (void)buffer_len;
    return menuitems[item].desc;
}

bool debug_menu(void)
{
    struct simplelist_info info;

    simplelist_info_init(&info, "Debug Menu", ARRAYLEN(menuitems), NULL);
    info.action_callback = menu_action_callback;
    info.get_name = dbg_menu_getname;
    return simplelist_show_list(&info);
}

bool run_debug_screen(char* screen)
{
    unsigned i;
    for (i=0; i<ARRAYLEN(menuitems); i++)
    {
        if (!strcmp(screen, menuitems[i].desc))
        {
            FOR_NB_SCREENS(j)
               viewportmanager_theme_enable(j, false, NULL);
            menuitems[i].function();
            FOR_NB_SCREENS(j)
                viewportmanager_theme_undo(j, false);
            return true;
        }
    }
    return false;
}

