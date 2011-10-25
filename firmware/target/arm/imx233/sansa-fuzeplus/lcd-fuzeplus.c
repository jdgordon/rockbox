/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (c) 2011 by Amaury Pouly
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
#include <sys/types.h> /* off_t */
#include <string.h>
#include "cpu.h"
#include "system.h"
#include "backlight-target.h"
#include "lcd.h"
#include "lcdif-imx233.h"
#include "clkctrl-imx233.h"
#include "pinctrl-imx233.h"
#include "logf.h"

/* Copies a rectangle from one framebuffer to another. Can be used in
   single transfer mode with width = num pixels, and height = 1 which
   allows a full-width rectangle to be copied more efficiently. */
extern void lcd_copy_buffer_rect(fb_data *dst, const fb_data *src,
                                 int width, int height);

static unsigned lcd_yuv_options = 0;

#ifdef HAVE_LCD_ENABLE
static bool lcd_on = true;
#endif

static enum lcd_kind_t
{
    LCD_KIND_7783 = 0x7783,
    LCD_KIND_9325 = 0x9325,
    LCD_KIND_OTHER = 0,
} lcd_kind = LCD_KIND_OTHER;

static void setup_parameters(void)
{
    imx233_lcdif_reset();
    imx233_lcdif_set_lcd_databus_width(HW_LCDIF_CTRL__LCD_DATABUS_WIDTH_18_BIT);
    imx233_lcdif_set_word_length(HW_LCDIF_CTRL__WORD_LENGTH_18_BIT);
    imx233_lcdif_set_timings(1, 2, 2, 2);
}

static void setup_lcd_pins(bool use_lcdif)
{
    if(use_lcdif)
    {
        imx233_set_pin_function(1, 25, PINCTRL_FUNCTION_GPIO); /* lcd_vsync */
        imx233_set_pin_function(1, 21, PINCTRL_FUNCTION_MAIN); /* lcd_cs */
        imx233_set_pin_function(1, 22, PINCTRL_FUNCTION_GPIO); /* lcd_dotclk */
        imx233_set_pin_function(1, 23, PINCTRL_FUNCTION_GPIO); /* lcd_enable */
        imx233_set_pin_function(1, 24, PINCTRL_FUNCTION_GPIO); /* lcd_hsync */
        imx233_set_pin_function(1, 18, PINCTRL_FUNCTION_MAIN); /* lcd_reset */
        imx233_set_pin_function(1, 19, PINCTRL_FUNCTION_MAIN); /* lcd_rs */
        imx233_set_pin_function(1, 16, PINCTRL_FUNCTION_MAIN); /* lcd_d16 */
        imx233_set_pin_function(1, 17, PINCTRL_FUNCTION_MAIN); /* lcd_d17 */
        imx233_set_pin_function(1, 20, PINCTRL_FUNCTION_MAIN); /* lcd_wr */
        __REG_CLR(HW_PINCTRL_MUXSEL(2)) = 0xffffffff; /* lcd_d{0-15} */
    }
    else
    {
        __REG_SET(HW_PINCTRL_MUXSEL(2)) = 0xffffffff; /* lcd_d{0-15} */
        imx233_enable_gpio_output_mask(1, 0x3ffffff, false); /* lcd_{d{0-17},reset,rs,wr,cs,dotclk,enable,hsync,vsync} */
        imx233_set_pin_function(1, 16, PINCTRL_FUNCTION_GPIO); /* lcd_d16 */
        imx233_set_pin_function(1, 17, PINCTRL_FUNCTION_GPIO); /* lcd_d17 */
        imx233_set_pin_function(1, 19, PINCTRL_FUNCTION_GPIO); /* lcd_rs */
        imx233_set_pin_function(1, 20, PINCTRL_FUNCTION_GPIO); /* lcd_wr */
        imx233_set_pin_function(1, 21, PINCTRL_FUNCTION_GPIO); /* lcd_cs */
        imx233_set_pin_function(1, 22, PINCTRL_FUNCTION_GPIO); /* lcd_dotclk */
        imx233_set_pin_function(1, 23, PINCTRL_FUNCTION_GPIO); /* lcd_enable */
        imx233_set_pin_function(1, 24, PINCTRL_FUNCTION_GPIO); /* lcd_hsync */
        imx233_set_pin_function(1, 25, PINCTRL_FUNCTION_GPIO); /* lcd_vsync */
    }
}

