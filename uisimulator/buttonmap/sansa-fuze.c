/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2008 Thomas Martitz
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


#include <SDL.h>
#include "button.h"
#include "buttonmap.h"

int key_to_button(int keyboard_button)
{
    int new_btn = BUTTON_NONE;
    switch (keyboard_button)
    {
        case SDLK_KP4:
        case SDLK_LEFT:
            new_btn = BUTTON_LEFT;
            break;
        case SDLK_KP6:
        case SDLK_RIGHT:
            new_btn = BUTTON_RIGHT;
            break;
        case SDLK_KP8:
        case SDLK_UP:
            new_btn = BUTTON_SCROLL_BACK;
            break;
        case SDLK_KP2:
        case SDLK_DOWN:
            new_btn = BUTTON_SCROLL_FWD;
            break;
        case SDLK_PAGEUP:
        case SDLK_KP9:
            new_btn = BUTTON_UP;
            break;
        case SDLK_PAGEDOWN:
        case SDLK_KP3:
            new_btn = BUTTON_DOWN;
            break;
        case SDLK_KP_MINUS:
        case SDLK_KP1:
            new_btn = BUTTON_POWER;
            break;
        case SDLK_KP_MULTIPLY:
            new_btn = BUTTON_HOME;
            break;
        case SDLK_KP5:
        case SDLK_SPACE:
        case SDLK_KP_ENTER:
        case SDLK_RETURN:
            new_btn = BUTTON_SELECT;
            break;
        case SDL_BUTTON_WHEELUP:
            new_btn = BUTTON_SCROLL_BACK;
            break;
        case SDL_BUTTON_WHEELDOWN:
            new_btn = BUTTON_SCROLL_FWD;
            break;
    }
    return new_btn;
}

struct button_map bm[] = {
    { SDLK_KP7, 70, 400, 15, "Back" },
    { SDLK_KP9, 258, 400, 15, "Play/Pause" },
    { SDLK_KP5, 162, 476, 15, "Select" },
    { SDLK_KP8, 162, 407, 35, "Up" },
    { SDLK_KP2, 162, 546, 35, "Down" },
    { SDLK_KP6, 233, 546, 35, "Right" },
    { SDLK_KP4, 92, 476, 35, "Left" },
    { SDLK_KP_MINUS, 20, 190, 20, "Volume-" },
    { SDLK_KP_PLUS, 20, 130, 20, "Volume+" },
    { SDLK_KP1, 160, 15, 15, "Power" },
    { 0, 0, 0, 0, "None" }
};
