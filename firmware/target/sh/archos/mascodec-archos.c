/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2002 by Linus Nielsen Feltzing
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
#include "stdbool.h"
#include "config.h"
#include "sh7034.h"
#include "i2c.h"
#include "debug.h"
#include "mas35xx.h"
#include "kernel.h"
#include "system.h"
#include "hwcompat.h"

static int mas_devread(unsigned long *dest, int len);

int mas_default_read(unsigned short *buf)
{
    unsigned char *dest = (unsigned char *)buf;
    int ret = 0;

    i2c_begin();
    
    i2c_start();
    i2c_outb(MAS_DEV_WRITE);
    if (i2c_getack()) {
        i2c_outb(MAS_DATA_READ);
        if (i2c_getack()) {
            i2c_start();
            i2c_outb(MAS_DEV_READ);
            if (i2c_getack()) {
                    dest[0] = i2c_inb(0);
                    dest[1] = i2c_inb(1);
            }
            else
                ret = -3;
        }
        else
            ret = -2;
    }
    else
        ret = -1;
    
    i2c_stop();

    i2c_end();
    return ret;
}

int mas_run(unsigned short address)
{
    int ret = 0;
    unsigned char buf[3];

    i2c_begin();
    
    buf[0] = MAS_DATA_WRITE;
    buf[1] = address >> 8;
    buf[2] = address & 0xff;

    /* send run command */
    if (i2c_write(MAS_DEV_WRITE,buf,3))
    {
        ret = -1;
    }

    i2c_end();
    return ret;
}

/* note: 'len' is number of 32-bit words, not number of bytes! */
int mas_readmem(int bank, int addr, unsigned long* dest, int len)
{
    int ret = 0;
    unsigned char buf[7];

    i2c_begin();

    buf[0] = MAS_DATA_WRITE;
    buf[1] = bank?MAS_CMD_READ_D1_MEM:MAS_CMD_READ_D0_MEM;
    buf[2] = 0x00;
    buf[3] = (len & 0xff00) >> 8;
    buf[4] = len & 0xff;
    buf[5] = (addr & 0xff00) >> 8;
    buf[6] = addr & 0xff;

    /* send read command */
    if (i2c_write(MAS_DEV_WRITE,buf,7))
    {
        ret = -1;
    }

    ret = mas_devread(dest, len);

    i2c_end();
    return ret;
}

/* note: 'len' is number of 32-bit words, not number of bytes! */
int mas_writemem(int bank, int addr, const unsigned long* src, int len)
{
    int ret = 0;
    int i, j;
    unsigned char buf[60];
    const unsigned char* ptr = (const unsigned char*)src;

    i2c_begin();

    i=0;
    buf[i++] = MAS_DATA_WRITE;
    buf[i++] = bank?MAS_CMD_WRITE_D1_MEM:MAS_CMD_WRITE_D0_MEM;
    buf[i++] = 0x00;
    buf[i++] = (len & 0xff00) >> 8;
    buf[i++] = len & 0xff;
    buf[i++] = (addr & 0xff00) >> 8;
    buf[i++] = addr & 0xff;

    j = 0;
    while(len--) {
#if (CONFIG_CODEC == MAS3587F) || (CONFIG_CODEC == MAS3539F)
        buf[i++] = 0;
        buf[i++] = ptr[j+1];
        buf[i++] = ptr[j+2];
        buf[i++] = ptr[j+3];
#else
        buf[i++] = ptr[j+2];
        buf[i++] = ptr[j+3];
        buf[i++] = 0;
        buf[i++] = ptr[j+1];
#endif
        j += 4;
    }
    
    /* send write command */
    if (i2c_write(MAS_DEV_WRITE,buf,i))
    {
        ret = -1;
    }

    i2c_end();
    return ret;
}

