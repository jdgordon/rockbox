/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2005 Dave Chapman
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#ifndef _SUDOKU_H
#define _SUDOKU_H

#include "plugin.h"

#define GAME_FILE         PLUGIN_DIR "/sudoku.ss"

/* variable button definitions */
#if CONFIG_KEYPAD == RECORDER_PAD
#define SUDOKU_BUTTON_QUIT BUTTON_OFF
#define SUDOKU_BUTTON_UP BUTTON_UP
#define SUDOKU_BUTTON_DOWN BUTTON_DOWN
#define SUDOKU_BUTTON_LEFT BUTTON_LEFT
#define SUDOKU_BUTTON_RIGHT BUTTON_RIGHT
#define SUDOKU_BUTTON_TOGGLE BUTTON_PLAY
#define SUDOKU_BUTTON_MENU BUTTON_F1
#define SUDOKU_BUTTON_POSSIBLE BUTTON_F2

#elif CONFIG_KEYPAD == ARCHOS_AV300_PAD
#define SUDOKU_BUTTON_QUIT BUTTON_OFF
#define SUDOKU_BUTTON_UP BUTTON_UP
#define SUDOKU_BUTTON_DOWN BUTTON_DOWN
#define SUDOKU_BUTTON_LEFT BUTTON_LEFT
#define SUDOKU_BUTTON_RIGHT BUTTON_RIGHT
#define SUDOKU_BUTTON_TOGGLE BUTTON_SELECT
#define SUDOKU_BUTTON_MENU BUTTON_F1
#define SUDOKU_BUTTON_POSSIBLE BUTTON_F2

#elif CONFIG_KEYPAD == ONDIO_PAD
#define SUDOKU_BUTTON_QUIT BUTTON_OFF
#define SUDOKU_BUTTON_UP BUTTON_UP
#define SUDOKU_BUTTON_DOWN BUTTON_DOWN
#define SUDOKU_BUTTON_LEFT BUTTON_LEFT
#define SUDOKU_BUTTON_RIGHT BUTTON_RIGHT
#define SUDOKU_BUTTON_ALTTOGGLE (BUTTON_MENU | BUTTON_DOWN)
#define SUDOKU_BUTTON_TOGGLE_PRE BUTTON_MENU
#define SUDOKU_BUTTON_TOGGLE (BUTTON_MENU | BUTTON_REL)
#define SUDOKU_BUTTON_MENU_PRE BUTTON_MENU
#define SUDOKU_BUTTON_MENU (BUTTON_MENU | BUTTON_REPEAT)
#define SUDOKU_BUTTON_POSSIBLE (BUTTON_MENU | BUTTON_LEFT)

#elif (CONFIG_KEYPAD == IRIVER_H100_PAD) || \
      (CONFIG_KEYPAD == IRIVER_H300_PAD)
#define SUDOKU_BUTTON_QUIT BUTTON_OFF
#define SUDOKU_BUTTON_UP BUTTON_UP
#define SUDOKU_BUTTON_DOWN BUTTON_DOWN
#define SUDOKU_BUTTON_LEFT BUTTON_LEFT
#define SUDOKU_BUTTON_RIGHT BUTTON_RIGHT
#define SUDOKU_BUTTON_ALTTOGGLE BUTTON_ON
#define SUDOKU_BUTTON_TOGGLE BUTTON_SELECT
#define SUDOKU_BUTTON_MENU BUTTON_MODE
#define SUDOKU_BUTTON_POSSIBLE BUTTON_REC

#elif (CONFIG_KEYPAD == IPOD_4G_PAD) || \
      (CONFIG_KEYPAD == IPOD_3G_PAD) || \
      (CONFIG_KEYPAD == IPOD_1G2G_PAD)
