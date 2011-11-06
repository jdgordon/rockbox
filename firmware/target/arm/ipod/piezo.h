/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2006-2007 Robert Keevil
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

void piezo_init(void);
void piezo_play(unsigned short inv_freq, unsigned char form);
void piezo_play_for_tick(unsigned short inv_freq,
                    unsigned char form, unsigned int dur);
void piezo_play_for_usec(unsigned short inv_freq,
                    unsigned char form, unsigned int dur);
void piezo_stop(void);
void piezo_clear(void);
bool piezo_busy(void);
unsigned int piezo_hz(unsigned int hz);
void piezo_button_beep(bool beep, bool force);
