/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2007 Jonathan Gordon
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

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include "config.h"
#include "lang.h"
#include "action.h"
#include "splash.h"
#include "settings.h"
#include "menu.h"
#include "sound_menu.h"
#include "kernel.h"
#include "playlist.h"
#include "dsp.h"
#include "scrobbler.h"
#include "audio.h"
#include "cuesheet.h"
#if CONFIG_CODEC == SWCODEC
#include "playback.h"
#endif


#if (CONFIG_CODEC == SWCODEC) && defined(HAVE_CROSSFADE)
static int setcrossfadeonexit_callback(int action,const struct menu_item_ex *this_item)
{
    (void)this_item;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM: /* on exit */
            audio_set_crossfade(global_settings.crossfade);
            break;
    }
    return action;
}

#endif /* CONFIG_CODEC == SWCODEC */

/***********************************/
/*    PLAYBACK MENU                */
static int playback_callback(int action,const struct menu_item_ex *this_item);
    
MENUITEM_SETTING(shuffle_item, &global_settings.playlist_shuffle, playback_callback);
MENUITEM_SETTING(repeat_mode, &global_settings.repeat_mode, playback_callback);
MENUITEM_SETTING(play_selected, &global_settings.play_selected, NULL);

MENUITEM_SETTING(ff_rewind_accel, &global_settings.ff_rewind_accel, NULL);
MENUITEM_SETTING(ff_rewind_min_step, &global_settings.ff_rewind_min_step, NULL);
MAKE_MENU(ff_rewind_settings_menu, ID2P(LANG_WIND_MENU), 0, Icon_NOICON,
          &ff_rewind_min_step, &ff_rewind_accel);
#ifdef HAVE_DISK_STORAGE
#if CONFIG_CODEC == SWCODEC
static int buffermargin_callback(int action,const struct menu_item_ex *this_item)
{
    (void)this_item;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM: /* on exit */
            audio_set_buffer_margin(global_settings.buffer_margin);
            break;
    }
    return action;
}
#else 
# define buffermargin_callback NULL
#endif
MENUITEM_SETTING(buffer_margin, &global_settings.buffer_margin,
                 buffermargin_callback);
#endif /*HAVE_DISK_STORAGE */
MENUITEM_SETTING(fade_on_stop, &global_settings.fade_on_stop, NULL);
MENUITEM_SETTING(party_mode, &global_settings.party_mode, NULL);

#if CONFIG_CODEC == SWCODEC
#ifdef HAVE_CROSSFADE
/* crossfade submenu */
MENUITEM_SETTING(crossfade, &global_settings.crossfade, setcrossfadeonexit_callback);
MENUITEM_SETTING(crossfade_fade_in_delay,
    &global_settings.crossfade_fade_in_delay, NULL);
MENUITEM_SETTING(crossfade_fade_in_duration,
    &global_settings.crossfade_fade_in_duration, NULL);
MENUITEM_SETTING(crossfade_fade_out_delay,
    &global_settings.crossfade_fade_out_delay, setcrossfadeonexit_callback);
MENUITEM_SETTING(crossfade_fade_out_duration,
    &global_settings.crossfade_fade_out_duration, setcrossfadeonexit_callback);
MENUITEM_SETTING(crossfade_fade_out_mixmode,
    &global_settings.crossfade_fade_out_mixmode,NULL);
MAKE_MENU(crossfade_settings_menu,ID2P(LANG_CROSSFADE),0, Icon_NOICON,
          &crossfade, &crossfade_fade_in_delay, &crossfade_fade_in_duration,
          &crossfade_fade_out_delay, &crossfade_fade_out_duration,
          &crossfade_fade_out_mixmode);
#endif

/* replay gain submenu */

static int replaygain_callback(int action,const struct menu_item_ex *this_item)
{
    (void)this_item;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM: /* on exit */    
            dsp_set_replaygain();
            break;
    }
    return action;
}
MENUITEM_SETTING(replaygain_noclip, &global_settings.replaygain_noclip ,replaygain_callback);
MENUITEM_SETTING(replaygain_type, &global_settings.replaygain_type ,replaygain_callback);
MENUITEM_SETTING(replaygain_preamp, &global_settings.replaygain_preamp ,replaygain_callback);
MAKE_MENU(replaygain_settings_menu,ID2P(LANG_REPLAYGAIN),0, Icon_NOICON,
          &replaygain_type, &replaygain_noclip, &replaygain_preamp);
          
