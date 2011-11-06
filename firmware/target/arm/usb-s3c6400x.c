/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2009 by Michael Sparmann
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
#include "usb.h"
#include "usb-target.h"
#include "usb_drv.h"

#include "cpu.h"
#include "system.h"
#include "kernel.h"
#include "panic.h"

#include "usb-s3c6400x.h"

#ifdef HAVE_USBSTACK
#include "usb_ch9.h"
#include "usb_core.h"
#include <inttypes.h>
#include "power.h"

struct ep_type
{
    bool active;
    bool busy;
    bool done;
    int rc;
    int size;
    struct semaphore complete;
} ;

static struct ep_type endpoints[USB_NUM_ENDPOINTS];
static struct usb_ctrlrequest ctrlreq USB_DEVBSS_ATTR;

int usb_drv_port_speed(void)
{
    return (DSTS & 2) == 0 ? 1 : 0;
}

static void reset_endpoints(int reinit)
{
    unsigned int i;
    for (i = 0; i < sizeof(endpoints)/sizeof(struct ep_type); i++)
    {
        if (reinit) endpoints[i].active = false;
        endpoints[i].busy = false;
        endpoints[i].rc = -1;
        endpoints[i].done = true;
        semaphore_release(&endpoints[i].complete);
    }
    DIEPCTL0 = 0x8800;  /* EP0 IN ACTIVE NEXT=1 */
    DOEPCTL0 = 0x8000;  /* EP0 OUT ACTIVE */
    DOEPTSIZ0 = 0x20080040;  /* EP0 OUT Transfer Size:
                                64 Bytes, 1 Packet, 1 Setup Packet */
    DOEPDMA0 = &ctrlreq;
    DOEPCTL0 |= 0x84000000;  /* EP0 OUT ENABLE CLEARNAK */
    if (reinit)
    {
        /* The size is getting set to zero, because we don't know
           whether we are Full Speed or High Speed at this stage */
        /* EP1 IN INACTIVE DATA0 SIZE=0 NEXT=3 */
        DIEPCTL1 = 0x10001800;
        /* EP2 OUT INACTIVE DATA0 SIZE=0 */
        DOEPCTL2 = 0x10000000;
        /* EP3 IN INACTIVE DATA0 SIZE=0 NEXT=0 */
        DIEPCTL3 = 0x10000000;
        /* EP4 OUT INACTIVE DATA0 SIZE=0 */
        DOEPCTL4 = 0x10000000;
    }
    else
    {
        /* INACTIVE DATA0 */
        DIEPCTL1 = (DIEPCTL1 & ~0x00008000) | 0x10000000;
        DOEPCTL2 = (DOEPCTL2 & ~0x00008000) | 0x10000000;
        DIEPCTL3 = (DIEPCTL3 & ~0x00008000) | 0x10000000;
        DOEPCTL4 = (DOEPCTL4 & ~0x00008000) | 0x10000000;
    }
    DAINTMSK = 0xFFFFFFFF;  /* Enable interrupts on all EPs */
}

int usb_drv_request_endpoint(int type, int dir)
{
    size_t ep;
    int ret = -1;

    if (dir == USB_DIR_IN) ep = 1;
    else ep = 2;

    while (ep < USB_NUM_ENDPOINTS)
    {
        if (!endpoints[ep].active)
        {
            endpoints[ep].active = true;
            ret = ep | dir;
            uint32_t newbits = (type << 18) | 0x10000000;
            if (dir) DIEPCTL(ep) = (DIEPCTL(ep) & ~0x000C0000) | newbits;
            else DOEPCTL(ep) = (DOEPCTL(ep) & ~0x000C0000) | newbits;
            break;
        }
        ep += 2;
    }

    return ret;
}

void usb_drv_release_endpoint(int ep)
{
    ep = ep & 0x7f;

    if (ep < 1 || ep > USB_NUM_ENDPOINTS) return;

    endpoints[ep].active = false;
}

