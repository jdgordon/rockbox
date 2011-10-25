 /***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2005 by Björn Stenberg
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
#include <stdlib.h>
#include <file.h>
#include <dir.h>
#include <string.h>
#include <kernel.h>
#include <lcd.h>
#include <debug.h>
#include <font.h>
#include <limits.h>
#include "bookmark.h"
#include "tree.h"
#include "core_alloc.h"
#include "settings.h"
#include "filetypes.h"
#include "talk.h"
#include "playlist.h"
#include "lang.h"
#include "language.h"
#include "screens.h"
#include "plugin.h"
#include "rolo.h"
#include "splash.h"
#include "cuesheet.h"
#include "filetree.h"
#include "misc.h"
#include "strnatcmp.h"
#include "playlist_viewer.h"
#ifdef HAVE_LCD_BITMAP
#include "keyboard.h"
#endif

#if CONFIG_TUNER
#include "radio.h"
#endif
#include "wps.h"

static int compare_sort_dir; /* qsort key for sorting directories */

int ft_build_playlist(struct tree_context* c, int start_index)
{
    int i;
    int start=start_index;

    tree_lock_cache(c);
    struct entry *entries = tree_get_entries(c);

    for(i = 0;i < c->filesindir;i++)
    {
        if((entries[i].attr & FILE_ATTR_MASK) == FILE_ATTR_AUDIO)
        {
            if (playlist_add(entries[i].name) < 0)
                break;
        }
        else
        {
            /* Adjust the start index when se skip non-MP3 entries */
            if(i < start)
                start_index--;
        }
    }

    tree_unlock_cache(c);

    return start_index;
}

/* Start playback of a playlist, checking for bookmark autoload, modified
 * playlists, etc., as required. Returns false if playback wasn't started,
 * or started via bookmark autoload, true otherwise.
 *
 * Pointers to both the full pathname and the separated parts needed to
 * avoid allocating yet another path buffer on the stack (and save some 
 * code; the caller typically needs to create the full pathname anyway)...
 */
bool ft_play_playlist(char* pathname, char* dirname, char* filename)
{
    if (global_settings.party_mode && audio_status()) 
    {
        splash(HZ, ID2P(LANG_PARTY_MODE));
        return false;
    }

    if (bookmark_autoload(pathname))
    {
        return false;
    }

    splash(0, ID2P(LANG_WAIT));

    /* about to create a new current playlist...
       allow user to cancel the operation */
    if (!warn_on_pl_erase())
        return false;

    if (playlist_create(dirname, filename) != -1)
    {
        if (global_settings.playlist_shuffle)
        {
            playlist_shuffle(current_tick, -1);
        }
        
        playlist_start(0, 0);
        return true;
    }
    
    return false;
}

/* walk a directory and check all entries if a .talk file exists */
static void check_file_thumbnails(struct tree_context* c)
{
    int i;
    struct dirent *entry;
    struct entry* entries;
    DIR *dir;

    dir = opendir(c->currdir);
    if(!dir)
        return;
    /* mark all files as non talking, except the .talk ones */
    entries = tree_get_entries(c);
    tree_lock_cache(c);
    for (i=0; i < c->filesindir; i++)
    {
        if (entries[i].attr & ATTR_DIRECTORY)
            continue; /* we're not touching directories */

        if (strcasecmp(file_thumbnail_ext,
            &entries[i].name[strlen(entries[i].name)
                              - strlen(file_thumbnail_ext)]))
        {   /* no .talk file */
            entries[i].attr &= ~FILE_ATTR_THUMBNAIL; /* clear */
        }
        else
        {   /* .talk file, we later let them speak themselves */
            entries[i].attr |= FILE_ATTR_THUMBNAIL; /* set */
        }
    }

    while((entry = readdir(dir)) != 0) /* walk directory */
    {
        int ext_pos;
        struct dirinfo info = dir_get_info(dir, entry);
        ext_pos = strlen((char *)entry->d_name) - strlen(file_thumbnail_ext);
        if (ext_pos <= 0 /* too short to carry ".talk" */
            || (info.attribute & ATTR_DIRECTORY) /* no file */
            || strcasecmp((char *)&entry->d_name[ext_pos], file_thumbnail_ext))
        {   /* or doesn't end with ".talk", no candidate */
            continue;
        }

        /* terminate the (disposable) name in dir buffer,
           this truncates off the ".talk" without needing an extra buffer */
        entry->d_name[ext_pos] = '\0';

        /* search corresponding file in dir cache */
        for (i=0; i < c->filesindir; i++)
        {
            if (!strcasecmp(entries[i].name, (char *)entry->d_name))
            {   /* match */
                entries[i].attr |= FILE_ATTR_THUMBNAIL; /* set the flag */
                break; /* exit search loop, because we found it */
            }
        }
    }
    tree_unlock_cache(c);
    closedir(dir);
}

