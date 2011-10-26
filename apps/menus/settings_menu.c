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
#include <string.h>
#include "config.h"
#include "lang.h"
#include "action.h"
#include "settings.h"
#include "menu.h"
#include "keyboard.h"
#include "sound_menu.h"
#include "exported_menus.h"
#include "tree.h"
#include "tagtree.h"
#include "usb.h"
#include "splash.h"
#include "yesno.h"
#include "talk.h"
#include "powermgmt.h"
#if CONFIG_CODEC == SWCODEC
#include "playback.h"
#endif
#if CONFIG_RTC
#include "screens.h"
#endif
#include "quickscreen.h"
#ifdef HAVE_DIRCACHE
#include "dircache.h"
#endif

/***********************************/
/*    TAGCACHE MENU                */
#ifdef HAVE_TAGCACHE

static void tagcache_rebuild_with_splash(void)
{
    tagcache_rebuild();
    splash(HZ*2, ID2P(LANG_TAGCACHE_FORCE_UPDATE_SPLASH));
}

static void tagcache_update_with_splash(void)
{
    tagcache_update();
    splash(HZ*2, ID2P(LANG_TAGCACHE_FORCE_UPDATE_SPLASH));
}

#ifdef HAVE_TC_RAMCACHE
MENUITEM_SETTING(tagcache_ram, &global_settings.tagcache_ram, NULL);
#endif
MENUITEM_SETTING(tagcache_autoupdate, &global_settings.tagcache_autoupdate, NULL);
MENUITEM_FUNCTION(tc_init, 0, ID2P(LANG_TAGCACHE_FORCE_UPDATE),
                    (int(*)(void))tagcache_rebuild_with_splash,
                    NULL, NULL, Icon_NOICON);
MENUITEM_FUNCTION(tc_update, 0, ID2P(LANG_TAGCACHE_UPDATE),
                    (int(*)(void))tagcache_update_with_splash,
                    NULL, NULL, Icon_NOICON);
MENUITEM_SETTING(runtimedb, &global_settings.runtimedb, NULL);
MENUITEM_FUNCTION(tc_export, 0, ID2P(LANG_TAGCACHE_EXPORT),
                    (int(*)(void))tagtree_export, NULL,
                    NULL, Icon_NOICON);
MENUITEM_FUNCTION(tc_import, 0, ID2P(LANG_TAGCACHE_IMPORT),
                    (int(*)(void))tagtree_import, NULL,
                    NULL, Icon_NOICON);
MAKE_MENU(tagcache_menu, ID2P(LANG_TAGCACHE), 0, Icon_NOICON,
#ifdef HAVE_TC_RAMCACHE
                &tagcache_ram,
#endif
                &tagcache_autoupdate, &tc_init, &tc_update, &runtimedb,
                &tc_export, &tc_import);
#endif /* HAVE_TAGCACHE */
/*    TAGCACHE MENU                */
/***********************************/

/***********************************/
/*    FILE VIEW MENU               */
static int fileview_callback(int action,const struct menu_item_ex *this_item);
MENUITEM_SETTING(sort_case, &global_settings.sort_case, NULL);
MENUITEM_SETTING(sort_dir, &global_settings.sort_dir, fileview_callback);
MENUITEM_SETTING(sort_file, &global_settings.sort_file, fileview_callback);
MENUITEM_SETTING(interpret_numbers, &global_settings.interpret_numbers, fileview_callback);
MENUITEM_SETTING(dirfilter, &global_settings.dirfilter, NULL);
MENUITEM_SETTING(show_filename_ext, &global_settings.show_filename_ext, NULL);
MENUITEM_SETTING(browse_current, &global_settings.browse_current, NULL);
#ifdef HAVE_LCD_BITMAP
MENUITEM_SETTING(show_path_in_browser, &global_settings.show_path_in_browser, NULL);
#endif
static int clear_start_directory(void)
{
    strcpy(global_settings.start_directory, "/");
    settings_save();
    splash(HZ, ID2P(LANG_RESET_DONE_CLEAR));
    return false;
}
MENUITEM_FUNCTION(clear_start_directory_item, 0, ID2P(LANG_RESET_START_DIR), 
                  clear_start_directory, NULL, NULL, Icon_file_view_menu);