static void usb_reset(void)
{
    DCTL = 0x802;  /* Soft Disconnect */

    OPHYPWR = 0;  /* PHY: Power up */
    udelay(10);
    OPHYUNK1 = 1;
    OPHYUNK2 = 0xE3F;
    ORSTCON = 1;  /* PHY: Assert Software Reset */
    udelay(10);
    ORSTCON = 0;  /* PHY: Deassert Software Reset */
    OPHYUNK3 = 0x600;
    OPHYCLK = SYNOPSYSOTG_CLOCK;
    udelay(400);

    GRSTCTL = 1;  /* OTG: Assert Software Reset */
    while (GRSTCTL & 1);  /* Wait for OTG to ack reset */
    while (!(GRSTCTL & 0x80000000));  /* Wait for OTG AHB master idle */

    GRXFSIZ = 0x00000200;  /* RX FIFO: 512 bytes */
    GNPTXFSIZ = 0x02000200;  /* Non-periodic TX FIFO: 512 bytes */
    GAHBCFG = SYNOPSYSOTG_AHBCFG;
    GUSBCFG = 0x1408;  /* OTG: 16bit PHY and some reserved bits */

    DCFG = 4;  /* Address 0 */
    DCTL = 0x800;  /* Soft Reconnect */
    DIEPMSK = 0x0D;  /* IN EP interrupt mask */
    DOEPMSK = 0x0D;  /* IN EP interrupt mask */
    DAINTMSK = 0xFFFFFFFF;  /* Enable interrupts on all endpoints */
    GINTMSK = 0xC3000;  /* Interrupt mask: IN event, OUT event, bus reset */

    reset_endpoints(1);
}

/* IRQ handler */
void INT_USB_FUNC(void)
{
    int i;
    uint32_t ints = GINTSTS;
    uint32_t epints;
    if (ints & 0x1000)  /* bus reset */
    {
        DCFG = 4;  /* Address 0 */
        reset_endpoints(1);
        usb_core_bus_reset();
    }

    if (ints & 0x2000)  /* enumeration done, we now know the speed */
    {
        /* Set up the maximum packet sizes accordingly */
        uint32_t maxpacket = usb_drv_port_speed() ? 512 : 64;
        DIEPCTL1 = (DIEPCTL1 & ~0x000003FF) | maxpacket;
        DOEPCTL2 = (DOEPCTL2 & ~0x000003FF) | maxpacket;
        DIEPCTL3 = (DIEPCTL3 & ~0x000003FF) | maxpacket;
        DOEPCTL4 = (DOEPCTL4 & ~0x000003FF) | maxpacket;
    }

    if (ints & 0x40000)  /* IN EP event */
        for (i = 0; i < 4; i += i + 1)  // 0, 1, 3
            if ((epints = DIEPINT(i)))
            {
                if (epints & 1)  /* Transfer completed */
                {
                    invalidate_dcache();
                    int bytes = endpoints[i].size - (DIEPTSIZ(i) & 0x3FFFF);
                    if (endpoints[i].busy)
                    {
                        endpoints[i].busy = false;
                        endpoints[i].rc = 0;
                        endpoints[i].done = true;
                        usb_core_transfer_complete(i, USB_DIR_IN, 0, bytes);
                        semaphore_release(&endpoints[i].complete);
                    }
                }
                if (epints & 4)  /* AHB error */
                    panicf("USB: AHB error on IN EP%d", i);
                if (epints & 8)  /* Timeout */
                {
                    if (endpoints[i].busy)
                    {
                        endpoints[i].busy = false;
                        endpoints[i].rc = 1;
                        endpoints[i].done = true;
                        semaphore_release(&endpoints[i].complete);
                    }
                }
                DIEPINT(i) = epints;
            }

    if (ints & 0x80000)  /* OUT EP event */
        for (i = 0; i < USB_NUM_ENDPOINTS; i += 2)
            if ((epints = DOEPINT(i)))
            {
                if (epints & 1)  /* Transfer completed */
                {
                    invalidate_dcache();
                    int bytes = endpoints[i].size - (DOEPTSIZ(i) & 0x3FFFF);
                    if (endpoints[i].busy)
                    {
                        endpoints[i].busy = false;
                        endpoints[i].rc = 0;
                        endpoints[i].done = true;
                        usb_core_transfer_complete(i, USB_DIR_OUT, 0, bytes);
                        semaphore_release(&endpoints[i].complete);
                    }
                }
                if (epints & 4)  /* AHB error */
                    panicf("USB: AHB error on OUT EP%d", i);
                if (epints & 8)  /* SETUP phase done */
                {
                    invalidate_dcache();
                    if (i == 0)
                    {
                        if (ctrlreq.bRequest == 5)
                        {
                            /* Already set the new address here,
                               before passing the packet to the core.
                               See below (usb_drv_set_address) for details. */
                            DCFG = (DCFG & ~0x7F0) | (ctrlreq.wValue << 4);
                        }
                        usb_core_control_request(&ctrlreq);
                    }
                    else panicf("USB: SETUP done on OUT EP%d!?", i);
                }
                /* Make sure EP0 OUT is set up to accept the next request */
                if (!i)
                {
                    DOEPTSIZ0 = 0x20080040;
                    DOEPDMA0 = &ctrlreq;
                    DOEPCTL0 |= 0x84000000;
                }
                DOEPINT(i) = epints;
            }

    GINTSTS = ints;
}