/* support function for qsort() */
static int compare(const void* p1, const void* p2)
{
    struct entry* e1 = (struct entry*)p1;
    struct entry* e2 = (struct entry*)p2;
    int criteria;

    if (e1->attr & ATTR_DIRECTORY && e2->attr & ATTR_DIRECTORY)
    {   /* two directories */
        criteria = compare_sort_dir;

#ifdef HAVE_MULTIVOLUME
        if (e1->attr & ATTR_VOLUME || e2->attr & ATTR_VOLUME)
        {   /* a volume identifier is involved */
            if (e1->attr & ATTR_VOLUME && e2->attr & ATTR_VOLUME)
                criteria = SORT_ALPHA; /* two volumes: sort alphabetically */
            else /* only one is a volume: volume first */
                return (e2->attr & ATTR_VOLUME) - (e1->attr & ATTR_VOLUME);
        }
#endif

    }
    else if (!(e1->attr & ATTR_DIRECTORY) && !(e2->attr & ATTR_DIRECTORY))
    {   /* two files */
        criteria = global_settings.sort_file;
    }
    else /* dir and file, dir goes first */
        return (e2->attr & ATTR_DIRECTORY) - (e1->attr & ATTR_DIRECTORY);

    switch(criteria)
    {
        case SORT_TYPE:
        case SORT_TYPE_REVERSED:
        {
            int t1 = e1->attr & FILE_ATTR_MASK;
            int t2 = e2->attr & FILE_ATTR_MASK;

            if (!t1) /* unknown type */
                t1 = INT_MAX; /* gets a high number, to sort after known */
            if (!t2) /* unknown type */
                t2 = INT_MAX; /* gets a high number, to sort after known */

            if (t1 != t2) /* if different */
                return (t1 - t2) * (criteria == SORT_TYPE_REVERSED ? -1 : 1);
            /* else fall through to alphabetical sorting */
        }

        case SORT_DATE:
        case SORT_DATE_REVERSED:
            /* Ignore SORT_TYPE */
            if (criteria == SORT_DATE || criteria == SORT_DATE_REVERSED)
            {
                if (e1->time_write != e2->time_write)
                    return (e1->time_write - e2->time_write)
                           * (criteria == SORT_DATE_REVERSED ? -1 : 1);
                /* else fall through to alphabetical sorting */
            }

        case SORT_ALPHA:
        case SORT_ALPHA_REVERSED:
        {
            if (global_settings.sort_case)
            {
                if (global_settings.interpret_numbers == SORT_INTERPRET_AS_NUMBER)
                    return strnatcmp(e1->name, e2->name)
                           * (criteria == SORT_ALPHA_REVERSED ? -1 : 1);
                else
                    return strncmp(e1->name, e2->name, MAX_PATH)
                           * (criteria == SORT_ALPHA_REVERSED ? -1 : 1);
            }
            else
            {
                if (global_settings.interpret_numbers == SORT_INTERPRET_AS_NUMBER)
                    return strnatcasecmp(e1->name, e2->name)
                           * (criteria == SORT_ALPHA_REVERSED ? -1 : 1);
                else
                    return strncasecmp(e1->name, e2->name, MAX_PATH)
                           * (criteria == SORT_ALPHA_REVERSED ? -1 : 1);
            }
        }

    }
    return 0; /* never reached */
}