static int fileview_callback(int action,const struct menu_item_ex *this_item)
{
    static int oldval;
    int *variable = this_item->variable;
    switch (action)
    {
        case ACTION_ENTER_MENUITEM: /* on entering an item */
            oldval = *variable;
            break;
        case ACTION_EXIT_MENUITEM: /* on exit */
            if (*variable != oldval)
                reload_directory(); /* force reload if this has changed */
            break;
    }
    return action;
}

MAKE_MENU(file_menu, ID2P(LANG_FILE), 0, Icon_file_view_menu,
                &sort_case, &sort_dir, &sort_file, &interpret_numbers,
                &dirfilter, &show_filename_ext, &browse_current,
#ifdef HAVE_LCD_BITMAP
                &show_path_in_browser,
#endif
                &clear_start_directory_item
                );
/*    FILE VIEW MENU               */
/***********************************/


/***********************************/
/*    SYSTEM MENU                  */

/* Battery */
#if BATTERY_CAPACITY_INC > 0
MENUITEM_SETTING(battery_capacity, &global_settings.battery_capacity, NULL);
#endif
#if BATTERY_TYPES_COUNT > 1
MENUITEM_SETTING(battery_type, &global_settings.battery_type, NULL);
#endif
#ifdef HAVE_USB_CHARGING_ENABLE
static int usbcharging_callback(int action,const struct menu_item_ex *this_item)
{
    (void)this_item;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM: /* on exit */
            usb_charging_enable(global_settings.usb_charging);
            break;
    }
    return action;
}
MENUITEM_SETTING(usb_charging, &global_settings.usb_charging, usbcharging_callback);
#endif /* HAVE_USB_CHARGING_ENABLE */
MAKE_MENU(battery_menu, ID2P(LANG_BATTERY_MENU), 0, Icon_NOICON,
#if defined(BATTERY_CAPACITY_INC) && BATTERY_CAPACITY_INC > 0
            &battery_capacity,
#endif
#if BATTERY_TYPES_COUNT > 1
            &battery_type,
#endif
#ifdef HAVE_USB_CHARGING_ENABLE
            &usb_charging,
#endif
         );
/* Disk */
#ifdef HAVE_DISK_STORAGE
MENUITEM_SETTING(disk_spindown, &global_settings.disk_spindown, NULL);
#endif
#ifdef HAVE_DIRCACHE
static int dircache_callback(int action,const struct menu_item_ex *this_item)
{
    (void)this_item;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM: /* on exit */
            if (global_settings.dircache && !dircache_is_enabled())
            {
                if (dircache_build(0) < 0)
                    splash(HZ*2, ID2P(LANG_PLEASE_REBOOT));
            }
            else if (!global_settings.dircache && dircache_is_enabled())
            {
                dircache_disable();
            }
            break;
    }
    return action;
}
MENUITEM_SETTING(dircache, &global_settings.dircache, dircache_callback);
#endif
#if defined(HAVE_DIRCACHE) || defined(HAVE_DISK_STORAGE)
MAKE_MENU(disk_menu, ID2P(LANG_DISK_MENU), 0, Icon_NOICON,
#ifdef HAVE_DISK_STORAGE
          &disk_spindown,
#endif
#ifdef HAVE_DIRCACHE
            &dircache,
#endif
         );
#endif

/* System menu */
MENUITEM_SETTING(poweroff, &global_settings.poweroff, NULL);

/* Limits menu */
MENUITEM_SETTING(max_files_in_dir, &global_settings.max_files_in_dir, NULL);
MENUITEM_SETTING(max_files_in_playlist, &global_settings.max_files_in_playlist, NULL);
MAKE_MENU(limits_menu, ID2P(LANG_LIMITS_MENU), 0, Icon_NOICON,
           &max_files_in_dir, &max_files_in_playlist);