int mas_readreg(int reg)
{
    int ret = 0;
    unsigned char buf[16];
    unsigned long value;

    i2c_begin();

    buf[0] = MAS_DATA_WRITE;
    buf[1] = MAS_CMD_READ_REG | (reg >> 4);
    buf[2] = (reg & 0x0f) << 4;

    /* send read command */
    if (i2c_write(MAS_DEV_WRITE,buf,3))
    {
        ret = -1;
    }
    else
    {
        if(mas_devread(&value, 1))
        {
            ret = -2;
        }
        else
        {
            ret = value;
        }
    }

    i2c_end();
    return ret;
}

int mas_writereg(int reg, unsigned int val)
{
    int ret = 0;
    unsigned char buf[5];

    i2c_begin();

    buf[0] = MAS_DATA_WRITE;
    buf[1] = MAS_CMD_WRITE_REG | (reg >> 4);
#if (CONFIG_CODEC == MAS3587F) || (CONFIG_CODEC == MAS3539F)
    buf[2] = ((reg & 0x0f) << 4) | (val >> 16 & 0x0f);
    buf[3] = (val >> 8) & 0xff;
    buf[4] = val & 0xff;
#else
    buf[2] = ((reg & 0x0f) << 4) | (val & 0x0f);
    buf[3] = (val >> 12) & 0xff;
    buf[4] = (val >> 4) & 0xff;
#endif

    /* send write command */
    if (i2c_write(MAS_DEV_WRITE,buf,5))
    {
        ret = -1;
    }

    i2c_end();
    return ret;
}

/* note: 'len' is number of 32-bit words, not number of bytes! */
static int mas_devread(unsigned long *dest, int len)
{
    int ret = 0;
    unsigned char* ptr = (unsigned char*)dest;
    int i;
    
    /* handle read-back */
    /* Remember, the MAS values are only 20 bits, so we set
       the upper 12 bits to 0 */
    i2c_start();
    i2c_outb(MAS_DEV_WRITE);
    if (i2c_getack()) {
        i2c_outb(MAS_DATA_READ);
        if (i2c_getack()) {
            i2c_start();
            i2c_outb(MAS_DEV_READ);
            if (i2c_getack()) {
                for (i=0;len;i++) {
                    len--;
#if (CONFIG_CODEC == MAS3587F) || (CONFIG_CODEC == MAS3539F)
                    i2c_inb(0); /* Dummy read */
                    ptr[i*4+0] = 0;
                    ptr[i*4+1] = i2c_inb(0) & 0x0f;
                    ptr[i*4+2] = i2c_inb(0);
                    if(len)
                        ptr[i*4+3] = i2c_inb(0);
                    else
                        ptr[i*4+3] = i2c_inb(1); /* NAK the last byte */
#else
                    ptr[i*4+2] = i2c_inb(0);
                    ptr[i*4+3] = i2c_inb(0);
                    ptr[i*4+0] = i2c_inb(0);
                    if(len)
                        ptr[i*4+1] = i2c_inb(0);
                    else
                        ptr[i*4+1] = i2c_inb(1); /* NAK the last byte */
#endif
                }
            }
            else
                ret = -3;
        }
        else
            ret = -2;
    }
    else
        ret = -1;
    
    i2c_stop();

    return ret;
}

void mas_reset(void)
{
    or_b(0x01, &PAIORH);

#if CONFIG_CODEC == MAS3507D
    /* PB5 is "MAS enable". make it GPIO output and high */
    PBCR2 &= ~0x0c00;
    or_b(0x20, &PBIORL);
    or_b(0x20, &PBDRL);

    and_b(~0x01, &PADRH);
    sleep(HZ/100);
    or_b(0x01, &PADRH);
    sleep(HZ/5);
#elif (CONFIG_CODEC == MAS3587F) || (CONFIG_CODEC == MAS3539F)
    if (HW_MASK & ATA_ADDRESS_200)
    {
        and_b(~0x01, &PADRH);
        sleep(HZ/100);
        or_b(0x01, &PADRH);
        sleep(HZ/5);
    }
    else
    {
        /* Older recorder models don't invert the POR signal */
        or_b(0x01, &PADRH);
        sleep(HZ/100);
        and_b(~0x01, &PADRH);
        sleep(HZ/5);
    }
#endif
}