static void setup_lcd_pins_i80(bool i80)
{
    if(i80)
    {
        imx233_set_pin_drive_strength(1, 19, PINCTRL_DRIVE_12mA); /* lcd_rs */
        imx233_set_pin_drive_strength(1, 20, PINCTRL_DRIVE_12mA); /* lcd_wr */
        imx233_set_pin_drive_strength(1, 21, PINCTRL_DRIVE_12mA); /* lcd_cs */
        imx233_set_pin_drive_strength(1, 23, PINCTRL_DRIVE_12mA); /* lcd_enable */
        imx233_set_pin_function(1, 19, PINCTRL_FUNCTION_GPIO); /* lcd_rs */
        imx233_set_pin_function(1, 20, PINCTRL_FUNCTION_GPIO); /* lcd_wr */
        imx233_set_pin_function(1, 21, PINCTRL_FUNCTION_GPIO); /* lcd_cs */
        imx233_set_pin_function(1, 23, PINCTRL_FUNCTION_GPIO); /* lcd_enable */
        /* lcd_{rs,wr,cs,enable} */
        imx233_enable_gpio_output_mask(1, (1 << 19) | (1 << 20) | (1 << 21) | (1 << 23), true);
        imx233_set_gpio_output_mask(1, (1 << 19) | (1 << 20) | (1 << 21) | (1 << 23), true);

        imx233_enable_gpio_output_mask(1, 0x3ffff, false); /* lcd_d{0-17} */
        __REG_SET(HW_PINCTRL_MUXSEL(2)) = 0xffffffff; /* lcd_d{0-15} as GPIO */
        imx233_set_pin_function(1, 16, PINCTRL_FUNCTION_GPIO); /* lcd_d16 */
        imx233_set_pin_function(1, 17, PINCTRL_FUNCTION_GPIO); /* lcd_d17 */
        imx233_set_pin_function(1, 18, PINCTRL_FUNCTION_GPIO); /* lcd_reset */
        imx233_set_pin_function(1, 19, PINCTRL_FUNCTION_GPIO); /* lcd_rs */
    }
    else
    {
        imx233_set_gpio_output_mask(1, (1 << 19) | (1 << 20) | (1 << 21) | (1 << 23), true);
        imx233_set_pin_drive_strength(1, 19, PINCTRL_DRIVE_4mA); /* lcd_rs */
        imx233_set_pin_drive_strength(1, 20, PINCTRL_DRIVE_4mA); /* lcd_wr */
        imx233_set_pin_drive_strength(1, 21, PINCTRL_DRIVE_4mA); /* lcd_cs */
        imx233_set_pin_drive_strength(1, 23, PINCTRL_DRIVE_4mA); /* lcd_enable */
        imx233_set_pin_function(1, 19, PINCTRL_FUNCTION_MAIN); /* lcd_rs */
        imx233_set_pin_function(1, 20, PINCTRL_FUNCTION_MAIN); /* lcd_wr */
        imx233_set_pin_function(1, 21, PINCTRL_FUNCTION_MAIN); /* lcd_cs */
        imx233_enable_gpio_output_mask(1, 0x3ffff, false); /* lcd_d{0-17} */
        __REG_CLR(HW_PINCTRL_MUXSEL(2)) = 0xffffffff; /* lcd_d{0-15} as lcd_d{0-15} */
        imx233_set_pin_function(1, 16, PINCTRL_FUNCTION_MAIN); /* lcd_d16 */
        imx233_set_pin_function(1, 17, PINCTRL_FUNCTION_MAIN); /* lcd_d17 */
        imx233_set_pin_function(1, 18, PINCTRL_FUNCTION_MAIN); /* lcd_reset */
        imx233_set_pin_function(1, 19, PINCTRL_FUNCTION_MAIN); /* lcd_rs */
    }
}

static void common_lcd_enable(bool enable)
{
    imx233_lcdif_enable(enable);
    setup_lcd_pins(enable); /* use GPIO pins when disable */
}

static void setup_lcdif(void)
{
    setup_parameters();
    common_lcd_enable(true);
    imx233_lcdif_enable_bus_master(true);
}

