/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2009 Thomas Martitz
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
#ifndef __STATUSBAR_SKINNED_H__
#define __STATUSBAR_SKINNED_H__

#define DEFAULT_UPDATE_DELAY (HZ/7)

#ifdef HAVE_LCD_BITMAP

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "icon.h"
#include "skin_engine/skin_engine.h"

void sb_skin_data_load(enum screen_type screen, const char *buf, bool isfile);

char* sb_create_from_settings(enum screen_type screen);
void sb_skin_init(void) INIT_ATTR;
void sb_set_info_vp(enum screen_type screen, OFFSETTYPE(char*) label);
struct viewport *sb_skin_get_info_vp(enum screen_type screen);
void sb_skin_update(enum screen_type screen, bool force);

void sb_skin_set_update_delay(int delay);
bool sb_set_title_text(char* title, enum themable_icons icon, enum screen_type screen);
void sb_skin_has_title(enum screen_type screen);
const char* sb_get_title(enum screen_type screen);
enum themable_icons sb_get_icon(enum screen_type screen);

#ifdef HAVE_TOUCHSCREEN
void sb_bypass_touchregions(bool enable);
int sb_touch_to_button(int context);
#endif

#if (LCD_DEPTH > 1) || (defined(HAVE_REMOTE_LCD) && LCD_REMOTE_DEPTH > 1)
int sb_get_backdrop(enum screen_type screen);
#endif
int sb_preproccess(enum screen_type screen, struct wps_data *data);
int sb_postproccess(enum screen_type screen, struct wps_data *data);

#else /* CHARCELL */
#define sb_skin_init()
#define sb_skin_data_load(a,b,c)
#define sb_skin_set_update_delay(a)
#define sb_skin_set_state(a,b)
#define sb_create_from_settings NULL
#define sb_preproccess NULL
#define sb_postproccess NULL
#endif
void do_sbs_update_callback(void *param);
#endif /* __STATUSBAR_SKINNED_H__ */
