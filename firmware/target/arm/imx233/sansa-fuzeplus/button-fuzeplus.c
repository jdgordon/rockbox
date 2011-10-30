/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2011 by Amaury Pouly
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
#include "button-target.h"
#include "system.h"
#include "system-target.h"
#include "pinctrl-imx233.h"
#include "generic_i2c.h"
#include "synaptics-rmi.h"
#include "lcd.h"
#include "string.h"
#include "usb.h"

#ifndef BOOTLOADER

void button_debug_screen(void)
{
    char product_id[RMI_PRODUCT_ID_LEN];
    rmi_read(RMI_PRODUCT_ID, RMI_PRODUCT_ID_LEN, product_id);
    int x_max = rmi_read_single(RMI_2D_SENSOR_XMAX_MSB(0)) << 8 | rmi_read_single(RMI_2D_SENSOR_XMAX_LSB(0));
    int y_max = rmi_read_single(RMI_2D_SENSOR_YMAX_MSB(0)) << 8 | rmi_read_single(RMI_2D_SENSOR_YMAX_LSB(0));
    int func_presence = rmi_read_single(RMI_FUNCTION_PRESENCE(RMI_2D_TOUCHPAD_FUNCTION));
    int sensor_prop = rmi_read_single(RMI_2D_SENSOR_PROP2(0));
    int sensor_resol = rmi_read_single(RMI_2D_SENSOR_RESOLUTION(0));
    int min_dist = rmi_read_single(RMI_2D_MIN_DIST);
    int gesture_settings = rmi_read_single(RMI_2D_GESTURE_SETTINGS);
    union
    {
        unsigned char data;
        signed char value;
    }sensitivity;
    rmi_read(RMI_2D_SENSITIVITY_ADJ, 1, &sensitivity.data);

    /* Device to screen */
    int zone_w = LCD_WIDTH;
    int zone_h = (zone_w * y_max + x_max - 1) / x_max;
    int zone_x = 0;
    int zone_y = LCD_HEIGHT - zone_h;
    #define DX2SX(x) (((x) * zone_w ) / x_max)
    #define DY2SY(y) (zone_h - ((y) * zone_h ) / y_max)
    struct viewport report_vp;
    memset(&report_vp, 0, sizeof(report_vp));
    report_vp.x = zone_x;
    report_vp.y = zone_y;
    report_vp.width = zone_w;
    report_vp.height = zone_h;
    struct viewport gesture_vp;
    memset(&gesture_vp, 0, sizeof(gesture_vp));
    gesture_vp.x = 0;
    gesture_vp.y = zone_y - 80;
    gesture_vp.width = LCD_WIDTH;
    gesture_vp.height = 80;
    
    while(1)
    {
        lcd_set_viewport(NULL);
        lcd_clear_display();
        int btns = button_read_device();
        lcd_putsf(0, 0, "button bitmap: %x", btns);
        lcd_putsf(0, 1, "RMI: id=%s p=%x s=%x", product_id, func_presence, sensor_prop);
        lcd_putsf(0, 2, "xmax=%d ymax=%d res=%d", x_max, y_max, sensor_resol);
        lcd_putsf(0, 3, "attn=%d ctl=%x int=%x",
            imx233_get_gpio_input_mask(0, 0x08000000) ? 0 : 1,
            rmi_read_single(RMI_DEVICE_CONTROL),
            rmi_read_single(RMI_INTERRUPT_REQUEST));
        lcd_putsf(0, 4, "sensi: %d min_dist: %d", (int)sensitivity.value, min_dist);
        lcd_putsf(0, 5, "gesture: %x", gesture_settings);
        
        union
        {
            unsigned char data[10];
            struct
            {
                struct rmi_2d_absolute_data_t absolute;
                struct rmi_2d_relative_data_t relative;
                struct rmi_2d_gesture_data_t gesture;
            }s;
        }u;
        int absolute_x = u.s.absolute.x_msb << 8 | u.s.absolute.x_lsb;
        int absolute_y = u.s.absolute.y_msb << 8 | u.s.absolute.y_lsb;
        int nr_fingers = u.s.absolute.misc & 7;
        bool gesture = (u.s.absolute.misc & 8) == 8;
        int palm_width = u.s.absolute.misc >> 4;
        rmi_read(RMI_DATA_REGISTER(0), 10, u.data);
        lcd_putsf(0, 6, "abs: %d %d %d", absolute_x, absolute_y, (int)u.s.absolute.z);
        lcd_putsf(0, 7, "rel: %d %d", (int)u.s.relative.x, (int)u.s.relative.y);
        lcd_putsf(0, 8, "gesture: %x %x", u.s.gesture.misc, u.s.gesture.flick);
        lcd_putsf(0, 9, "misc: w=%d g=%d f=%d", palm_width, gesture, nr_fingers);

        lcd_set_viewport(&report_vp);
        lcd_set_drawinfo(DRMODE_SOLID, LCD_RGBPACK(0xff, 0, 0), LCD_BLACK);
        lcd_drawrect(0, 0, zone_w, zone_h);
        if(nr_fingers == 1)
        {
            lcd_set_drawinfo(DRMODE_SOLID, LCD_RGBPACK(0, 0, 0xff), LCD_BLACK);
            lcd_drawline(DX2SX(absolute_x) - u.s.relative.x,
                DY2SY(absolute_y) + u.s.relative.y,
                DX2SX(absolute_x), DY2SY(absolute_y));
            lcd_set_drawinfo(DRMODE_SOLID, LCD_RGBPACK(0, 0xff, 0), LCD_BLACK);
            lcd_fillrect(DX2SX(absolute_x) - 1, DY2SY(absolute_y) - 1, 3, 3);
        }
        lcd_set_viewport(&gesture_vp);
        lcd_set_drawinfo(DRMODE_SOLID, LCD_RGBPACK(0xff, 0xff, 0), LCD_BLACK);
        if(u.s.gesture.misc & RMI_2D_GEST_MISC_CONFIRMED)
        {
            switch(u.s.gesture.misc & RMI_2D_GEST_MISC_TAP_CODE_BM)
            {
                case RMI_2D_GEST_MISC_NO_TAP: break;
                case RMI_2D_GEST_MISC_SINGLE_TAP:
                    lcd_putsf(0, 0, "TAP!");
                    break;
                case RMI_2D_GEST_MISC_DOUBLE_TAP:
                    lcd_putsf(0, 0, "DOUBLE TAP!");
                    break;
                case RMI_2D_GEST_MISC_TAP_AND_HOLD:
                    lcd_putsf(0, 0, "TAP & HOLD!");
                    break;
                default: break;
            }
            
            if(u.s.gesture.misc & RMI_2D_GEST_MISC_FLICK)
            {
                lcd_putsf(0, 1, "FLICK!");
                int flick_x = u.s.gesture.flick & RMI_2D_GEST_FLICK_X_BM;
                int flick_y = (u.s.gesture.flick & RMI_2D_GEST_FLICK_Y_BM) >> RMI_2D_GEST_FLICK_Y_BP;
                #define SIGN4EXT(a) \
                    if(a & 8) a = -((a ^ 0xf) + 1);
                SIGN4EXT(flick_x);
                SIGN4EXT(flick_y);
                
                int center_x = (LCD_WIDTH * 2) / 3;
                int center_y = 40;
                lcd_drawline(center_x, center_y, center_x + flick_x * 5, center_y - flick_y * 5);
            }
        }
        lcd_update();
        
        if(btns & BUTTON_POWER)
            break;
        if(btns & BUTTON_VOL_DOWN || btns & BUTTON_VOL_UP)
        {
            if(btns & BUTTON_VOL_UP)
                sensitivity.value++;
            if(btns & BUTTON_VOL_DOWN)
                sensitivity.value--;
            rmi_write(RMI_2D_SENSITIVITY_ADJ, 1, &sensitivity.data);
        }
        
        yield();
    }
}