static inline uint32_t encode_16_to_18(uint32_t a)
{
    return ((a & 0xff) << 1) | (((a >> 8) & 0xff) << 10);
}

static inline uint32_t decode_18_to_16(uint32_t a)
{
    return ((a >> 1) & 0xff) | ((a >> 2) & 0xff00);
}

static void setup_lcdif_clock(void)
{
    /* the LCD seems to work at 24Mhz, so use the xtal clock with no divider */
    imx233_enable_clock(CLK_PIX, false);
    imx233_set_clock_divisor(CLK_PIX, 1);
    imx233_set_bypass_pll(CLK_PIX, true); /* use XTAL */
    imx233_enable_clock(CLK_PIX, true);
}

static uint32_t i80_read_register(uint32_t data_out)
{
    /* lcd_enable is mapped to the RD pin of the controller */
    imx233_set_gpio_output(1, 21, true); /* lcd_cs */
    imx233_set_gpio_output(1, 19, true); /* lcd_rs */
    imx233_set_gpio_output(1, 23, true); /* lcd_enable */
    imx233_set_gpio_output(1, 20, true); /* lcd_wr */
    imx233_enable_gpio_output_mask(1, 0x3ffff, true); /* lcd_d{0-17} */
    mdelay(2);
    imx233_set_gpio_output(1, 19, false); /* lcd_rs */
    mdelay(1);
    imx233_set_gpio_output(1, 21, false); /* lcd_cs */
    mdelay(1);
    imx233_set_gpio_output(1, 20, false); /* lcd_wr */
    mdelay(1);
    imx233_set_gpio_output_mask(1, data_out & 0x3ffff, true); /* lcd_d{0-17} */
    mdelay(1);
    imx233_set_gpio_output(1, 20, true); /* lcd_wr */
    mdelay(3);
    imx233_enable_gpio_output_mask(1, 0x3ffff, false); /* lcd_d{0-17} */
    mdelay(2);
    imx233_set_gpio_output(1, 23, false); /* lcd_enable */
    mdelay(1);
    imx233_set_gpio_output(1, 19, true); /* lcd_rs */
    mdelay(1);
    imx233_set_gpio_output(1, 23, true); /* lcd_enable */
    mdelay(3);
    imx233_set_gpio_output(1, 23, false); /* lcd_enable */
    mdelay(2);
    uint32_t data_in = imx233_get_gpio_input_mask(1, 0x3ffff); /* lcd_d{0-17} */
    mdelay(1);
    imx233_set_gpio_output(1, 23, true); /* lcd_enable */
    mdelay(1);
    imx233_set_gpio_output(1, 21, true); /* lcd_cs */
    mdelay(1);
    return data_in;
}

static void lcd_write_reg(uint32_t reg, uint32_t data)
{
    uint32_t old_reg = reg;
    /* get back to 18-bit word length */
    imx233_lcdif_set_word_length(HW_LCDIF_CTRL__WORD_LENGTH_18_BIT);
    reg = encode_16_to_18(reg);
    data = encode_16_to_18(data);
    
    imx233_lcdif_pio_send(false, 2, &reg);
    if(old_reg != 0x22)
        imx233_lcdif_pio_send(true, 2, &data);
}

static uint32_t lcd_read_reg(uint32_t reg)
{
    setup_lcd_pins_i80(true);
    uint32_t data_in = i80_read_register(encode_16_to_18(reg));
    setup_lcd_pins_i80(false);
    lcd_write_reg(0x22, 0);
    return decode_18_to_16(data_in);
}

#define REG_MDELAY  0xffffffff
struct lcd_sequence_entry_t
{
    uint32_t reg, data;
};

static void lcd_send_sequence(struct lcd_sequence_entry_t *seq, unsigned count)
{
    for(;count-- > 0; seq++)
    {
        if(seq->reg == REG_MDELAY)
            mdelay(seq->data);
        else
            lcd_write_reg(seq->reg, seq->data);
    }
}

#define _begin_seq() static struct lcd_sequence_entry_t __seq[] = {
#define _mdelay(a) {REG_MDELAY, a},
#define _lcd_write_reg(a, b) {a, b},
#define _end_seq() }; lcd_send_sequence(__seq, sizeof(__seq) / sizeof(__seq[0]));

