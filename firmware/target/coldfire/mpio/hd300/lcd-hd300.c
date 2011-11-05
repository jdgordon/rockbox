/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Adopted for MPIO HD300 by Marcin Bukat
 * Copyright (C) 2007 by Jens Arnold
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

#include "system.h"
#include "kernel.h"
#include "lcd.h"

/*** definitions ***/

/* LCD command codes */
#define LCD_CNTL_POWER_CONTROL          0x25
#define LCD_CNTL_VOLTAGE_SELECT         0x2b
#define LCD_CNTL_LINE_INVERT_DRIVE      0x36
#define LCD_CNTL_GRAY_SCALE_PATTERN     0x39
#define LCD_CNTL_TEMP_GRADIENT_SELECT   0x4e
#define LCD_CNTL_OSC_FREQUENCY          0x5f
#define LCD_CNTL_ON_OFF                 0xae
#define LCD_CNTL_OSC_ON_OFF             0xaa
#define LCD_CNTL_OFF_MODE               0xbe
#define LCD_CNTL_POWER_SAVE             0xa8
#define LCD_CNTL_REVERSE                0xa6
#define LCD_CNTL_ALL_LIGHTING           0xa4
#define LCD_CNTL_COMMON_OUTPUT_STATUS   0xc4
#define LCD_CNTL_COLUMN_ADDRESS_DIR     0xa0
#define LCD_CNTL_NLINE_ON_OFF           0xe4
#define LCD_CNTL_DISPLAY_MODE           0x66
#define LCD_CNTL_DUTY_SET               0x6d
#define LCD_CNTL_ELECTRONIC_VOLUME      0x81
#define LCD_CNTL_DATA_INPUT_DIR         0x84
#define LCD_CNTL_DISPLAY_START_LINE     0x8a
#define LCD_CNTL_AREA_SCROLL            0x10

#define LCD_CNTL_PAGE                   0xb1
#define LCD_CNTL_COLUMN                 0x13
#define LCD_CNTL_DATA_WRITE             0x1d

/*** shared semi-private declarations ***/
extern const unsigned char lcd_dibits[16] ICONST_ATTR;

/*** hardware configuration ***/
int lcd_default_contrast(void)
{
    return DEFAULT_CONTRAST_SETTING;
}

void lcd_set_contrast(int val)
{
    /* Keep val in acceptable hw range */
    if (val < 0)
        val = 0;
    else if (val > 127)
        val = 127;

    lcd_write_command_ex(LCD_CNTL_ELECTRONIC_VOLUME, val, -1);
}

void lcd_set_invert_display(bool yesno)
{
    lcd_write_command(LCD_CNTL_REVERSE | (yesno?1:0));
}

/* turn the display upside down (call lcd_update() afterwards) */
void lcd_set_flip(bool yesno)
{
    if (yesno) 
    {
        lcd_write_command(LCD_CNTL_COLUMN_ADDRESS_DIR | 1);
        lcd_write_command(LCD_CNTL_COMMON_OUTPUT_STATUS | 0);
        lcd_write_command_ex(LCD_CNTL_DUTY_SET, 0x20, 0);
    }
    else
    {
        lcd_write_command(LCD_CNTL_COLUMN_ADDRESS_DIR | 0);
        lcd_write_command(LCD_CNTL_COMMON_OUTPUT_STATUS | 1);
        lcd_write_command_ex(LCD_CNTL_DUTY_SET, 0x20, 1);
    }
}

void lcd_init_device(void)
{
    and_l(~0x00000800, &GPIO_FUNCTION); /* CS3 line */

    /* LCD Reset GPO34 */
    or_l(0x00000004, &GPIO1_ENABLE);    /* set as output */
    or_l(0x00000004, &GPIO1_FUNCTION);  /* switch to secondary function - GPIO */

    and_l(~0x00000004, &GPIO1_OUT);     /* RESET low */
    sleep(1);                           /* delay at least 1000 ns */
    or_l(0x00000004, &GPIO1_OUT);       /* RESET high */
    sleep(1);

    lcd_write_command(LCD_CNTL_ON_OFF | 1); /* LCD ON */
    lcd_write_command(LCD_CNTL_OFF_MODE | 1); /* OFF -> VCC on drivers */
    lcd_write_command(LCD_CNTL_REVERSE | 0); /* Reverse OFF */
    lcd_write_command(LCD_CNTL_ALL_LIGHTING | 0); /* Normal */
    lcd_write_command(LCD_CNTL_COMMON_OUTPUT_STATUS | 1); /* Reverse dir */
    lcd_write_command_ex(LCD_CNTL_DISPLAY_START_LINE, 0, -1);
    lcd_write_command(LCD_CNTL_COLUMN_ADDRESS_DIR | 0);   /* Normal */
    lcd_write_command_ex(LCD_CNTL_DISPLAY_MODE, 0, -1); /* Greyscale mode */
    lcd_write_command_ex(LCD_CNTL_GRAY_SCALE_PATTERN, 0x53, -1);
    lcd_write_command_ex(LCD_CNTL_DUTY_SET, 0x20, 1);
    lcd_write_command_ex(LCD_CNTL_ELECTRONIC_VOLUME, 24, -1); /* 0x18 */

    lcd_write_command(LCD_CNTL_OSC_ON_OFF | 1); /* Oscillator ON */
    lcd_write_command(LCD_CNTL_POWER_SAVE | 0);
    lcd_write_command_ex(LCD_CNTL_VOLTAGE_SELECT, 3, -1);
    lcd_write_command_ex(LCD_CNTL_POWER_CONTROL, 0x17, -1);
    lcd_write_command_ex(LCD_CNTL_OSC_FREQUENCY, 3, -1);
    lcd_write_command(LCD_CNTL_NLINE_ON_OFF | 1); /* N-line ON */
    lcd_write_command_ex(LCD_CNTL_LINE_INVERT_DRIVE, 0x10, -1);
    lcd_write_command_ex(LCD_CNTL_TEMP_GRADIENT_SELECT, 0, -1);

    lcd_update();
}