#define SUDOKU_BUTTON_MENU       BUTTON_MENU
#define SUDOKU_BUTTON_LEFT       BUTTON_SCROLL_BACK
#define SUDOKU_BUTTON_RIGHT      BUTTON_SCROLL_FWD
#define SUDOKU_BUTTON_ALTTOGGLE  BUTTON_SELECT
#define SUDOKU_BUTTON_TOGGLE     BUTTON_RIGHT
#define SUDOKU_BUTTON_TOGGLEBACK BUTTON_LEFT
#define SUDOKU_BUTTON_POSSIBLE   BUTTON_PLAY

#elif (CONFIG_KEYPAD == IAUDIO_X5M5_PAD)
#define SUDOKU_BUTTON_QUIT BUTTON_POWER
#define SUDOKU_BUTTON_UP BUTTON_UP
#define SUDOKU_BUTTON_DOWN BUTTON_DOWN
#define SUDOKU_BUTTON_LEFT BUTTON_LEFT
#define SUDOKU_BUTTON_RIGHT BUTTON_RIGHT
#define SUDOKU_BUTTON_TOGGLE BUTTON_SELECT
#define SUDOKU_BUTTON_MENU BUTTON_PLAY
#define SUDOKU_BUTTON_POSSIBLE BUTTON_REC

#elif (CONFIG_KEYPAD == GIGABEAT_PAD)
#define SUDOKU_BUTTON_QUIT BUTTON_POWER
#define SUDOKU_BUTTON_UP BUTTON_UP
#define SUDOKU_BUTTON_DOWN BUTTON_DOWN
#define SUDOKU_BUTTON_LEFT BUTTON_LEFT
#define SUDOKU_BUTTON_RIGHT BUTTON_RIGHT
#define SUDOKU_BUTTON_TOGGLE BUTTON_SELECT
#define SUDOKU_BUTTON_MENU BUTTON_MENU
#define SUDOKU_BUTTON_POSSIBLE BUTTON_A

#elif (CONFIG_KEYPAD == IRIVER_H10_PAD)
#define SUDOKU_BUTTON_QUIT BUTTON_POWER
#define SUDOKU_BUTTON_UP BUTTON_SCROLL_UP
#define SUDOKU_BUTTON_DOWN BUTTON_SCROLL_DOWN
#define SUDOKU_BUTTON_LEFT BUTTON_LEFT
#define SUDOKU_BUTTON_RIGHT BUTTON_RIGHT
#define SUDOKU_BUTTON_TOGGLE BUTTON_REW
#define SUDOKU_BUTTON_MENU BUTTON_PLAY
#define SUDOKU_BUTTON_POSSIBLE BUTTON_FF

#elif (CONFIG_KEYPAD == SANSA_E200_PAD)
#define SUDOKU_BUTTON_QUIT BUTTON_POWER
#define SUDOKU_BUTTON_UP BUTTON_UP
#define SUDOKU_BUTTON_DOWN BUTTON_DOWN
#define SUDOKU_BUTTON_LEFT BUTTON_LEFT
#define SUDOKU_BUTTON_RIGHT BUTTON_RIGHT
#define SUDOKU_BUTTON_TOGGLEBACK BUTTON_SCROLL_UP
#define SUDOKU_BUTTON_TOGGLE BUTTON_SCROLL_DOWN
#define SUDOKU_BUTTON_MENU BUTTON_SELECT
#define SUDOKU_BUTTON_POSSIBLE BUTTON_REC

#elif
  #error SUDOKU: Unsupported keypad
#endif

struct sudoku_state_t {
  char filename[MAX_PATH];  /* Filename */
  char startboard[9][9];    /* The initial state of the game */
  char currentboard[9][9];  /* The current state of the game */
  char savedboard[9][9];    /* Cached copy of saved state */
  int x,y;                  /* Cursor position */
  int editmode;             /* We are editing the start board */
#ifdef SUDOKU_BUTTON_POSSIBLE 
  short possiblevals[9][9];  /* possible values a cell could be, user sets them */
#endif
};


#endif