static void lcd_init_seq_7783(void)
{
    __REG_SET(HW_LCDIF_CTRL1) = HW_LCDIF_CTRL1__RESET;
    mdelay(50);
    __REG_CLR(HW_LCDIF_CTRL1) = HW_LCDIF_CTRL1__RESET;
    mdelay(10);
    __REG_SET(HW_LCDIF_CTRL1) = HW_LCDIF_CTRL1__RESET;

    _begin_seq()
    _mdelay(200)
    _lcd_write_reg(1, 0x100)
    _lcd_write_reg(2, 0x700)
    _lcd_write_reg(3, 0x1030)
    _lcd_write_reg(7, 0x121)
    _lcd_write_reg(8, 0x302)
    _lcd_write_reg(9, 0x200)
    _lcd_write_reg(0xa, 0)
    _lcd_write_reg(0x10, 0x790)
    _lcd_write_reg(0x11, 5)
    _lcd_write_reg(0x12, 0)
    _lcd_write_reg(0x13, 0)
    _mdelay(100)
    _lcd_write_reg(0x10, 0x12b0)
    _mdelay(100)
    _lcd_write_reg(0x11, 7)
    _mdelay(100)
    _lcd_write_reg(0x12, 0x89)
    _lcd_write_reg(0x13, 0x1d00)
    _lcd_write_reg(0x29, 0x2f)
    _mdelay(50)
    _lcd_write_reg(0x30, 0)
    _lcd_write_reg(0x31, 0x505)
    _lcd_write_reg(0x32, 0x205)
    _lcd_write_reg(0x35, 0x206)
    _lcd_write_reg(0x36, 0x408)
    _lcd_write_reg(0x37, 0)
    _lcd_write_reg(0x38, 0x504)
    _lcd_write_reg(0x39, 0x206)
    _lcd_write_reg(0x3c, 0x206)
    _lcd_write_reg(0x3d, 0x408)
    _lcd_write_reg(0x50, 0) /* left X ? */ 
    _lcd_write_reg(0x51, 0xef) /* right X ? */
    _lcd_write_reg(0x52, 0) /* top Y ? */
    _lcd_write_reg(0x53, 0x13f) /* bottom Y ? */
    _lcd_write_reg(0x20, 0) /* left X ? */ 
    _lcd_write_reg(0x21, 0) /* top Y ? */
    _lcd_write_reg(0x60, 0xa700)
    _lcd_write_reg(0x61, 1)
    _lcd_write_reg(0x90, 0x33)
    _lcd_write_reg(0x2b, 0xa)
    _lcd_write_reg(9, 0)
    _lcd_write_reg(7, 0x133)
    _mdelay(50)
    _lcd_write_reg(0x22, 0)
    _end_seq()
}

static void lcd_init_seq_9325(void)
{
    _begin_seq()
    _lcd_write_reg(0xe5, 0x78f0)
    _lcd_write_reg(0xe3, 0x3008)
    _lcd_write_reg(0xe7, 0x12)
    _lcd_write_reg(0xef, 0x1231)
    _lcd_write_reg(0, 1)
    _lcd_write_reg(1, 0x100)
    _lcd_write_reg(2, 0x700)
    _lcd_write_reg(3, 0x1030)
    _lcd_write_reg(4, 0)
    _lcd_write_reg(8, 0x207)
    _lcd_write_reg(9, 0)
    _lcd_write_reg(0xa, 0)
    _lcd_write_reg(0xc, 0)
    _lcd_write_reg(0xd, 0)
    _lcd_write_reg(0xf, 0)
    _lcd_write_reg(0x10, 0)
    _lcd_write_reg(0x11, 7)
    _lcd_write_reg(0x12, 0)
    _lcd_write_reg(0x13, 0)
    _mdelay(20)
    _lcd_write_reg(0x10, 0x1290)
    _lcd_write_reg(0x11, 7)
    _mdelay(50)
    _lcd_write_reg(0x12, 0x19)
    _mdelay(50)
    _lcd_write_reg(0x13, 0x1700)
    _lcd_write_reg(0x29, 0x14)
    _mdelay(50)
    _lcd_write_reg(0x20, 0)
    _lcd_write_reg(0x21, 0)
    _lcd_write_reg(0x30, 0x504)
    _lcd_write_reg(0x31, 7)
    _lcd_write_reg(0x32, 6)
    _lcd_write_reg(0x35, 0x106)
    _lcd_write_reg(0x36, 0x202)
    _lcd_write_reg(0x37, 0x504)
    _lcd_write_reg(0x38, 0x500)
    _lcd_write_reg(0x39, 0x706)
    _lcd_write_reg(0x3c, 0x204)
    _lcd_write_reg(0x3d, 0x202)
    _lcd_write_reg(0x50, 0)
    _lcd_write_reg(0x51, 0xef)
    _lcd_write_reg(0x52, 0)
    _lcd_write_reg(0x53, 0x13f)
    _lcd_write_reg(0x60, 0xa700)
    _lcd_write_reg(0x61, 1)
    _lcd_write_reg(0x6a, 1)
    _lcd_write_reg(0x2b, 0xd)
    _mdelay(50)
    _lcd_write_reg(0x90, 0x11)
    _lcd_write_reg(0x92, 0x600)
    _lcd_write_reg(0x93, 3)
    _lcd_write_reg(0x95, 0x110)
    _lcd_write_reg(0x97, 0)
    _lcd_write_reg(0x98, 0)
    _lcd_write_reg(7, 0x173)
    _lcd_write_reg(0x22, 0)
    _end_seq()
}

