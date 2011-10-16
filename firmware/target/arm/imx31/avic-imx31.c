/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2007 by James Espinoza
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
#include <stdio.h>
#include "system.h"
#include "imx31l.h"
#include "avic-imx31.h"
#include "panic.h"
#include "debug.h"

#define avic ((unsigned long * const)AVIC_BASE_ADDR)
#define INTCNTL     (0x000 / sizeof (unsigned long)) /* 000h     */
#define NIMASK      (0x004 / sizeof (unsigned long)) /* 004h     */
#define INTENNUM    (0x008 / sizeof (unsigned long)) /* 008h     */
#define INTDISNUM   (0x00c / sizeof (unsigned long)) /* 00Ch     */
#define INTENABLE   (0x010 / sizeof (unsigned long)) /* 010h H,L */
#define INTTYPE     (0x018 / sizeof (unsigned long)) /* 018h H,L */
#define NIPRIORITY  (0x020 / sizeof (unsigned long)) /* 020h 7-0 */
#define NIVECSR     (0x040 / sizeof (unsigned long)) /* 040h     */
#define FIVECSR     (0x044 / sizeof (unsigned long)) /* 044h     */
#define INTSRC      (0x048 / sizeof (unsigned long)) /* 048h H,L */
#define INTFRC      (0x050 / sizeof (unsigned long)) /* 050h H,L */
#define NIPND       (0x058 / sizeof (unsigned long)) /* 058h H,L */
#define FIPND       (0x060 / sizeof (unsigned long)) /* 060h H,L */
#define VECTOR      (0x100 / sizeof (unsigned long)) /* 100h     */ 

static const char * const avic_int_names[64] =
{
    "RESERVED0", "RESERVED1",     "RESERVED2", "I2C3",
    "I2C2",      "MPEG4_ENCODER", "RTIC",      "FIR",
    "MMC/SDHC2", "MMC/SDHC1",     "I2C1",      "SSI2",
    "SSI1",      "CSPI2",         "CSPI1",     "ATA",
    "MBX",       "CSPI3",         "UART3",     "IIM",
    "SIM1",      "SIM2",          "RNGA",      "EVTMON",
    "KPP",       "RTC",           "PWN",       "EPIT2",
    "EPIT1",     "GPT",           "PWR_FAIL",  "CCM_DVFS",
    "UART2",     "NANDFC",        "SDMA",      "USB_HOST1",
    "USB_HOST2", "USB_OTG",       "RESERVED3", "MSHC1",
    "MSHC2",     "IPU_ERR",       "IPU",       "RESERVED4",
    "RESERVED5", "UART1",         "UART4",     "UART5",
    "ETC_IRQ",   "SCC_SCM",       "SCC_SMN",   "GPIO2",
    "GPIO1",     "CCM_CLK",       "PCMCIA",    "WDOG",
    "GPIO3",     "RESERVED6",     "EXT_PWMG",  "EXT_TEMP",
    "EXT_SENS1", "EXT_SENS2",     "EXT_WDOG",  "EXT_TV"
};

void UIE_VECTOR(void)
{
    unsigned long mode;
    int offset;

    asm volatile (
        "mrs    %0, cpsr      \n" /* Mask core IRQ/FIQ */
        "orr    %0, %0, #0xc0 \n"
        "msr    cpsr_c, %0    \n"
        "and    %0, %0, #0x1f \n" /* Get mode bits */
        : "=&r"(mode)
    );

    offset = mode == 0x11 ? (long)avic[FIVECSR] : ((long)avic[NIVECSR] >> 16);

    panicf("Unhandled %s %d: %s",
           mode == 0x11 ? "FIQ" : "IRQ", offset,
           offset >= 0 ? avic_int_names[offset] : "<Unknown>");
}

#if 0
/* We use the AVIC */
void __attribute__((interrupt("IRQ"))) irq_handler(void)
{
    int offset = (long)avic[NIVECSR] >> 16;

    if (offset == -1)
    {
        /* This is called occasionally for some unknown reason even with the
         * avic enabled but returning normally appears to cause no harm. The
         * KPP and ATA seem to have a part in it (common but multiplexed pins
         * that can interfere). It will be investigated more thoroughly but
         * for now it is simply an occasional irritant. */
        return;
    }

    disable_interrupt(IRQ_FIQ_STATUS);
    panicf("Unhandled IRQ %d in irq_handler: %s", offset,
           offset >= 0 ? avic_int_names[offset] : "<Unknown>");
}
#endif /* 0 */

