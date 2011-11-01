/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2002 by Felix Arends
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

#include <math.h>
#include <stdlib.h>         /* EXIT_SUCCESS */
#include "sim-ui-defines.h"
#include "lcd-charcells.h"
#include "lcd-remote.h"
#include "config.h"
#include "button.h"
#include "kernel.h"
#include "backlight.h"
#include "system.h"
#include "button-sdl.h"
#include "sim_tasks.h"
#include "buttonmap.h"
#include "debug.h"
#include "powermgmt.h"

#ifdef HAVE_TOUCHSCREEN
#include "touchscreen.h"
static int mouse_coords = 0;
#endif
/* how long until repeat kicks in */
#define REPEAT_START      6

/* the speed repeat starts at */
#define REPEAT_INTERVAL_START   4

/* speed repeat finishes at */
#define REPEAT_INTERVAL_FINISH  2

#ifdef HAVE_TOUCHSCREEN
#define USB_KEY SDLK_c /* SDLK_u is taken by BUTTON_MIDLEFT */
#else
#define USB_KEY SDLK_u
#endif

#if defined(IRIVER_H100_SERIES) || defined (IRIVER_H300_SERIES)
int _remote_type=REMOTETYPE_H100_LCD;

int remote_type(void)
{
    return _remote_type;
}
#endif

struct event_queue button_queue;

static int btn = 0;    /* Hopefully keeps track of currently pressed keys... */

int sdl_app_has_input_focus = 1;
#if (CONFIG_PLATFORM & PLATFORM_MAEMO)
static int n900_updown_key_pressed = 0;
#endif

#ifdef HAS_BUTTON_HOLD
bool hold_button_state = false;
bool button_hold(void) {
    return hold_button_state;
}
#endif

#ifdef HAS_REMOTE_BUTTON_HOLD
bool remote_hold_button_state = false;
bool remote_button_hold(void) {
    return remote_hold_button_state;
}
#endif
static void button_event(int key, bool pressed);
extern bool debug_wps;
extern bool mapping;

#ifdef HAVE_TOUCHSCREEN
static void touchscreen_event(int x, int y)
{
    if(background) {
        x -= UI_LCD_POSX;
        y -= UI_LCD_POSY;
    }

    if(x >= 0 && y >= 0 && x < SIM_LCD_WIDTH && y < SIM_LCD_HEIGHT) {
        mouse_coords = (x << 16) | y;
        button_event(BUTTON_TOUCHSCREEN, true);
        if (debug_wps)
            printf("Mouse at 1: (%d, %d)\n", x, y);
    }
}
#endif

static void mouse_event(SDL_MouseButtonEvent *event, bool button_up)
{
#define SQUARE(x) ((x)*(x))
    static int x,y;
#ifdef SIMULATOR
    static int xybutton = 0;
#endif

    if(button_up) {
        switch ( event->button )
        {
#ifdef HAVE_SCROLLWHEEL
        case SDL_BUTTON_WHEELUP:
        case SDL_BUTTON_WHEELDOWN:
#endif
        case SDL_BUTTON_MIDDLE:
        case SDL_BUTTON_RIGHT:
            button_event( event->button, false );
            break;
        /* The scrollwheel button up events are ignored as they are queued immediately */
        case SDL_BUTTON_LEFT:
            if ( mapping && background ) {
                printf("    { SDLK_,     %d, %d, %d, \"\" },\n", x, y,
                (int)sqrt( SQUARE(x-(int)event->x) + SQUARE(y-(int)event->y))
                );
            }
#ifdef SIMULATOR
            if ( background && xybutton ) {
                    button_event( xybutton, false );
                    xybutton = 0;
                }
#endif
#ifdef HAVE_TOUCHSCREEN
                else
                    button_event(BUTTON_TOUCHSCREEN, false);
#endif
            break;
        }
    } else {    /* button down */
        switch ( event->button )
        {
#ifdef HAVE_SCROLLWHEEL
        case SDL_BUTTON_WHEELUP:
        case SDL_BUTTON_WHEELDOWN:
#endif
        case SDL_BUTTON_MIDDLE:
        case SDL_BUTTON_RIGHT:
            button_event( event->button, true );
            break;
        case SDL_BUTTON_LEFT:
            if ( mapping && background ) {
                x = event->x;
                y = event->y;
            }
#ifdef SIMULATOR
            if ( background ) {
                xybutton = xy2button( event->x, event->y );
                if( xybutton ) {
                    button_event( xybutton, true );
                    break;
                }
            }
#endif
#ifdef HAVE_TOUCHSCREEN
            touchscreen_event(event->x, event->y);
#endif
            break;
        }

        if (debug_wps && event->button == SDL_BUTTON_LEFT)
        {
            int m_x, m_y;

            if ( background )
            {
                m_x = event->x - 1;
                m_y = event->y - 1;
#ifdef HAVE_REMOTE
                if ( event->y >= UI_REMOTE_POSY ) /* Remote Screen */
                {
                    m_x -= UI_REMOTE_POSX;
                    m_y -= UI_REMOTE_POSY;
                }
                else
#endif
                {
                    m_x -= UI_LCD_POSX;
                    m_y -= UI_LCD_POSY;
                }
            }
            else
            {
                m_x = event->x;
                m_y = event->y;
#ifdef HAVE_REMOTE
                if ( m_y >= LCD_HEIGHT ) /* Remote Screen */
                    m_y -= LCD_HEIGHT;
#endif
            }

            printf("Mouse at 2: (%d, %d)\n", m_x, m_y);
        }
    }
#undef SQUARE
}