void lcd_init_device(void)
{
    setup_lcdif();
    setup_lcdif_clock();

    for(int i = 0; i < 10; i++)
    {
        lcd_kind = lcd_read_reg(0);
        if(lcd_kind == LCD_KIND_7783 || lcd_kind == LCD_KIND_9325)
            break;
    }
    mdelay(5);
    switch(lcd_kind)
    {
        case LCD_KIND_7783: lcd_init_seq_7783(); break;
        case LCD_KIND_9325: lcd_init_seq_9325(); break;
        default:
            lcd_kind = LCD_KIND_7783;
            lcd_init_seq_7783(); break;
    }
}

#ifdef HAVE_LCD_ENABLE
static void lcd_enable_7783(bool enable)
{
    if(!enable)
    {
        _begin_seq()
        _lcd_write_reg(7, 0x131)
        _mdelay(50)
        _lcd_write_reg(7, 0x20)
        _mdelay(50)
        _lcd_write_reg(0x10, 0x82)
        _mdelay(50)
        _end_seq()
    }
    else
    {
        _begin_seq()
        _lcd_write_reg(0x11, 5)
        _lcd_write_reg(0x10, 0x12b0)
        _mdelay(50)
        _lcd_write_reg(7, 0x11)
        _mdelay(50)
        _lcd_write_reg(0x12, 0x89)
        _mdelay(50)
        _lcd_write_reg(0x13, 0x1d00)
        _mdelay(50)
        _lcd_write_reg(0x29, 0x2f)
        _mdelay(50)
        _lcd_write_reg(0x2b, 0xa)
        _lcd_write_reg(7, 0x133)
        _mdelay(50)
        _lcd_write_reg(0x22, 0)
        _end_seq()
    }
}

static void lcd_enable_9325(bool enable)
{
    if(!enable)
    {
        _begin_seq()
        _lcd_write_reg(7, 0x131)
        _mdelay(10)
        _lcd_write_reg(7, 0x130)
        _mdelay(10)
        _lcd_write_reg(7, 0)
        _lcd_write_reg(0x10, 0x80)
        _lcd_write_reg(0x11, 0)
        _lcd_write_reg(0x12, 0)
        _lcd_write_reg(0x13, 0)
        _mdelay(200)
        _lcd_write_reg(0x10, 0x82)
        _end_seq()
    }
    else
    {
        _begin_seq()
        _lcd_write_reg(0x10, 0x80)
        _lcd_write_reg(0x11, 0)
        _lcd_write_reg(0x12, 0)
        _lcd_write_reg(0x13, 0)
        _lcd_write_reg(7, 1)
        _mdelay(200)
        _lcd_write_reg(0x10, 0x1290)
        _lcd_write_reg(0x11, 7)
        _mdelay(50)
        _lcd_write_reg(0x12, 0x19)
        _mdelay(50)
        _lcd_write_reg(0x13, 0x1700)
        _lcd_write_reg(0x29, 0x10)
        _mdelay(50)
        _lcd_write_reg(7, 0x133)
        _lcd_write_reg(0x22, 0)
        _end_seq()
    }
}