void usb_drv_set_address(int address)
{
    (void)address;
    /* Ignored intentionally, because the controller requires us to set the
       new address before sending the response for some reason. So we'll
       already set it when the control request arrives, before passing that
       into the USB core, which will then call this dummy function. */
}

static void ep_send(int ep, const void *ptr, int length)
{
    endpoints[ep].busy = true;
    endpoints[ep].size = length;
    DIEPCTL(ep) |= 0x8000;  /* EPx OUT ACTIVE */
    int blocksize = usb_drv_port_speed() ? 512 : 64;
    int packets = (length + blocksize - 1) / blocksize;
    if (!length)
    {
        DIEPTSIZ(ep) = 1 << 19;  /* one empty packet */
        DIEPDMA(ep) = NULL;
    }
    else
    {
        DIEPTSIZ(ep) = length | (packets << 19);
        DIEPDMA(ep) = ptr;
    }
    clean_dcache();
    DIEPCTL(ep) |= 0x84000000;  /* EPx OUT ENABLE CLEARNAK */
}

static void ep_recv(int ep, void *ptr, int length)
{
    endpoints[ep].busy = true;
    endpoints[ep].size = length;
    DOEPCTL(ep) &= ~0x20000;  /* EPx UNSTALL */
    DOEPCTL(ep) |= 0x8000;  /* EPx OUT ACTIVE */
    int blocksize = usb_drv_port_speed() ? 512 : 64;
    int packets = (length + blocksize - 1) / blocksize;
    if (!length)
    {
        DOEPTSIZ(ep) = 1 << 19;  /* one empty packet */
        DOEPDMA(ep) = NULL;
    }
    else
    {
        DOEPTSIZ(ep) = length | (packets << 19);
        DOEPDMA(ep) = ptr;
    }
    clean_dcache();
    DOEPCTL(ep) |= 0x84000000;  /* EPx OUT ENABLE CLEARNAK */
}

int usb_drv_send(int endpoint, void *ptr, int length)
{
    endpoint &= 0x7f;
    endpoints[endpoint].done = false;
    ep_send(endpoint, ptr, length);
    while (!endpoints[endpoint].done && endpoints[endpoint].busy)
        semaphore_wait(&endpoints[endpoint].complete, TIMEOUT_BLOCK);
    return endpoints[endpoint].rc;
}

int usb_drv_send_nonblocking(int endpoint, void *ptr, int length)
{
    ep_send(endpoint & 0x7f, ptr, length);
    return 0;
}