static bool event_handler(SDL_Event *event)
{
    SDLKey ev_key;

    switch(event->type)
    {
    case SDL_ACTIVEEVENT:
        if (event->active.state & SDL_APPINPUTFOCUS)
        {
            if (event->active.gain == 1)
                sdl_app_has_input_focus = 1;
            else
                sdl_app_has_input_focus = 0;
        }
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        ev_key = event->key.keysym.sym;
#if (CONFIG_PLATFORM & PLATFORM_MAEMO5)
        /* N900 with shared up/down cursor mapping. Seen on the German,
           Finnish, Italian, French and Russian version. Probably more. */
        if (event->key.keysym.mod & KMOD_MODE || n900_updown_key_pressed)
        {
            /* Prevent stuck up/down keys: If you release the ALT key before the cursor key,
               rockbox will see a KEYUP event for left/right instead of up/down and
               the previously pressed up/down key would stay active. */
            if (ev_key == SDLK_LEFT || ev_key == SDLK_RIGHT)
            {
                if (event->type == SDL_KEYDOWN)
                    n900_updown_key_pressed = 1;
                else
                    n900_updown_key_pressed = 0;
            }

            if (ev_key == SDLK_LEFT)
                ev_key = SDLK_UP;
            else if (ev_key == SDLK_RIGHT)
                ev_key = SDLK_DOWN;
        }
#endif
        button_event(ev_key, event->type == SDL_KEYDOWN);
        break;
#ifdef HAVE_TOUCHSCREEN
    case SDL_MOUSEMOTION:
        if (event->motion.state & SDL_BUTTON(1))
        {
            int x = event->motion.x / display_zoom;
            int y = event->motion.y / display_zoom;
            touchscreen_event(x, y);
        }
        break;
#endif

    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEBUTTONDOWN:
    {
        SDL_MouseButtonEvent *mev = &event->button;
        mev->x /= display_zoom;
        mev->y /= display_zoom;
        mouse_event(mev, event->type == SDL_MOUSEBUTTONUP);
        break;
    }
    case SDL_QUIT:
        /* Will post SDL_USEREVENT in shutdown_hw() if successful. */
        sys_poweroff();
        break;
    case SDL_USEREVENT:
        return true;
        break;
    }

    return false;
}

void gui_message_loop(void)
{
    SDL_Event event;
    bool quit;

    do {
        /* wait for the next event */
        if(SDL_WaitEvent(&event) == 0) {
            printf("SDL_WaitEvent() error\n");
            return; /* error, out of here */
        }

        sim_enter_irq_handler();
        quit = event_handler(&event);
        sim_exit_irq_handler();

    } while(!quit);
}