/* Keyclick menu */
#if CONFIG_CODEC == SWCODEC
MENUITEM_SETTING(keyclick, &global_settings.keyclick, NULL);
MENUITEM_SETTING(keyclick_repeats, &global_settings.keyclick_repeats, NULL);
#ifdef HAVE_HARDWARE_CLICK
MENUITEM_SETTING(keyclick_hardware, &global_settings.keyclick_hardware, NULL);
MAKE_MENU(keyclick_menu, ID2P(LANG_KEYCLICK), 0, Icon_NOICON,
           &keyclick, &keyclick_hardware, &keyclick_repeats);
#else
MAKE_MENU(keyclick_menu, ID2P(LANG_KEYCLICK), 0, Icon_NOICON,
           &keyclick, &keyclick_repeats);
#endif
#endif


#if CONFIG_CODEC == MAS3507D
void dac_line_in(bool enable);
static int linein_callback(int action,const struct menu_item_ex *this_item)
{
    (void)this_item;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM: /* on exit */
#ifndef SIMULATOR
            dac_line_in(global_settings.line_in);
#endif
            break;
    }
    return action;
}
MENUITEM_SETTING(line_in, &global_settings.line_in, linein_callback);
#endif
#if CONFIG_CHARGING
MENUITEM_SETTING(car_adapter_mode, &global_settings.car_adapter_mode, NULL);
#endif
#ifdef IPOD_ACCESSORY_PROTOCOL
MENUITEM_SETTING(serial_bitrate, &global_settings.serial_bitrate, NULL);
#endif
#ifdef HAVE_ACCESSORY_SUPPLY
MENUITEM_SETTING(accessory_supply, &global_settings.accessory_supply, NULL);
#endif
#ifdef HAVE_LINEOUT_POWEROFF
MENUITEM_SETTING(lineout_onoff, &global_settings.lineout_active, NULL);
#endif
MENUITEM_SETTING(start_screen, &global_settings.start_in_screen, NULL);
#ifdef USB_ENABLE_HID
MENUITEM_SETTING(usb_hid, &global_settings.usb_hid, NULL);
MENUITEM_SETTING(usb_keypad_mode, &global_settings.usb_keypad_mode, NULL);
#endif
#if defined(USB_ENABLE_STORAGE) && defined(HAVE_MULTIDRIVE)
MENUITEM_SETTING(usb_skip_first_drive, &global_settings.usb_skip_first_drive, NULL);
#endif

#ifdef HAVE_MORSE_INPUT
MENUITEM_SETTING(morse_input, &global_settings.morse_input, NULL);
#endif

#ifdef HAVE_BUTTON_LIGHT
MENUITEM_SETTING(buttonlight_timeout, &global_settings.buttonlight_timeout, NULL);
#endif

#ifdef HAVE_BUTTONLIGHT_BRIGHTNESS
MENUITEM_SETTING(buttonlight_brightness, &global_settings.buttonlight_brightness, NULL);
#endif

#ifdef HAVE_TOUCHPAD_SENSITIVITY_SETTING
MENUITEM_SETTING(touchpad_sensitivity, &global_settings.touchpad_sensitivity, NULL);
#endif