MENUITEM_SETTING(beep, &global_settings.beep ,NULL);
#endif /* CONFIG_CODEC == SWCODEC */

#ifdef HAVE_SPDIF_POWER
MENUITEM_SETTING(spdif_enable, &global_settings.spdif_enable, NULL);
#endif
MENUITEM_SETTING(next_folder, &global_settings.next_folder, NULL);
static int audioscrobbler_callback(int action,const struct menu_item_ex *this_item)
{
    (void)this_item;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM: /* on exit */
            if (!scrobbler_is_enabled() && global_settings.audioscrobbler)
                scrobbler_init();
        
            if(scrobbler_is_enabled() && !global_settings.audioscrobbler)
                scrobbler_shutdown();
            break;
    }
    return action;
}
MENUITEM_SETTING(audioscrobbler, &global_settings.audioscrobbler, audioscrobbler_callback);


static int cuesheet_callback(int action,const struct menu_item_ex *this_item)
{
    (void)this_item;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM: /* on exit */
#if CONFIG_CODEC == SWCODEC
            audio_set_cuesheet(global_settings.cuesheet);
#else
            if (global_settings.cuesheet)
                splash(HZ*2, ID2P(LANG_PLEASE_REBOOT));
            break;
#endif
    }
    return action;
}
MENUITEM_SETTING(cuesheet, &global_settings.cuesheet, cuesheet_callback);

#ifdef HAVE_HEADPHONE_DETECTION
MENUITEM_SETTING(unplug_mode, &global_settings.unplug_mode, NULL);
MENUITEM_SETTING(unplug_autoresume, &global_settings.unplug_autoresume, NULL);
MAKE_MENU(unplug_menu, ID2P(LANG_HEADPHONE_UNPLUG), 0, Icon_NOICON,
          &unplug_mode, &unplug_autoresume);
#endif

MENUITEM_SETTING(skip_length, &global_settings.skip_length, NULL);
MENUITEM_SETTING(prevent_skip, &global_settings.prevent_skip, NULL);
#if CONFIG_CODEC == SWCODEC
MENUITEM_SETTING(resume_rewind, &global_settings.resume_rewind, NULL);
#endif
MENUITEM_SETTING(pause_rewind, &global_settings.pause_rewind, NULL);

MAKE_MENU(playback_settings,ID2P(LANG_PLAYBACK),0,
          Icon_Playback_menu,
          &shuffle_item, &repeat_mode, &play_selected,
          &ff_rewind_settings_menu,
#ifdef HAVE_DISK_STORAGE 
          &buffer_margin,
#endif
          &fade_on_stop, &party_mode,
          
#if (CONFIG_CODEC == SWCODEC) && defined(HAVE_CROSSFADE)
          &crossfade_settings_menu, 
#endif

#if CONFIG_CODEC == SWCODEC
          &replaygain_settings_menu, &beep,
#endif

#ifdef HAVE_SPDIF_POWER
          &spdif_enable,
#endif
          &next_folder, &audioscrobbler, &cuesheet
#ifdef HAVE_HEADPHONE_DETECTION
         ,&unplug_menu
#endif
         ,&skip_length, &prevent_skip,

#if CONFIG_CODEC == SWCODEC
          &resume_rewind,
#endif
          &pause_rewind,
         );
         
static int playback_callback(int action,const struct menu_item_ex *this_item)
{
    static bool old_shuffle = false;
    static int old_repeat = 0;
    switch (action)
    {
        case ACTION_ENTER_MENUITEM:
            if (this_item == &shuffle_item)
            {
                old_shuffle = global_settings.playlist_shuffle;
            }
            else if (this_item == &repeat_mode)
            {
                old_repeat = global_settings.repeat_mode;
            }
            break;

        case ACTION_EXIT_MENUITEM: /* on exit */
            if (!(audio_status() & AUDIO_STATUS_PLAY))
                break;

            if (this_item == &shuffle_item)
            {
                if (old_shuffle == global_settings.playlist_shuffle)
                    break;

#if CONFIG_CODEC == SWCODEC
                dsp_set_replaygain();
#endif
                if (global_settings.playlist_shuffle)
                {
                    playlist_randomise(NULL, current_tick, true);
                }
                else
                {
                    playlist_sort(NULL, true);
                }
            }
            else if (this_item == &repeat_mode)
            {
                if (old_repeat == global_settings.repeat_mode)
                    break;

                audio_flush_and_reload_tracks();
            }

            break;
    }
    return action;
}
/*    PLAYBACK MENU                */
/***********************************/