static void button_event(int key, bool pressed)
{
    int new_btn = 0;
    static bool usb_connected = false;
    if (usb_connected && key != USB_KEY)
        return;
    switch (key)
    {
    case USB_KEY:
        if (!pressed)
        {
            usb_connected = !usb_connected;
            if (usb_connected)
                queue_post(&button_queue, SYS_USB_CONNECTED, 0);
            else
                queue_post(&button_queue, SYS_USB_DISCONNECTED, 0);
        }
        return;

#if (CONFIG_PLATFORM & PLATFORM_PANDORA)
    case SDLK_LCTRL:
        /* Will post SDL_USEREVENT in shutdown_hw() if successful. */
        sys_poweroff();
        break;
#endif
#ifdef HAS_BUTTON_HOLD
    case SDLK_h:
        if(pressed)
        {
            hold_button_state = !hold_button_state;
            DEBUGF("Hold button is %s\n", hold_button_state?"ON":"OFF");
        }
        return;
#endif
        
#ifdef HAS_REMOTE_BUTTON_HOLD
    case SDLK_j:
        if(pressed)
        {
            remote_hold_button_state = !remote_hold_button_state;
            DEBUGF("Remote hold button is %s\n",
                   remote_hold_button_state?"ON":"OFF");
        }
        return;
#endif

#if defined(IRIVER_H100_SERIES) || defined (IRIVER_H300_SERIES)
    case SDLK_t:
        if(pressed)
            switch(_remote_type)
            {
                case REMOTETYPE_UNPLUGGED: 
                    _remote_type=REMOTETYPE_H100_LCD;
                    DEBUGF("Changed remote type to H100\n");
                    break;
                case REMOTETYPE_H100_LCD:
                    _remote_type=REMOTETYPE_H300_LCD;
                    DEBUGF("Changed remote type to H300\n");
                    break;
                case REMOTETYPE_H300_LCD:
                    _remote_type=REMOTETYPE_H300_NONLCD;
                    DEBUGF("Changed remote type to H300 NON-LCD\n");
                    break;
                case REMOTETYPE_H300_NONLCD:
                    _remote_type=REMOTETYPE_UNPLUGGED;
                    DEBUGF("Changed remote type to none\n");
                    break;
            }
        break;
#endif
    case SDLK_KP0:
    case SDLK_F5:
        if(pressed)
        {
            sim_trigger_screendump();
            return;
        }
        break;
#ifdef HAVE_TOUCHSCREEN
    case SDLK_F4:
        if(pressed)
        {
            touchscreen_set_mode(touchscreen_get_mode() == TOUCHSCREEN_POINT ? TOUCHSCREEN_BUTTON : TOUCHSCREEN_POINT);
            printf("Touchscreen mode: %s\n", touchscreen_get_mode() == TOUCHSCREEN_POINT ? "TOUCHSCREEN_POINT" : "TOUCHSCREEN_BUTTON");
        }
#endif
    default:
#ifdef HAVE_TOUCHSCREEN
        new_btn = key_to_touch(key, mouse_coords);
        if (!new_btn)
#endif
            new_btn = key_to_button(key);
        break;
    }
    /* Call to make up for scrollwheel target implementation.  This is
     * not handled in the main button.c driver, but on the target
     * implementation (look at button-e200.c for example if you are trying to 
     * figure out why using button_get_data needed a hack before).
     */
#if defined(BUTTON_SCROLL_FWD) && defined(BUTTON_SCROLL_BACK)
    if((new_btn == BUTTON_SCROLL_FWD || new_btn == BUTTON_SCROLL_BACK) && 
        pressed)
    {
        /* Clear these buttons from the data - adding them to the queue is
         *  handled in the scrollwheel drivers for the targets.  They do not
         *  store the scroll forward/back buttons in their button data for
         *  the button_read call.
         */
#ifdef HAVE_BACKLIGHT
        backlight_on();
#endif
#ifdef HAVE_BUTTON_LIGHT
        buttonlight_on();
#endif
        reset_poweroff_timer();
        queue_post(&button_queue, new_btn, 1<<24);
        new_btn &= ~(BUTTON_SCROLL_FWD | BUTTON_SCROLL_BACK);
    }
#endif

    if (pressed)
        btn |= new_btn;
    else
        btn &= ~new_btn;
}
#if defined(HAVE_BUTTON_DATA) && defined(HAVE_TOUCHSCREEN)
int button_read_device(int* data)
{
    *data = mouse_coords;
#else
int button_read_device(void)
{
#endif
#ifdef HAS_BUTTON_HOLD
    int hold_button = button_hold();

#ifdef HAVE_BACKLIGHT
    /* light handling */
    static int hold_button_old = false;
    if (hold_button != hold_button_old)
    {
        hold_button_old = hold_button;
        backlight_hold_changed(hold_button);
    }
#endif

    if (hold_button)
        return BUTTON_NONE;
    else
#endif

    return btn;
}

void button_init_device(void)
{
    SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
}
