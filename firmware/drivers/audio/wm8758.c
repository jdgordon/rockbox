/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Driver for WM8758 audio codec - based on datasheet for WM8983
 *
 * Based on code from the ipodlinux project - http://ipodlinux.org/
 * Adapted for Rockbox in December 2005
 *
 * Original file: linux/arch/armnommu/mach-ipod/audio.c
 *
 * Copyright (c) 2003-2005 Bernard Leach (leachbj@bouncycastle.org)
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include "system.h"
#include "string.h"
#include "audio.h"

#include "wmcodec.h"
#include "audiohw.h"
#include "i2s.h"

const struct sound_settings_info audiohw_settings[] = {
    [SOUND_VOLUME]        = {"dB", 0,  1, -58,   6, -25},
    [SOUND_BASS]          = {"dB", 0,  1, -12,  12,   0},
    [SOUND_TREBLE]        = {"dB", 0,  1, -12,  12,   0},
    [SOUND_BALANCE]       = {"%",  0,  1,-100, 100,   0},
    [SOUND_CHANNELS]      = {"",   0,  1,   0,   5,   0},
    [SOUND_STEREO_WIDTH]  = {"%",  0,  5,   0, 250, 100},
    [SOUND_LEFT_GAIN]     = {"dB", 1,  1,-128,  96,   0},
    [SOUND_RIGHT_GAIN]    = {"dB", 1,  1,-128,  96,   0},
    [SOUND_MIC_GAIN]      = {"dB", 1,  1,-128, 108,  16},
    [SOUND_BASS_CUTOFF]   = {"",   0,  1,   1,   4,   1},
    [SOUND_TREBLE_CUTOFF] = {"",   0,  1,   1,   4,   1},
};

/* shadow registers */
unsigned int eq1_reg;
unsigned int eq5_reg;

/* convert tenth of dB volume (-57..6) to master volume register value */
int tenthdb2master(int db)
{
    /* +6 to -57dB in 1dB steps == 64 levels = 6 bits */
    /* 0111111 == +6dB  (0x3f) = 63) */
    /* 0111001 == 0dB   (0x39) = 57) */
    /* 0000001 == -56dB (0x01) = */
    /* 0000000 == -57dB (0x00) */

    /* 1000000 == Mute (0x40) */

    if (db < VOLUME_MIN) {
        return 0x40;
    } else {
        return((db/10)+57);
    }
}

/* convert tenth of dB volume (-780..0) to mixer volume register value */
int tenthdb2mixer(int db)
{
    if (db < -660)                 /* 1.5 dB steps */
        return (2640 - db) / 15;
    else if (db < -600)            /* 0.75 dB steps */
        return (990 - db) * 2 / 15;
    else if (db < -460)            /* 0.5 dB steps */
        return (460 - db) / 5; 
    else                           /* 0.25 dB steps */
        return -db * 2 / 5;
}

#define IPOD_PCM_LEVEL 0x65       /* -6dB */

//#define BASSCTRL   0x
//#define TREBCTRL   0x0b

/* Silently enable / disable audio output */
void audiohw_enable_output(bool enable)
{
    if (enable) 
    {
        /* reset the I2S controller into known state */
        i2s_reset();

        /* TODO: Review the power-up sequence to prevent pops */

        wmcodec_write(RESET, 0x1ff);    /*Reset*/
    
        wmcodec_write(PWRMGMT1, 0x2b);
        wmcodec_write(PWRMGMT2, 0x180);
        wmcodec_write(PWRMGMT3, 0x6f);

        wmcodec_write(AINTFCE, 0x10);
        wmcodec_write(CLKCTRL, 0x49);

        wmcodec_write(OUTCTRL, 1);

        /* The iPod can handle multiple frequencies, but fix at 44.1KHz
           for now */
        audiohw_set_sample_rate(WM8758_44100HZ);
    
        wmcodec_write(LOUTMIX,0x1); /* Enable mixer */
        wmcodec_write(ROUTMIX,0x1); /* Enable mixer */
        audiohw_mute(0);
    } else {
        audiohw_mute(1);
    }
}

int audiohw_set_master_vol(int vol_l, int vol_r)
{
    /* OUT1 */
    wmcodec_write(LOUT1VOL, 0x080 | vol_l);
    wmcodec_write(ROUT1VOL, 0x180 | vol_r);

    return 0;
}

int audiohw_set_lineout_vol(int vol_l, int vol_r)
{
    /* OUT2 */
    wmcodec_write(LOUT2VOL, vol_l);
    wmcodec_write(ROUT2VOL, 0x100 | vol_r);
    
    return 0;
}

int audiohw_set_mixer_vol(int channel1, int channel2)
{
    (void)channel1;
    (void)channel2;

    return 0;
}

