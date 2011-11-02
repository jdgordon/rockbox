/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright © 2011 by Amaury Pouly
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
#include "clkctrl-imx233.h"

#define __CLK_CLKGATE   (1 << 31)
#define __CLK_BUSY      (1 << 29)

void imx233_enable_xtal_clock(enum imx233_xtal_clkt_t xtal_clk, bool enable)
{
    if(enable)
        __REG_CLR(HW_CLKCTRL_XTAL) = xtal_clk;
    else
        __REG_SET(HW_CLKCTRL_XTAL) = xtal_clk;
}

void imx233_enable_clock(enum imx233_clock_t clk, bool enable)
{
    volatile uint32_t *REG;
    switch(clk)
    {
        case CLK_PIX: REG = &HW_CLKCTRL_PIX; break;
        case CLK_SSP: REG = &HW_CLKCTRL_SSP; break;
        default: return;
    }

    /* warning: some registers like HW_CLKCTRL_PIX don't have a CLR/SET variant ! */
    if(enable)
    {
        *REG = (*REG) & ~__CLK_CLKGATE;
        while((*REG) & __CLK_CLKGATE);
        while((*REG) & __CLK_BUSY);
    }
    else
    {
        *REG |= __CLK_CLKGATE;
        while(!((*REG) & __CLK_CLKGATE));
    }
}

void imx233_set_clock_divisor(enum imx233_clock_t clk, int div)
{
    switch(clk)
    {
        case CLK_PIX:
            __REG_CLR(HW_CLKCTRL_PIX) = HW_CLKCTRL_PIX__DIV_BM;
            __REG_SET(HW_CLKCTRL_PIX) = div;
            while(HW_CLKCTRL_PIX & __CLK_BUSY);
            break;
        case CLK_SSP:
            __REG_CLR(HW_CLKCTRL_SSP) = HW_CLKCTRL_SSP__DIV_BM;
            __REG_SET(HW_CLKCTRL_SSP) = div;
            while(HW_CLKCTRL_SSP & __CLK_BUSY);
            break;
        case CLK_CPU:
            __REG_CLR(HW_CLKCTRL_CPU) = HW_CLKCTRL_CPU__DIV_CPU_BM;
            __REG_SET(HW_CLKCTRL_CPU) = div;
            while(HW_CLKCTRL_CPU & HW_CLKCTRL_CPU__BUSY_REF_CPU);
            break;
        case CLK_AHB:
            __REG_CLR(HW_CLKCTRL_HBUS) = HW_CLKCTRL_HBUS__DIV_BM;
            __REG_SET(HW_CLKCTRL_HBUS) = div;
            while(HW_CLKCTRL_HBUS & __CLK_BUSY);
            break;
        default: return;
    }
}

void imx233_set_fractional_divisor(enum imx233_clock_t clk, int fracdiv)
{
    /* NOTE: HW_CLKCTRL_FRAC only support byte access ! */
    volatile uint8_t *REG;
    switch(clk)
    {
        case CLK_PIX: REG = &HW_CLKCTRL_FRAC_PIX; break;
        case CLK_IO: REG = &HW_CLKCTRL_FRAC_IO; break;
        case CLK_CPU: REG = &HW_CLKCTRL_FRAC_CPU; break;
        default: return;
    }

    if(fracdiv != 0)
        *REG = fracdiv;
    else
        *REG = HW_CLKCTRL_FRAC_XX__CLKGATEXX;;
}

void imx233_set_bypass_pll(enum imx233_clock_t clk, bool bypass)
{
    uint32_t msk;
    switch(clk)
    {
        case CLK_PIX: msk = HW_CLKCTRL_CLKSEQ__BYPASS_PIX; break;
        case CLK_SSP: msk = HW_CLKCTRL_CLKSEQ__BYPASS_SSP; break;
        case CLK_CPU: msk = HW_CLKCTRL_CLKSEQ__BYPASS_CPU; break;
        default: return;
    }

    if(bypass)
        __REG_SET(HW_CLKCTRL_CLKSEQ) = msk;
    else
        __REG_CLR(HW_CLKCTRL_CLKSEQ) = msk;
}

void imx233_enable_usb_pll(bool enable)
{
    if(enable)
        __REG_SET(HW_CLKCTRL_PLLCTRL0) = HW_CLKCTRL_PLLCTRL0__EN_USB_CLKS;
    else
        __REG_CLR(HW_CLKCTRL_PLLCTRL0) = HW_CLKCTRL_PLLCTRL0__EN_USB_CLKS;
}