#if 0
/* Accoring to section 9.3.5 of the UM, the AVIC doesn't accelerate
 * fast interrupts and they must be dispatched */
void __attribute__((naked)) fiq_handler(void)
{
    asm volatile (
        "mov r10, #0x68000000      \n" /* load AVIC base address */
        "ldr r9, [r10, #0x44]      \n" /* read FIVECSR of AVIC */
        "add r10, r10, #0x100      \n" /* move pointer to base of VECTOR table */
        "ldr r8, [r10, r9, lsl #2] \n" /* read FIQ vector from VECTOR table */
        "bx  r8                    \n" /* jump to FIQ service routine */
    );
}
#endif /* 0 */

void INIT_ATTR avic_init(void)
{
    int i;

    /* Disable all interrupts and set to unhandled */
    avic_disable_int(INT_ALL);

    /* Reset AVIC control */
    avic[INTCNTL] = 0;

    /* Init all interrupts to type IRQ */
    avic_set_int_type(INT_ALL, INT_TYPE_IRQ);

    /* Set all normal to lowest priority */
    for (i = 0; i < 8; i++)
        avic[NIPRIORITY + i] = 0;

    /* Set NM bit to enable VIC. Mask fast interrupts. Core arbiter rise
     * for normal interrupts (for lowest latency). */
    avic[INTCNTL] |= AVIC_INTCNTL_NM | AVIC_INTCNTL_FIDIS |
                     AVIC_INTCNTL_NIAD;

    /* Enable VE bit in CP15 Control reg to enable VIC */
    asm volatile (
        "mrc p15, 0, r0, c1, c0, 0 \n"
        "orr r0, r0, #(1 << 24)    \n"
        "mcr p15, 0, r0, c1, c0, 0 \n"
        : : : "r0");

    /* Enable normal interrupts at all priorities */
    avic[NIMASK] = AVIC_NIL_ENABLE;
}

void avic_set_int_priority(enum IMX31_INT_LIST ints,
                           unsigned long ni_priority)
{
    volatile unsigned long * const reg = &avic[NIPRIORITY + 7 - (ints >> 3)];
    unsigned int shift = (ints & 0x7) << 2;
    unsigned long mask = 0xful << shift;
    *reg = (*reg & ~mask) | ((ni_priority << shift) & mask);
}

void avic_enable_int(enum IMX31_INT_LIST ints, enum INT_TYPE intstype,
                     unsigned long ni_priority, void (*handler)(void))
{
    int oldstatus = disable_interrupt_save(IRQ_FIQ_STATUS);

    if (ints != INT_ALL) /* No mass-enable allowed */
    {
        avic_set_int_type(ints, intstype);
        avic[VECTOR + ints] = (unsigned long)handler;
        avic[INTENNUM] = ints;
        avic_set_int_priority(ints, ni_priority);
    }

    restore_interrupt(oldstatus);
}

void avic_disable_int(enum IMX31_INT_LIST ints)
{
    if (ints == INT_ALL)
    {
        int i;
        for (i = 0; i < 64; i++)
        {
            avic[INTDISNUM] = i;
            avic[VECTOR + i] = (unsigned long)UIE_VECTOR;
        }
    }
    else
    {
        avic[INTDISNUM] = ints;
        avic[VECTOR + ints] = (unsigned long)UIE_VECTOR;
    }
}

static void set_int_type(int i, enum INT_TYPE intstype)
{
    /* INTTYPEH: vectors 63-32, INTTYPEL: vectors 31-0 */
    volatile unsigned long * const reg = &avic[INTTYPE + 1 - (i >> 5)];
    unsigned long val = 1L << (i & 0x1f);

    if (intstype == INT_TYPE_IRQ)
        val = *reg & ~val;
    else
        val = *reg | val;

    *reg = val;
}

void avic_set_int_type(enum IMX31_INT_LIST ints, enum INT_TYPE intstype)
{
    int oldstatus = disable_interrupt_save(IRQ_FIQ_STATUS);

    if (ints == INT_ALL)
    {
        int i;
        for (i = 0; i < 64; i++)
            set_int_type(i, intstype);
    }
    else
    {
        set_int_type(ints, intstype);
    }

    restore_interrupt(oldstatus);
}

void avic_set_ni_level(int level)
{
    if (level < 0)
        level = 0x1f; /* -1 */
    else if (level > 15)
        level = 15;

    avic[NIMASK] = level;
}