int usb_drv_recv(int endpoint, void* ptr, int length)
{
    ep_recv(endpoint & 0x7f, ptr, length);
    return 0;
}

void usb_drv_cancel_all_transfers(void)
{
    int flags = disable_irq_save();
    reset_endpoints(0);
    restore_irq(flags);
}

void usb_drv_set_test_mode(int mode)
{
    (void)mode;
}

bool usb_drv_stalled(int endpoint, bool in)
{
    if (in) return DIEPCTL(endpoint) & 0x00200000 ? true : false;
    else return DOEPCTL(endpoint) & 0x00200000 ? true : false;
}

void usb_drv_stall(int endpoint, bool stall, bool in)
{
    if (in)
    {
        if (stall) DIEPCTL(endpoint) |= 0x00200000;
        else DIEPCTL(endpoint) &= ~0x00200000;
    }
    else
    {
        if (stall) DOEPCTL(endpoint) |= 0x00200000;
        else DOEPCTL(endpoint) &= ~0x00200000;
    }
}

void usb_drv_init(void)
{
    /* Enable USB clock */
#if CONFIG_CPU==S5L8701
    PWRCON &= ~0x4000;
    PWRCONEXT &= ~0x800;
    INTMSK |= INTMSK_USB_OTG;
#elif CONFIG_CPU==S5L8702
    PWRCON(0) &= ~0x4;
    PWRCON(1) &= ~0x8;
    VIC0INTENABLE |= 1 << 19;
#endif
    PCGCCTL = 0;

    /* reset the beast */
    usb_reset();
}

void usb_drv_exit(void)
{
    DCTL = 0x802;  /* Soft Disconnect */

    OPHYPWR = 0xF;  /* PHY: Power down */
    udelay(10);
    ORSTCON = 7;  /* Put the PHY into reset (needed to get current down) */
    udelay(10);
    PCGCCTL = 1;  /* Shut down PHY clock */
    
#if CONFIG_CPU==S5L8701
    PWRCON |= 0x4000;
    PWRCONEXT |= 0x800;
#elif CONFIG_CPU==S5L8702
    PWRCON(0) |= 0x4;
    PWRCON(1) |= 0x8;
#endif
}

void usb_init_device(void)
{
    unsigned int i;
    for (i = 0; i < sizeof(endpoints)/sizeof(struct ep_type); i++)
        semaphore_init(&endpoints[i].complete, 1, 0);

    /* Power up the core clocks to allow writing
       to some registers needed to power it down */
    PCGCCTL = 0;
#if CONFIG_CPU==S5L8701
    PWRCON &= ~0x4000;
    PWRCONEXT &= ~0x800;
    INTMSK |= INTMSK_USB_OTG;
#elif CONFIG_CPU==S5L8702
    PWRCON(0) &= ~0x4;
    PWRCON(1) &= ~0x8;
    VIC0INTENABLE |= 1 << 19;
#endif

    usb_drv_exit();
}

void usb_enable(bool on)
{
    if (on) usb_core_init();
    else usb_core_exit();
}

void usb_attach(void)
{
    usb_enable(true);
}

int usb_detect(void)
{
    if (charger_inserted())
        return USB_INSERTED;
    return USB_EXTRACTED;
}

#else
void usb_init_device(void)
{
    DCTL = 0x802;  /* Soft Disconnect */

    ORSTCON = 1;  /* Put the PHY into reset (needed to get current down) */
    PCGCCTL = 1;  /* Shut down PHY clock */
    OPHYPWR = 0xF;  /* PHY: Power down */
    
#if CONFIG_CPU==S5L8701
    PWRCON |= 0x4000;
    PWRCONEXT |= 0x800;
#elif CONFIG_CPU==S5L8702
    PWRCON(0) |= 0x4;
    PWRCON(1) |= 0x8;
#endif
}

void usb_enable(bool on)
{
    (void)on;
}

/* Always return false for now */
int usb_detect(void)
{
    return USB_EXTRACTED;
}
#endif