MAKE_MENU(system_menu, ID2P(LANG_SYSTEM),
          0, Icon_System_menu,
            &start_screen,
#if (BATTERY_CAPACITY_INC > 0) || (BATTERY_TYPES_COUNT > 1)
            &battery_menu,
#endif
#if defined(HAVE_DIRCACHE) || defined(HAVE_DISK_STORAGE)
            &disk_menu,
#endif
            &poweroff,
            &limits_menu,
#ifdef HAVE_MORSE_INPUT
            &morse_input,
#endif
#if CONFIG_CODEC == MAS3507D
            &line_in,
#endif
#if CONFIG_CHARGING
            &car_adapter_mode,
#endif
#ifdef IPOD_ACCESSORY_PROTOCOL
            &serial_bitrate,
#endif
#ifdef HAVE_ACCESSORY_SUPPLY
            &accessory_supply,
#endif
#ifdef HAVE_LINEOUT_POWEROFF
            &lineout_onoff,
#endif
#ifdef HAVE_BUTTON_LIGHT
            &buttonlight_timeout,
#endif
#ifdef HAVE_BUTTONLIGHT_BRIGHTNESS
            &buttonlight_brightness,
#endif
#if CONFIG_CODEC == SWCODEC
            &keyclick_menu,
#endif
#ifdef HAVE_TOUCHPAD_SENSITIVITY_SETTING
            &touchpad_sensitivity,
#endif
#ifdef USB_ENABLE_HID
            &usb_hid,
            &usb_keypad_mode,
#endif
#if defined(USB_ENABLE_STORAGE) && defined(HAVE_MULTIDRIVE)
            &usb_skip_first_drive,
#endif
         );

/*    SYSTEM MENU                  */
/***********************************/


/***********************************/
/*    BOOKMARK MENU                */
static int bmark_callback(int action,const struct menu_item_ex *this_item)
{
    (void)this_item;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM: /* on exit */
            if(global_settings.autocreatebookmark ==  BOOKMARK_RECENT_ONLY_YES ||
               global_settings.autocreatebookmark ==  BOOKMARK_RECENT_ONLY_ASK)
            {
                if(global_settings.usemrb == BOOKMARK_NO)
                    global_settings.usemrb = BOOKMARK_YES;

            }
            break;
    }
    return action;
}
MENUITEM_SETTING(autocreatebookmark,
                 &global_settings.autocreatebookmark, bmark_callback);
MENUITEM_SETTING(autoupdatebookmark, &global_settings.autoupdatebookmark, NULL);
MENUITEM_SETTING(autoloadbookmark, &global_settings.autoloadbookmark, NULL);
MENUITEM_SETTING(usemrb, &global_settings.usemrb, NULL);
MAKE_MENU(bookmark_settings_menu, ID2P(LANG_BOOKMARK_SETTINGS), 0,
          Icon_Bookmark,
          &autocreatebookmark, &autoupdatebookmark, &autoloadbookmark, &usemrb);
/*    BOOKMARK MENU                */
/***********************************/

/***********************************/
/*    AUTORESUME MENU              */
#ifdef HAVE_TAGCACHE
#if CONFIG_CODEC == SWCODEC 

static int autoresume_callback(int action, const struct menu_item_ex *this_item)
{
    (void)this_item;

    if (action == ACTION_EXIT_MENUITEM  /* on exit */
        && global_settings.autoresume_enable
        && !tagcache_is_usable())
    {
        static const char *lines[] = {ID2P(LANG_TAGCACHE_BUSY),
                                      ID2P(LANG_TAGCACHE_FORCE_UPDATE)};
        static const struct text_message message = {lines, 2};
        
        if (gui_syncyesno_run(&message, NULL, NULL) == YESNO_YES)
            tagcache_rebuild_with_splash();
    }
    return action;
}

static int autoresume_nexttrack_callback(int action,
                                         const struct menu_item_ex *this_item)
{
    (void)this_item;
    static int oldval = 0;
    switch (action)
    {
        case ACTION_ENTER_MENUITEM:
            oldval = global_settings.autoresume_automatic;
            break;
        case ACTION_EXIT_MENUITEM:
            if (global_settings.autoresume_automatic == AUTORESUME_NEXTTRACK_CUSTOM
                && kbd_input ((char*) &global_settings.autoresume_paths,
                              MAX_PATHNAME+1) < 0)
            {
                global_settings.autoresume_automatic = oldval;
            }
    }
    return action;
}

MENUITEM_SETTING(autoresume_enable, &global_settings.autoresume_enable,
                 autoresume_callback);