/* load and sort directory into the tree's cache. returns NULL on failure. */
int ft_load(struct tree_context* c, const char* tempdir)
{
    int files_in_dir = 0;
    int name_buffer_used = 0;
    struct dirent *entry;
    bool (*callback_show_item)(char *, int, struct tree_context *) = NULL;
    DIR *dir;

    if (tempdir)
        dir = opendir(tempdir);
    else
    {
        dir = opendir(c->currdir);
        callback_show_item = c->browse? c->browse->callback_show_item: NULL;
    }
    if(!dir)
        return -1; /* not a directory */

    c->dirsindir = 0;
    c->dirfull = false;

    tree_lock_cache(c);
    while ((entry = readdir(dir))) {
        int len;
        struct dirinfo info;
        struct entry* dptr = tree_get_entry_at(c, files_in_dir);
        if (!entry)
            break;

        info = dir_get_info(dir, entry);
        len = strlen((char *)entry->d_name);

        /* skip directories . and .. */
        if ((info.attribute & ATTR_DIRECTORY) &&
            (((len == 1) && (!strncmp((char *)entry->d_name, ".", 1))) ||
             ((len == 2) && (!strncmp((char *)entry->d_name, "..", 2))))) {
            continue;
        }

        /* Skip FAT volume ID */
        if (info.attribute & ATTR_VOLUME_ID) {
            continue;
        }

        /* filter out dotfiles and hidden files */
        if (*c->dirfilter != SHOW_ALL &&
            ((entry->d_name[0]=='.') ||
            (info.attribute & ATTR_HIDDEN))) {
            continue;
        }

        dptr->attr = info.attribute;

        /* check for known file types */
        if ( !(dptr->attr & ATTR_DIRECTORY) )
            dptr->attr |= filetype_get_attr((char *)entry->d_name);

        /* filter out non-visible files */
        if ((!(dptr->attr & ATTR_DIRECTORY) && (
            (*c->dirfilter == SHOW_PLAYLIST &&
             (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_M3U) ||
            ((*c->dirfilter == SHOW_MUSIC &&
             (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_AUDIO) &&
             (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_M3U) ||
            (*c->dirfilter == SHOW_SUPPORTED && !filetype_supported(dptr->attr)))) ||
            (*c->dirfilter == SHOW_WPS && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_WPS) ||
#ifdef HAVE_LCD_BITMAP
            (*c->dirfilter == SHOW_FONT && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_FONT) ||
            (*c->dirfilter == SHOW_SBS  && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_SBS) ||
#if CONFIG_TUNER
            (*c->dirfilter == SHOW_FMS  && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_FMS) ||
#endif
#endif
#ifdef HAVE_REMOTE_LCD
            (*c->dirfilter == SHOW_RWPS && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_RWPS) ||
            (*c->dirfilter == SHOW_RSBS && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_RSBS) ||
#if CONFIG_TUNER
            (*c->dirfilter == SHOW_RFMS  && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_RFMS) ||
#endif
#endif
#if CONFIG_TUNER
            (*c->dirfilter == SHOW_FMR && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_FMR) ||
#endif
            (*c->dirfilter == SHOW_M3U && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_M3U) ||
            (*c->dirfilter == SHOW_CFG && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_CFG) ||
            (*c->dirfilter == SHOW_LNG && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_LNG) ||
            (*c->dirfilter == SHOW_MOD && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_MOD) ||
            (*c->dirfilter == SHOW_PLUGINS && (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_ROCK &&
                                              (dptr->attr & FILE_ATTR_MASK) != FILE_ATTR_LUA) ||
            (callback_show_item && !callback_show_item(entry->d_name, dptr->attr, c)))
        {
            continue;
        }

        if ((len > c->cache.name_buffer_size - name_buffer_used - 1) ||
            (files_in_dir >= c->cache.max_entries)) {
            /* Tell the world that we ran out of buffer space */
            c->dirfull = true;
            break;
        }

        ++files_in_dir;

        dptr->name = core_get_data(c->cache.name_buffer_handle)+name_buffer_used;
        dptr->time_write =
            (long)info.wrtdate<<16 |
            (long)info.wrttime; /* in one # */
        strcpy(dptr->name, (char *)entry->d_name);
        name_buffer_used += len + 1;

        if (dptr->attr & ATTR_DIRECTORY) /* count the remaining dirs */
            c->dirsindir++;
    }
    c->filesindir = files_in_dir;
    c->dirlength = files_in_dir;
    closedir(dir);

    compare_sort_dir = c->sort_dir;
    qsort(tree_get_entries(c), files_in_dir, sizeof(struct entry), compare);

    /* If thumbnail talking is enabled, make an extra run to mark files with
       associated thumbnails, so we don't do unsuccessful spinups later. */
    if (global_settings.talk_file_clip)
        check_file_thumbnails(c); /* map .talk to ours */

    tree_unlock_cache(c);
    return 0;
}
#ifdef HAVE_LCD_BITMAP
static void ft_load_font(char *file)
{
    int current_font_id;
    enum screen_type screen = SCREEN_MAIN;
#if NB_SCREENS > 1
    MENUITEM_STRINGLIST(menu, ID2P(LANG_CUSTOM_FONT), NULL, 
                        ID2P(LANG_MAIN_SCREEN), ID2P(LANG_REMOTE_SCREEN))
    switch (do_menu(&menu, NULL, NULL, false))
    {
        case 0: /* main lcd */
            screen = SCREEN_MAIN;
            set_file(file, (char *)global_settings.font_file, MAX_FILENAME);
            break;
        case 1: /* remote */
            screen = SCREEN_REMOTE;
            set_file(file, (char *)global_settings.remote_font_file, MAX_FILENAME);
            break;
    }
#else
    set_file(file, (char *)global_settings.font_file, MAX_FILENAME);
#endif
    splash(0, ID2P(LANG_WAIT));
    current_font_id = global_status.font_id[screen];
    if (current_font_id >= 0)
        font_unload(current_font_id);
    global_status.font_id[screen] = font_load(file);
    viewportmanager_theme_changed(THEME_UI_VIEWPORT);
}    
#endif

int ft_enter(struct tree_context* c)
{
    int rc = GO_TO_PREVIOUS;
    char buf[MAX_PATH];
    struct entry* file = tree_get_entry_at(c, c->selected_item);
    int file_attr = file->attr;

    if (c->currdir[1])
        snprintf(buf,sizeof(buf),"%s/%s",c->currdir, file->name);
    else
        snprintf(buf,sizeof(buf),"/%s",file->name);

    if (file_attr & ATTR_DIRECTORY) {
        memcpy(c->currdir, buf, sizeof(c->currdir));
        if ( c->dirlevel < MAX_DIR_LEVELS )
            c->selected_item_history[c->dirlevel] = c->selected_item;
        c->dirlevel++;
        c->selected_item=0;
    }
    else {
        int seed = current_tick;
        bool play = false;
        int start_index=0;

        switch ( file_attr & FILE_ATTR_MASK ) {
            case FILE_ATTR_M3U:
                if (!bookmark_autoload(buf))
                    playlist_viewer_ex(buf);
                break;

            case FILE_ATTR_AUDIO:
                if (bookmark_autoload(c->currdir))
                    break;

                splash(0, ID2P(LANG_WAIT));

                /* about to create a new current playlist...
                   allow user to cancel the operation */
                if (!warn_on_pl_erase())
                    break;

                if (global_settings.party_mode && audio_status()) 
                {
                    playlist_insert_track(NULL, buf,
                                          PLAYLIST_INSERT_LAST, true, true);
                    splash(HZ, ID2P(LANG_QUEUE_LAST));
                }
                else if (playlist_create(c->currdir, NULL) != -1)
                {
                    start_index = ft_build_playlist(c, c->selected_item);
                    if (global_settings.playlist_shuffle)
                    {
                        start_index = playlist_shuffle(seed, start_index);

                        /* when shuffling dir.: play all files
                           even if the file selected by user is
                           not the first one */
                        if (!global_settings.play_selected)
                            start_index = 0;
                    }

                    playlist_start(start_index, 0);
                    play = true;
                }
                break;

#if CONFIG_TUNER
                /* fmr preset file */
            case FILE_ATTR_FMR:
                splash(0, ID2P(LANG_WAIT));

                /* Preset inside the default folder. */
                if(!strncasecmp(FMPRESET_PATH, buf, strlen(FMPRESET_PATH)))
                {
                    set_file(buf, global_settings.fmr_file, MAX_FILENAME);
                    radio_load_presets(global_settings.fmr_file);
                }
                /*
                 * Preset outside default folder, we can choose such only
                 * if we are out of the radio screen, so the check for the
                 * radio status isn't neccessary
                 */
                else
                {
                    radio_load_presets(buf);
                }
                rc = GO_TO_FM;

                break;
            case FILE_ATTR_FMS:
                splash(0, ID2P(LANG_WAIT));
                set_file(buf, (char *)global_settings.fms_file, MAX_FILENAME);
                settings_apply_skins();
                break;
#ifdef HAVE_REMOTE_LCD
            case FILE_ATTR_RFMS:
                splash(0, ID2P(LANG_WAIT));
                set_file(buf, (char *)global_settings.rfms_file, MAX_FILENAME);
                settings_apply_skins();
                break;
#endif
#endif

#ifdef HAVE_LCD_BITMAP
            case FILE_ATTR_SBS:
                splash(0, ID2P(LANG_WAIT));
                set_file(buf, (char *)global_settings.sbs_file, MAX_FILENAME);
                settings_apply_skins();
                break;
#endif
#ifdef HAVE_REMOTE_LCD
            case FILE_ATTR_RSBS:
                splash(0, ID2P(LANG_WAIT));
                set_file(buf, (char *)global_settings.rsbs_file, MAX_FILENAME);
                settings_apply_skins();
                break;
#endif
                /* wps config file */
            case FILE_ATTR_WPS:
                splash(0, ID2P(LANG_WAIT));
                set_file(buf, (char *)global_settings.wps_file,
                         MAX_FILENAME);
                settings_apply_skins();
                break;

#if defined(HAVE_REMOTE_LCD) && (NB_SCREENS > 1)
                /* remote-wps config file */
            case FILE_ATTR_RWPS:
                splash(0, ID2P(LANG_WAIT));
                set_file(buf, (char *)global_settings.rwps_file,
                         MAX_FILENAME);
                settings_apply_skins();
                break;
#endif

            case FILE_ATTR_CFG:
                splash(0, ID2P(LANG_WAIT));
                if (!settings_load_config(buf,true))
                    break;
                splash(HZ, ID2P(LANG_SETTINGS_LOADED));
                break;

            case FILE_ATTR_BMARK:
                splash(0, ID2P(LANG_WAIT));
                bookmark_load(buf, false);
                rc = GO_TO_FILEBROWSER;
                break;

            case FILE_ATTR_LNG:
                splash(0, ID2P(LANG_WAIT));
                if (lang_core_load(buf))
                {
                    splash(HZ, ID2P(LANG_FAILED));
                    break;
                }
                set_file(buf, (char *)global_settings.lang_file,
                        MAX_FILENAME);
                talk_init(); /* use voice of same language */
                viewportmanager_theme_changed(THEME_LANGUAGE);
                settings_apply_skins();
                splash(HZ, ID2P(LANG_LANGUAGE_LOADED));
                break;

#ifdef HAVE_LCD_BITMAP
            case FILE_ATTR_FONT:
                ft_load_font(buf);
                break;

            case FILE_ATTR_KBD:
                splash(0, ID2P(LANG_WAIT));
                if (!load_kbd(buf))
                    splash(HZ, ID2P(LANG_KEYBOARD_LOADED));
                set_file(buf, (char *)global_settings.kbd_file, MAX_FILENAME);
                break;
#endif

#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
                /* firmware file */
            case FILE_ATTR_MOD:
                splash(0, ID2P(LANG_WAIT));
                audio_hard_stop();
                rolo_load(buf);
                break;
#endif

                /* plugin file */
            case FILE_ATTR_ROCK:
            case FILE_ATTR_LUA:
            {
                char *plugin = buf, *argument = NULL, lua_path[MAX_PATH];
                int ret;

                if ((file_attr & FILE_ATTR_MASK) == FILE_ATTR_LUA) {
                    snprintf(lua_path, sizeof(lua_path)-1, "%s/lua.rock", VIEWERS_DIR); /* Use a #define here ? */
                    plugin = lua_path;
                    argument = buf;
                }

                if (global_settings.party_mode && audio_status()) {
                    splash(HZ, ID2P(LANG_PARTY_MODE));
                    break;
                }
                ret = plugin_load(plugin, argument);
                switch (ret)
                {
                    case PLUGIN_GOTO_WPS:
                        play = true;
                        break;
                    case PLUGIN_USB_CONNECTED:
                        if(*c->dirfilter > NUM_FILTER_MODES)
                            /* leave sub-browsers after usb, doing
                               otherwise might be confusing to the user */
                            rc = GO_TO_ROOT;
                        else
                            rc = GO_TO_FILEBROWSER;
                        break;
                    /*
                    case PLUGIN_ERROR:
                    case PLUGIN_OK:
                    */
                    default:
                        break;
                }
                break;
            }
            case FILE_ATTR_CUE:
                display_cuesheet_content(buf);
                break;

            default:
            {
                const char* plugin;

                if (global_settings.party_mode && audio_status()) {
                    splash(HZ, ID2P(LANG_PARTY_MODE));
                    break;
                }

                struct entry* file = tree_get_entry_at(c, c->selected_item);
                plugin = filetype_get_plugin(file);
                if (plugin)
                {
                    switch (plugin_load(plugin,buf))
                    {
                        case PLUGIN_USB_CONNECTED:
                            rc = GO_TO_FILEBROWSER;
                            break;
                        case PLUGIN_GOTO_WPS:
                            rc = GO_TO_WPS;
                            break;
                        /*
                        case PLUGIN_OK:
                        case PLUGIN_ERROR:
                        */
                        default:
                            break;
                    }
                }
                break;
            }
        }

        if ( play ) {
            /* the resume_index must always be the index in the
               shuffled list in case shuffle is enabled */
            global_status.resume_index = start_index;
            global_status.resume_offset = 0;
            status_save();
            rc = GO_TO_WPS;
        }
        else {
            if (*c->dirfilter > NUM_FILTER_MODES &&
                *c->dirfilter != SHOW_CFG &&
                *c->dirfilter != SHOW_FONT &&
                *c->dirfilter != SHOW_PLUGINS)
            {
                rc = GO_TO_ROOT;
            }
        }
    }
    return rc;
}

int ft_exit(struct tree_context* c)
{
    extern char lastfile[]; /* from tree.c */
    char buf[MAX_PATH];
    int rc = 0;
    bool exit_func = false;

    int i = strlen(c->currdir);
    if (i>1) {
        while (c->currdir[i-1]!='/')
            i--;
        strcpy(buf,&c->currdir[i]);
        if (i==1)
            c->currdir[i]=0;
        else
            c->currdir[i-1]=0;

        if (*c->dirfilter > NUM_FILTER_MODES && c->dirlevel < 1)
            exit_func = true;

        c->dirlevel--;
        if ( c->dirlevel < MAX_DIR_LEVELS )
            c->selected_item=c->selected_item_history[c->dirlevel];
        else
            c->selected_item=0;

        /* if undefined position */
        if (c->selected_item == -1)
            strcpy(lastfile, buf);
    }
    else
    {
        if (*c->dirfilter > NUM_FILTER_MODES && c->dirlevel < 1)
            exit_func = true;
    }

    if (exit_func)
        rc = 3;

    return rc;
}