struct button_area_t
{
    /* define a rectangle region */
    int lx, ly;
    int rx, ry;
    int button;
};

static struct button_area_t button_areas[] =
{
    {1300, 600, 1700, 1100, BUTTON_SELECT},
    {500, 600, 1100, 1100, BUTTON_LEFT},
    {1900, 600, 2500, 1100, BUTTON_RIGHT},
    {1300, 0, 1700, 400, BUTTON_DOWN},
    {1300, 1300, 1700, 1800, BUTTON_UP},
    {2500, 1600, 2900, 1800, BUTTON_PLAYPAUSE},
    {300, 1600, 500, 1800, BUTTON_BACK},
    {0, 0, 0, 0, 0},
};

#define RMI_INTERRUPT   1

static int touchpad_btns = 0;
static long rmi_stack [DEFAULT_STACK_SIZE/sizeof(long)];
static const char rmi_thread_name[] = "rmi";
static struct event_queue rmi_queue;

static int find_button(int x, int y)
{
    struct button_area_t *area = button_areas;
    for(; area->button != 0; area++)
    {
        if(area->lx <= x && x <= area->rx &&
                area->ly <= y && y <= area->ry)
            return area->button;
    }
    return 0;
}

int touchpad_read_device(void)
{
    return touchpad_btns;
}