void audiohw_set_bass(int value)
{
    eq1_reg = (eq1_reg & ~EQ_GAIN_MASK) | EQ_GAIN_VALUE(value);
    wmcodec_write(EQ1, 0x100 | eq1_reg);
}

void audiohw_set_bass_cutoff(int value)
{
    eq1_reg = (eq1_reg & ~EQ_CUTOFF_MASK) | EQ_CUTOFF_VALUE(value);
    wmcodec_write(EQ1, 0x100 | eq1_reg);
}

void audiohw_set_treble(int value)
{
    eq5_reg = (eq5_reg & ~EQ_GAIN_MASK) | EQ_GAIN_VALUE(value);
    wmcodec_write(EQ5, eq5_reg); 
}

void audiohw_set_treble_cutoff(int value)
{
    eq5_reg = (eq5_reg & ~EQ_CUTOFF_MASK) | EQ_CUTOFF_VALUE(value);
    wmcodec_write(EQ5, eq5_reg); 
}

void audiohw_mute(bool mute)
{
    if (mute)
    {
        /* Set DACMU = 1 to soft-mute the audio DACs. */
        wmcodec_write(DACCTRL, 0x40);
    } else {
        /* Set DACMU = 0 to soft-un-mute the audio DACs. */
        wmcodec_write(DACCTRL, 0x0);
    }
}

/* Nice shutdown of WM8758 codec */
void audiohw_close(void)
{
    audiohw_mute(1);

    wmcodec_write(PWRMGMT3, 0x0);

    wmcodec_write(PWRMGMT1, 0x0);

    wmcodec_write(PWRMGMT2, 0x40);
}

/* Change the order of the noise shaper, 5th order is recommended above 32kHz */
void audiohw_set_nsorder(int order)
{
    (void)order;
}

/* Note: Disable output before calling this function */
void audiohw_set_sample_rate(int sampling_control)
{
    /**** We force 44.1KHz for now. ****/
    (void)sampling_control;

    /* set clock div */
    wmcodec_write(CLKCTRL, 1 | (0 << 2) | (2 << 5));

    /* setup PLL for MHZ=11.2896 */
    wmcodec_write(PLLN, (1 << 4) | 0x7);
    wmcodec_write(PLLK1, 0x21);
    wmcodec_write(PLLK2, 0x161);
    wmcodec_write(PLLK3, 0x26);

    /* set clock div */
    wmcodec_write(CLKCTRL, 1 | (1 << 2) | (2 << 5) | (1 << 8));

    /* set srate */
    wmcodec_write(SRATECTRL, (0 << 1));
}

void audiohw_enable_recording(bool source_mic)
{
    (void)source_mic; /* We only have a line-in (I think) */
 
    /* reset the I2S controller into known state */
    i2s_reset();
 
    wmcodec_write(RESET, 0x1ff);    /*Reset*/
    
    wmcodec_write(PWRMGMT1, 0x2b);
    wmcodec_write(PWRMGMT2, 0x18f);  /* Enable ADC - 0x0c enables left/right PGA input, and 0x03 turns on power to the ADCs */
    wmcodec_write(PWRMGMT3, 0x6f);

    wmcodec_write(AINTFCE, 0x10);
    wmcodec_write(CLKCTRL, 0x49);

    wmcodec_write(OUTCTRL, 1);

    /* The iPod can handle multiple frequencies, but fix at 44.1KHz
       for now */
    audiohw_set_sample_rate(WM8758_44100HZ);

    wmcodec_write(INCTRL,0x44);  /* Connect L2 and R2 inputs */

    /* Set L2/R2_2BOOSTVOL to 0db (bits 4-6) */
    /* 000 = disabled
       001 = -12dB
       010 = -9dB
       011 = -6dB
       100 = -3dB
       101 = 0dB
       110 = 3dB
       111 = 6dB
    */
    wmcodec_write(LADCBOOST,0x50);
    wmcodec_write(RADCBOOST,0x50);

    /* Set L/R input PGA Volume to 0db */
    //    wm8758_write(LINPGAVOL,0x3f);
    //    wm8758_write(RINPGAVOL,0x13f);

    /* Enable monitoring */
    wmcodec_write(LOUTMIX,0x17); /* Enable output mixer - BYPL2LMIX @ 0db*/
    wmcodec_write(ROUTMIX,0x17); /* Enable output mixer - BYPR2RMIX @ 0db*/

    audiohw_mute(0);
}

void audiohw_disable_recording(void) {
    audiohw_mute(1);

    wmcodec_write(PWRMGMT3, 0x0);

    wmcodec_write(PWRMGMT1, 0x0);
 
    wmcodec_write(PWRMGMT2, 0x40);
}

void audiohw_set_recvol(int left, int right, int type) {

    (void)left;
    (void)right;
    (void)type;
}

void audiohw_set_monitor(bool enable) {

    (void)enable;
}