/*** update functions ***/

/* Performance function that works with an external buffer
   note that by and bheight are in 8-pixel units! */
void lcd_blit_mono(const unsigned char *data, int x, int by, int width,
                   int bheight, int stride)
{
    const unsigned char *src, *src_end;
    unsigned char *dst_u, *dst_l;
    static unsigned char upper[LCD_WIDTH] IBSS_ATTR;
    static unsigned char lower[LCD_WIDTH] IBSS_ATTR;
    unsigned int byte;

    by *= 2;

    while (bheight--)
    {
        src = data;
        src_end = data + width;
        dst_u = upper;
        dst_l = lower;
        do
        {
            byte = *src++;
            *dst_u++ = lcd_dibits[byte & 0x0F];
            byte >>= 4;
            *dst_l++ = lcd_dibits[byte & 0x0F];
        }
        while (src < src_end);

        lcd_write_command_ex(LCD_CNTL_PAGE, by++, -1);
        lcd_write_command_ex(LCD_CNTL_COLUMN, x, -1);
        lcd_write_command(LCD_CNTL_DATA_WRITE);
        lcd_write_data(upper, width);

        lcd_write_command_ex(LCD_CNTL_PAGE, by++, -1);
        lcd_write_command_ex(LCD_CNTL_COLUMN, x, -1);
        lcd_write_command(LCD_CNTL_DATA_WRITE);
        lcd_write_data(lower, width);

        data += stride;
    }
}

/* Helper function for lcd_grey_phase_blit(). */
void lcd_grey_data(unsigned char *values, unsigned char *phases, int count);

/* Performance function that works with an external buffer
   note that by and bheight are in 4-pixel units! */
void lcd_blit_grey_phase(unsigned char *values, unsigned char *phases,
                         int x, int by, int width, int bheight, int stride)
{
    stride <<= 2; /* 4 pixels per block */
    while (bheight--)
    {
        lcd_write_command_ex(LCD_CNTL_PAGE, by++, -1);
        lcd_write_command_ex(LCD_CNTL_COLUMN, x, -1);
        lcd_write_command(LCD_CNTL_DATA_WRITE);
        lcd_grey_data(values, phases, width);
        values += stride;
        phases += stride;
    }
}

/* Update the display.
   This must be called after all other LCD functions that change the display. */
void lcd_update(void) ICODE_ATTR;
void lcd_update(void)
{

    /* Setup initial PAGE and COLUMN address
     * the addressing circuit will take care of the rest
     */
    lcd_write_command_ex(LCD_CNTL_PAGE, 0, -1);
    lcd_write_command_ex(LCD_CNTL_COLUMN, 0, -1);
    lcd_write_command(LCD_CNTL_DATA_WRITE);

    /* Copy display bitmap to hardware */
    lcd_write_data (&lcd_framebuffer[0][0], LCD_WIDTH*LCD_FBHEIGHT);
}

/* Update a fraction of the display. */
void lcd_update_rect(int, int, int, int) ICODE_ATTR;
void lcd_update_rect(int x, int y, int width, int height)
{
    int ymax;

    /* The Y coordinates have to work on even 8 pixel rows */
    ymax = (y + height-1) >> 2;
    y >>= 2;

    if(x + width > LCD_WIDTH)
        width = LCD_WIDTH - x;
    if (width <= 0)
        return; /* nothing left to do, 0 is harmful to lcd_write_data() */
    if(ymax >= LCD_FBHEIGHT)
        ymax = LCD_FBHEIGHT-1;

    /* Copy specified rectange bitmap to hardware */
    for (; y <= ymax; y++)
    {
        lcd_write_command_ex(LCD_CNTL_PAGE, y, -1);
        lcd_write_command_ex(LCD_CNTL_COLUMN, x, -1);

        lcd_write_command(LCD_CNTL_DATA_WRITE);
        lcd_write_data (&lcd_framebuffer[y][x], width);
    }
}