MENUITEM_SETTING(autoresume_automatic, &global_settings.autoresume_automatic,
                 autoresume_nexttrack_callback);

MAKE_MENU(autoresume_menu, ID2P(LANG_AUTORESUME),
          0, Icon_NOICON,
          &autoresume_enable, &autoresume_automatic);

#endif /* CONFIG_CODEC == SWCODEC */
#endif /* HAVE_TAGCACHE */
/*    AUTORESUME MENU              */
/***********************************/

/***********************************/
/*    VOICE MENU                   */
static int talk_callback(int action,const struct menu_item_ex *this_item);
MENUITEM_SETTING(talk_menu_item, &global_settings.talk_menu, NULL);
MENUITEM_SETTING(talk_dir_item, &global_settings.talk_dir, NULL);
MENUITEM_SETTING(talk_dir_clip_item, &global_settings.talk_dir_clip, talk_callback);
MENUITEM_SETTING(talk_file_item, &global_settings.talk_file, NULL);
MENUITEM_SETTING(talk_file_clip_item, &global_settings.talk_file_clip, talk_callback);
static int talk_callback(int action,const struct menu_item_ex *this_item)
{
    static int oldval = 0;
    switch (action)
    {
        case ACTION_ENTER_MENUITEM:
            oldval = global_settings.talk_file_clip;
            break;
        case ACTION_EXIT_MENUITEM:
#ifdef HAVE_CROSSFADE
            audio_set_crossfade(global_settings.crossfade);
#endif
            if (this_item == &talk_dir_clip_item)
                break;
            if (!oldval && global_settings.talk_file_clip)
            {
                /* force reload if newly talking thumbnails,
                because the clip presence is cached only if enabled */
                reload_directory();
            }
            break;
    }
    return action;
}
MENUITEM_SETTING(talk_filetype_item, &global_settings.talk_filetype, NULL);
MENUITEM_SETTING(talk_battery_level_item,
                 &global_settings.talk_battery_level, NULL);
MAKE_MENU(voice_settings_menu, ID2P(LANG_VOICE), 0, Icon_Voice,
          &talk_menu_item, &talk_dir_item, &talk_dir_clip_item,
          &talk_file_item, &talk_file_clip_item, &talk_filetype_item,
          &talk_battery_level_item);
/*    VOICE MENU                   */
/***********************************/


/***********************************/
/*    HOTKEY MENU                  */
#ifdef HAVE_HOTKEY
MENUITEM_SETTING(hotkey_wps_item, &global_settings.hotkey_wps, NULL);
MENUITEM_SETTING(hotkey_tree_item, &global_settings.hotkey_tree, NULL);
MAKE_MENU(hotkey_menu, ID2P(LANG_HOTKEY), 0, Icon_NOICON,
            &hotkey_wps_item, &hotkey_tree_item);
#endif /*have_hotkey */
/*    HOTKEY MENU                  */
/***********************************/


/***********************************/
/*    SETTINGS MENU                */

static struct browse_folder_info langs = { LANG_DIR, SHOW_LNG };

MENUITEM_FUNCTION(browse_langs, MENU_FUNC_USEPARAM, ID2P(LANG_LANGUAGE),
                  browse_folder, (void*)&langs, NULL, Icon_Language);

MAKE_MENU(settings_menu_item, ID2P(LANG_GENERAL_SETTINGS), 0,
          Icon_General_settings_menu,
          &playlist_settings, &file_menu,
#ifdef HAVE_TAGCACHE
          &tagcache_menu,
#endif
          &display_menu, &system_menu,
          &bookmark_settings_menu,
#ifdef HAVE_TAGCACHE
#if CONFIG_CODEC == SWCODEC 
          &autoresume_menu,
#endif          
#endif
          &browse_langs, &voice_settings_menu,
#ifdef HAVE_HOTKEY
          &hotkey_menu,
#endif
          );
/*    SETTINGS MENU                */
/***********************************/