void rmi_attn_cb(int bank, int pin)
{
    (void) bank;
    (void) pin;
    /* the callback will not be fired until interrupt is enabled back so
     * the queue will not overflow or contain multiple RMI_INTERRUPT events */
    queue_post(&rmi_queue, RMI_INTERRUPT, 0);
}

void rmi_thread(void)
{
    struct queue_event ev;
    
    while(1)
    {
        queue_wait(&rmi_queue, &ev);
        /* handle usb connect and ignore all messages except rmi interrupts */
        if(ev.id == SYS_USB_CONNECTED)
        {
            usb_acknowledge(SYS_USB_CONNECTED_ACK);
            continue;
        }
        else if(ev.id != RMI_INTERRUPT)
            continue;
        /* clear interrupt */
        rmi_read_single(RMI_INTERRUPT_REQUEST);
        /* read data */
        union
        {
            unsigned char data[10];
            struct
            {
                struct rmi_2d_absolute_data_t absolute;
                struct rmi_2d_relative_data_t relative;
                struct rmi_2d_gesture_data_t gesture;
            }s;
        }u;
        rmi_read(RMI_DATA_REGISTER(0), 10, u.data);
        int absolute_x = u.s.absolute.x_msb << 8 | u.s.absolute.x_lsb;
        int absolute_y = u.s.absolute.y_msb << 8 | u.s.absolute.y_lsb;
        int nr_fingers = u.s.absolute.misc & 7;

        if(nr_fingers == 0)
            touchpad_btns = 0;
        else
            touchpad_btns = find_button(absolute_x, absolute_y);
        /* enable interrupt */
        imx233_setup_pin_irq(0, 27, true, true, false, &rmi_attn_cb);
    }
}

void button_init_device(void)
{
    /* Synaptics TouchPad information:
     * - product id: 1533
     * - nr function: 1 (0x10 = 2D touchpad)
     * 2D Touchpad information (function 0x10)
     * - nr data sources: 3
     * - standard layout
     * - extra data registers: 7
     * - nr sensors: 1
     * 2D Touchpad Sensor #0 information:
     * - has relative data: yes
     * - has palm detect: yes
     * - has multi finger: yes
     * - has enhanced gesture: yes
     * - has scroller: no
     * - has 2D scrollers: no
     * - Maximum X: 3009
     * - Maxumum Y: 1974
     * - Resolution: 82
     *
     * ATTENTION line: B0P27 asserted low
     *
     * The B0P26 line seems to be related to the touchpad
     */
     
    /* touchpad CE ? */
    imx233_set_pin_function(0, 26, PINCTRL_FUNCTION_GPIO);
    imx233_enable_gpio_output(0, 26, false);
    imx233_set_pin_drive_strength(0, 26, PINCTRL_DRIVE_8mA);
    
    rmi_init(0x40);

    rmi_write_single(RMI_2D_SENSITIVITY_ADJ, 5);
    rmi_write_single(RMI_2D_GESTURE_SETTINGS,
        RMI_2D_GESTURE_PRESS_TIME_300MS |
        RMI_2D_GESTURE_FLICK_DIST_4MM << RMI_2D_GESTURE_FLICK_DIST_BP |
        RMI_2D_GESTURE_FLICK_TIME_700MS << RMI_2D_GESTURE_FLICK_TIME_BP);

    queue_init(&rmi_queue, true);
    create_thread(rmi_thread, rmi_stack, sizeof(rmi_stack), 0,
            rmi_thread_name IF_PRIO(, PRIORITY_USER_INTERFACE) IF_COP(, CPU));
    /* enable interrupt */
    imx233_setup_pin_irq(0, 27, true, true, false, &rmi_attn_cb);
}

#else

void button_init_device(void)
{
    
}

int touchpad_read_device(void)
{
    return 0;
}

#endif

int button_read_device(void)
{
    int res = 0;
    if(!imx233_get_gpio_input_mask(1, 0x40000000))
        res |= BUTTON_VOL_DOWN;
    /* The imx233 uses the voltage on the PSWITCH pin to detect power up/down
     * events as well as recovery mode. Since the power button is the power button
     * and the volume up button is recovery, it is not possible to know whether
     * power button is down when volume up is down (except if there is another
     * method but volume up and power don't seem to be wired to GPIO pins). */
    switch((HW_POWER_STS & HW_POWER_STS__PSWITCH_BM) >> HW_POWER_STS__PSWITCH_BP)
    {
        case 1: res |= BUTTON_POWER; break;
        case 3: res |= BUTTON_VOL_UP; break;
        default: break;
    }
    return res | touchpad_read_device();
}