#if (CONFIG_CODEC == MAS3587F) || (CONFIG_CODEC == MAS3539F)
int mas_direct_config_read(unsigned char reg)
{
    int ret = 0;
    unsigned char tmp[2];
    
    i2c_begin();

    i2c_start();
    i2c_outb(MAS_DEV_WRITE);
    if (i2c_getack()) {
        i2c_outb(reg);
        if (i2c_getack()) {
            i2c_start();
            i2c_outb(MAS_DEV_READ);
            if (i2c_getack()) {
                tmp[0] = i2c_inb(0);
                tmp[1] = i2c_inb(1); /* NAK the last byte */
                ret = (tmp[0] << 8) | tmp[1];
            }
            else
                ret = -3;
        }
        else
            ret = -2;
    }
    else
        ret = -1;
    
    i2c_stop();

    i2c_end();
    return ret;
}

int mas_direct_config_write(unsigned char reg, unsigned int val)
{
    int ret = 0;
    unsigned char buf[3];

    i2c_begin();

    buf[0] = reg;
    buf[1] = (val >> 8) & 0xff;
    buf[2] = val & 0xff;

    /* send write command */
    if (i2c_write(MAS_DEV_WRITE,buf,3))
    {
        ret = -1;
    }

    i2c_end();
    return ret;
}

int mas_codec_writereg(int reg, unsigned int val)
{
    int ret = 0;
    unsigned char buf[5];

    i2c_begin();

    buf[0] = MAS_CODEC_WRITE;
    buf[1] = (reg >> 8) & 0xff;
    buf[2] = reg & 0xff;
    buf[3] = (val >> 8) & 0xff;
    buf[4] = val & 0xff;

    /* send write command */
    if (i2c_write(MAS_DEV_WRITE,buf,5))
    {
        ret = -1;
    }
    
    i2c_end();
    return ret;
}

int mas_codec_readreg(int reg)
{
    int ret = 0;
    unsigned char buf[16];
    unsigned char tmp[2];

    i2c_begin();

    buf[0] = MAS_CODEC_WRITE;
    buf[1] = (reg >> 8) & 0xff;
    buf[2] = reg & 0xff;

    /* send read command */
    if (i2c_write(MAS_DEV_WRITE,buf,3))
    {
        ret = -1;
    }
    else
    {
        i2c_start();
        i2c_outb(MAS_DEV_WRITE);
        if (i2c_getack()) {
            i2c_outb(MAS_CODEC_READ);
            if (i2c_getack()) {
                i2c_start();
                i2c_outb(MAS_DEV_READ);
                if (i2c_getack()) {
                    tmp[0] = i2c_inb(0);
                    tmp[1] = i2c_inb(1); /* NAK the last byte */
                    ret = (tmp[0] << 8) | tmp[1];
                }
                else
                    ret = -4;
            }
            else
                ret = -3;
        }
        else
            ret = -2;

        i2c_stop();
    }
    
    i2c_end();
    return ret;
}

unsigned long mas_readver(void)
{
    int ret = 0;
    unsigned char buf[16];
    unsigned long value;

    i2c_begin();

    buf[0] = MAS_DATA_WRITE;
    buf[1] = MAS_CMD_READ_IC_VER;
    buf[2] = 0;

    /* send read command */
    if (i2c_write(MAS_DEV_WRITE,buf,3))
    {
        ret = -1;
    }
    else
    {
        if(mas_devread(&value, 1))
        {
            ret = -2;
        }
        else
        {
            ret = value;
        }
    }

    i2c_end();
    return ret;
}

#endif

#if CONFIG_TUNER & S1A0903X01
static int pllfreq;

void mas_store_pllfreq(int freq)
{
    pllfreq = freq;
}

int mas_get_pllfreq(void)
{
    return pllfreq;
}
#endif