void lcd_enable(bool enable)
{
    if(lcd_on == enable)
        return;
    lcd_on = enable;
    
    if(enable)
        common_lcd_enable(true);
    switch(lcd_kind)
    {
        case LCD_KIND_7783: lcd_enable_7783(enable); break;
        case LCD_KIND_9325: lcd_enable_9325(enable); break;
        default: lcd_enable_7783(enable); break;
    }
    if(!enable)
        common_lcd_enable(false);
}

bool lcd_active(void)
{
    return lcd_on;
}
#endif

void lcd_update(void)
{
#ifdef HAVE_LCD_ENABLE
    if(!lcd_on)
        return;
#endif
    imx233_lcdif_wait_ready();
    lcd_write_reg(0x50, 0);
    lcd_write_reg(0x51, LCD_WIDTH - 1);
    lcd_write_reg(0x52, 0);
    lcd_write_reg(0x53, LCD_HEIGHT - 1);
    lcd_write_reg(0x20, 0);
    lcd_write_reg(0x21, 0);
    lcd_write_reg(0x22, 0);
    imx233_lcdif_wait_ready();
    imx233_lcdif_set_word_length(HW_LCDIF_CTRL__WORD_LENGTH_16_BIT);
    imx233_lcdif_set_byte_packing_format(0xf); /* two pixels per 32-bit word */
    imx233_lcdif_set_data_format(false, false, false); /* RGB565, don't care, don't care */
    lcd_copy_buffer_rect((fb_data *)FRAME, &lcd_framebuffer[0][0],
                         LCD_WIDTH * LCD_HEIGHT, 1);
    imx233_lcdif_dma_send((void *)FRAME_PHYS_ADDR, LCD_WIDTH, LCD_HEIGHT);
}

void lcd_update_rect(int x, int y, int width, int height)
{
    (void) x;
    (void) y;
    (void) width;
    (void) height;
    lcd_update();
}

void lcd_yuv_set_options(unsigned options)
{
    lcd_yuv_options = options;
}

/* Line write helper function for lcd_yuv_blit. Write two lines of yuv420. */
extern void lcd_write_yuv420_lines(fb_data *dst,
                                   unsigned char const * const src[3],
                                   int width,
                                   int stride);
extern void lcd_write_yuv420_lines_odither(fb_data *dst,
                                           unsigned char const * const src[3],
                                           int width,
                                           int stride,
                                           int x_screen, /* To align dither pattern */
                                           int y_screen);
/* Performance function to blit a YUV bitmap directly to the LCD */
/* So the LCD_WIDTH is now the height */
void lcd_blit_yuv(unsigned char * const src[3],
                  int src_x, int src_y, int stride,
                  int x, int y, int width, int height)
{
    /* Caches for chroma data so it only need be recaculated every other
       line */
    unsigned char const * yuv_src[3];
    off_t z;

#ifdef HAVE_LCD_ENABLE
    if (!lcd_on)
        return;
#endif

    /* Sorry, but width and height must be >= 2 or else */
    width &= ~1;
    height >>= 1;

    y = LCD_WIDTH - 1 - y;
    fb_data *dst = (fb_data*)FRAME + x * LCD_WIDTH + y;

    z = stride*src_y;
    yuv_src[0] = src[0] + z + src_x;
    yuv_src[1] = src[1] + (z >> 2) + (src_x >> 1);
    yuv_src[2] = src[2] + (yuv_src[1] - src[1]);

    if (lcd_yuv_options & LCD_YUV_DITHER)
    {
        do
        {
            lcd_write_yuv420_lines_odither(dst, yuv_src, width, stride, y, x);
            yuv_src[0] += stride << 1; /* Skip down two luma lines */
            yuv_src[1] += stride >> 1; /* Skip down one chroma line */
            yuv_src[2] += stride >> 1;
            dst -= 2;
            y -= 2;
        }
        while (--height > 0);
    }
    else
    {
        do
        {
            lcd_write_yuv420_lines(dst, yuv_src, width, stride);
            yuv_src[0] += stride << 1; /* Skip down two luma lines */
            yuv_src[1] += stride >> 1; /* Skip down one chroma line */
            yuv_src[2] += stride >> 1;
            dst -= 2;
        }
        while (--height > 0);
    }
}
