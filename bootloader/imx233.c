/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2011 by amaury Pouly
 *
 * Based on Rockbox iriver bootloader by Linus Nielsen Feltzing
 * and the ipodlinux bootloader by Daniel Palffy and Bernard Leach
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
#include <system.h>
#include <inttypes.h>
#include "config.h"
#include "gcc_extensions.h"
#include "lcd.h"
#include "backlight.h"
#include "button-target.h"
#include "common.h"
#include "storage.h"
#include "disk.h"
#include "panic.h"
#include "power.h"
#include "system-target.h"
#include "fmradio_i2c.h"

#include "usb.h"
#include "usb-target.h"

extern char loadaddress[];
extern char loadaddressend[];

#ifdef HAVE_BOOTLOADER_USB_MODE
static void usb_mode(int connect_timeout)
{
    int button;
    
    usb_init();
    usb_start_monitoring();

    /* Wait for threads to connect or cable is pulled */
    printf("USB: Connecting");

    long end_tick = current_tick + connect_timeout;

    while(1)
    {
        button = button_get_w_tmo(HZ/10);

        if(button == SYS_USB_CONNECTED)
            break; /* Hit */

        if(TIME_AFTER(current_tick, end_tick))
        {
            /* Timed out waiting for the connect - will happen when connected
             * to a charger through the USB port */
            printf("USB: Timed out");
            break;
        }

        if(!usb_plugged())
            break; /* Cable pulled */
    }

    if(button == SYS_USB_CONNECTED)
    {
        /* Got the message - wait for disconnect */
        printf("Bootloader USB mode");

        usb_acknowledge(SYS_USB_CONNECTED_ACK);

        while(1)
        {
            button = button_get_w_tmo(HZ/2);
            if(button == SYS_USB_DISCONNECTED)
                break;
        }
    }

    /* Put drivers initialized for USB connection into a known state */
    usb_close();
}
#else /* !HAVE_BOOTLOADER_USB_MODE */
static void usb_mode(int connect_timeout)
{
    (void) connect_timeout;
}
#endif /* HAVE_BOOTLOADER_USB_MODE */

void main(uint32_t arg, uint32_t addr) NORETURN_ATTR;
void main(uint32_t arg, uint32_t addr)
{
    unsigned char* loadbuffer;
    int buffer_size;
    void(*kernel_entry)(void);
    int ret;

    system_init();
    kernel_init();

    power_init();
    enable_irq();

    lcd_init();
    lcd_clear_display();
    lcd_update();

    backlight_init();

    button_init();

    //button_debug_screen();
    printf("arg=%x addr=%x", arg, addr);

#ifdef SANSA_FUZEPLUS
    extern void imx233_mmc_disable_window(void);
    if(arg == 0xfee1dead)
    {
        printf("Disable MMC window.");
        imx233_mmc_disable_window();
    }
#endif

    ret = storage_init();
    if(ret < 0)
        error(EATA, ret, true);

    /* NOTE: allow disk_init and disk_mount_all to fail since we can do USB after.
     * We need this order to determine the correct logical sector size */
    while(!disk_init(IF_MV(0)))
        printf("disk_init failed!");

    if((ret = disk_mount_all()) <= 0)
        error(EDISK, ret, false);

    if(usb_plugged())
        usb_mode(HZ);

    printf("Loading firmware");

    loadbuffer = (unsigned char*)loadaddress;
    buffer_size = (int)(loadaddressend - loadaddress);

    while((ret = load_firmware(loadbuffer, BOOTFILE, buffer_size)) < 0)
    {
        error(EBOOTFILE, ret, true);
    }

    kernel_entry = (void*) loadbuffer;
    printf("Executing");
    disable_interrupt(IRQ_FIQ_STATUS);
    commit_discard_idcache();
    kernel_entry();
    printf("ERR: Failed to boot");

    /* never returns */
    while(1) ;
}
