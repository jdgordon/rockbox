/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2008 by Maurus Cuelenaere
 *
 * based on tcctool.c by Dave Chapman
 *
 * USB code based on ifp-line - http://ifp-driver.sourceforge.net
 *
 * ifp-line is (C) Pavel Kriz, Jun Yamishiro and Joe Roback and
 * licensed under the GPL (v2)
 *
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
#include <inttypes.h>
#include <usb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>
#include "jz4740.h"
#include "jz_xloader.h"

#define VERSION "0.4"

#define MAX_FIRMWARESIZE   (64*1024*1024)   /* Arbitrary limit (for safety) */

/* For win32 compatibility: */
#ifndef O_BINARY
#define O_BINARY 0
#endif

/* USB IDs for USB Boot Mode */
#define VID              0x601A
#define PID              0x4740

#define EP_BULK_TO       0x01
#define TOUT             5000

enum USB_JZ4740_REQUEST
{
    VR_GET_CPU_INFO = 0,
    VR_SET_DATA_ADDRESS,
    VR_SET_DATA_LENGTH,
    VR_FLUSH_CACHES,
    VR_PROGRAM_START1,
    VR_PROGRAM_START2,
    VR_NOR_OPS,
    VR_NAND_OPS,
    VR_SDRAM_OPS,
    VR_CONFIGURATION
};

enum NOR_OPS_TYPE
{
    NOR_INIT = 0,
    NOR_QUERY,
    NOR_WRITE,
    NOR_ERASE_CHIP,
    NOR_ERASE_SECTOR
};

enum NOR_FLASH_TYPE
{
    NOR_AM29 = 0,
    NOR_SST28,
    NOR_SST39x16,
    NOR_SST39x8
};

enum NAND_OPS_TYPE
{
    NAND_QUERY = 0,
    NAND_INIT,
    NAND_MARK_BAD,
    NAND_READ_OOB,
    NAND_READ_RAW,
    NAND_ERASE,
    NAND_READ,
    NAND_PROGRAM,
    NAND_READ_TO_RAM
};

enum SDRAM_OPS_TYPE
{
    SDRAM_LOAD,
};

enum DATA_STRUCTURE_OB
{
    DS_flash_info,
    DS_hand
};

enum OPTION
{
    OOB_ECC,
    OOB_NO_ECC,
    NO_OOB,
};

int filesize(FILE* fd)
{
    int ret;
    
    fseek(fd, 0, SEEK_END);
    ret = ftell(fd);
    fseek(fd, 0, SEEK_SET);
    
    return ret;
}

bool file_exists(const char* filename)
{
    FILE* fp = fopen(filename, "r");
    
    if(fp)
    {
        fclose(fp);
        return true;
    }
    else
        return false;
}


#define SEND_COMMAND(cmd, arg) err = usb_control_msg(dh, USB_ENDPOINT_OUT | USB_TYPE_VENDOR, (cmd), (arg)>>16, (arg)&0xFFFF, NULL, 0, TOUT);\
                               if (err < 0) \
                               { \
                                   fprintf(stderr,"\n[ERR]  Error sending control message (%d, %s)\n", err, usb_strerror()); \
                                   return -1; \
                               }

#define GET_CPU_INFO(s)        err = usb_control_msg(dh, USB_ENDPOINT_IN | USB_TYPE_VENDOR, VR_GET_CPU_INFO, 0, 0, (s), 8, TOUT); \
                               if (err < 0) \
                               { \
                                   fprintf(stderr,"\n[ERR]  Error sending control message (%d, %s)\n", err, usb_strerror()); \
                                   return -1; \
                               }

#define SEND_DATA(ptr, size)   err = usb_bulk_write(dh, USB_ENDPOINT_OUT | EP_BULK_TO, ((char*)(ptr)), (size), TOUT); \
                               if (err != (size))  \
                               { \
                                   fprintf(stderr,"\n[ERR]  Error writing data\n"); \
                                   fprintf(stderr,"[ERR]  Bulk write error (%d, %s)\n", err, strerror(-err)); \
                                   return -1; \
                               }

#define GET_DATA(ptr, size)    err = usb_bulk_read(dh, USB_ENDPOINT_IN | EP_BULK_TO, ((char*)(ptr)), (size), TOUT); \
                               if (err != (size))  \
                               { \
                                   fprintf(stderr,"\n[ERR]  Error writing data\n"); \
                                   fprintf(stderr,"[ERR]  Bulk write error (%d, %s)\n", err, strerror(-err)); \
                                   return -1; \
                               }
int upload_data(usb_dev_handle* dh, int address, unsigned char* p, int len)
{
    int err;
    char buf[9];
    unsigned char* tmp_buf;

    fprintf(stderr, "[INFO] GET_CPU_INFO: ");
    GET_CPU_INFO(buf);
    buf[8] = 0;
    fprintf(stderr, "%s\n", buf);

    fprintf(stderr, "[INFO] SET_DATA_ADDRESS to 0x%x...", address);
    SEND_COMMAND(VR_SET_DATA_ADDRESS, address);
    fprintf(stderr, " Done!\n");

    fprintf(stderr, "[INFO] Sending data...");
    /* Must not split the file in several packages! */
    SEND_DATA(p, len);
    fprintf(stderr, " Done!\n");
    
    fprintf(stderr, "[INFO] Verifying data...");
    SEND_COMMAND(VR_SET_DATA_ADDRESS, address);
    SEND_COMMAND(VR_SET_DATA_LENGTH, len);
    tmp_buf = malloc(len);
    if (tmp_buf == NULL)
    {
        fprintf(stderr, "\n[ERR]  Could not allocate memory.\n");
        return -1;
    }
    GET_DATA(tmp_buf, len);
    if (memcmp(tmp_buf, p, len) != 0)
        fprintf(stderr, "\n[WARN] Sent data isn't the same as received data...\n");
    else
        fprintf(stderr, " Done!\n");
    free(tmp_buf);
    
    return 0;
}

int boot(usb_dev_handle* dh, int address, bool stage2)
{
    int err;
    
    fprintf(stderr, "[INFO] Booting device STAGE%d...", (stage2 ? 2 : 1));
    SEND_COMMAND((stage2 ? VR_PROGRAM_START2 : VR_PROGRAM_START1), address );
    fprintf(stderr, " Done!\n");
    
    return err;
}

int upload_app(usb_dev_handle* dh, int address, unsigned char* p, int len, bool stage2)
{
    int err = upload_data(dh, address, p, len);
    if(err == 0)
    {
        err = boot(dh, address, stage2);
        if(err == 0)
            fprintf(stderr, "[INFO] Done!\n");
    }
    
    return err;
}

int read_data(usb_dev_handle* dh, int address, unsigned char *p, int len)
{
    int err;
    char buf[9];

    fprintf(stderr, "[INFO] GET_CPU_INFO: ");
    GET_CPU_INFO(buf);
    buf[8] = 0;
    fprintf(stderr, "%s\n", buf);

    fprintf(stderr, "[INFO] Reading data...");
    SEND_COMMAND(VR_SET_DATA_ADDRESS, address);
    SEND_COMMAND(VR_SET_DATA_LENGTH, len);
    GET_DATA(p, len);
    fprintf(stderr, " Done!\n");
    return 0;
}

unsigned int read_reg(usb_dev_handle* dh, int address, int size)
{
    int err;
    unsigned char buf[4];
    
    SEND_COMMAND(VR_SET_DATA_ADDRESS, address);
    SEND_COMMAND(VR_SET_DATA_LENGTH, size);
    GET_DATA(buf, size);

    if(size == 1)
        return buf[0];
    else if(size == 2)
        return (buf[1] << 8) | buf[0];
    else if(size == 4)
        return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
    else
        return 0;
}

int set_reg(usb_dev_handle* dh, int address, unsigned int val, int size)
{
    int err, i;
    unsigned char buf[4];
    
    buf[0] = val & 0xff;
    if(i > 1)
    {
        buf[1] = (val >> 8) & 0xff;
        if(i > 2)
        {
            buf[2] = (val >> 16) & 0xff;
            buf[3] = (val >> 24) & 0xff;
        }
    }
    
    SEND_COMMAND(VR_SET_DATA_ADDRESS, address);
    SEND_DATA(buf, size);

    return 0;
}
#define or_reg(dh, adr, val, size) set_reg(dh, adr, (read_reg(dh, adr, size) | (val)), size);
#define and_reg(dh, adr, val, size) set_reg(dh, adr, (read_reg(dh, adr, size) & (val)), size);
#define bc_reg(dh, adr, val, size) set_reg(dh, adr, (read_reg(dh, adr, size) & ~(val)), size);
#define xor_reg(dh, adr, val, size) set_reg(dh, adr, (read_reg(dh, adr, size) ^ (val)), size);

#define TEST(m, size) fprintf(stderr, "%s -> %x\n", #m, read_reg(dh, m, size));
int test_device(usb_dev_handle* dh)
{
    TEST(INTC_ISR, 4);
    TEST(INTC_IMR, 4);
    TEST(INTC_IMSR, 4);
    TEST(INTC_IMCR, 4);
    TEST(INTC_IPR, 4);
    
    fprintf(stderr, "\n");
    TEST(RTC_RCR, 4);
    TEST(RTC_RSR, 4);
    TEST(RTC_RSAR, 4);
    TEST(RTC_RGR, 4);
    TEST(RTC_HCR, 4);
    TEST(RTC_RCR, 4);
    TEST(RTC_HWFCR, 4);
    TEST(RTC_HRCR, 4);
    TEST(RTC_HWCR, 4);
    TEST(RTC_HWSR, 4);
    
    fprintf(stderr, "\n");
    TEST(GPIO_PXPIN(0), 4);
    TEST(GPIO_PXPIN(1), 4);
    TEST(GPIO_PXPIN(2), 4);
    TEST(GPIO_PXPIN(3), 4);
    
    fprintf(stderr, "\n");
    TEST(CPM_CLKGR, 4);
    
    fprintf(stderr, "\n");
    TEST(SADC_ENA, 1);
    TEST(SADC_CTRL, 1);
    TEST(SADC_TSDAT, 4);
    TEST(SADC_BATDAT, 2);
    TEST(SADC_STATE, 1);
    
    fprintf(stderr, "\n");
    
    TEST(SLCD_CFG, 4);
    TEST(SLCD_CTRL, 1);
    TEST(SLCD_STATE, 1);
    
    return 0;
}

unsigned int read_file(const char *name, unsigned char **buffer)
{
    FILE *fd;
    int len, n;
    
    fd = fopen(name, "rb");
    if (fd == NULL)
    {
        fprintf(stderr, "[ERR]  Could not open %s\n", name);
        return 0;
    }
    
    len = filesize(fd);
    
    *buffer = (unsigned char*)malloc(len);
    if (*buffer == NULL)
    {
        fprintf(stderr, "[ERR]  Could not allocate memory.\n");
        fclose(fd);
        return 0;
    }
    
    n = fread(*buffer, 1, len, fd);
    if (n != len)
    {
        fprintf(stderr, "[ERR]  Short read.\n");
        fclose(fd);
        return 0;
    }
    fclose(fd);
    
    return len;
}
#define _GET_CPU fprintf(stderr, "[INFO] GET_CPU_INFO:"); \
                 GET_CPU_INFO(cpu); \
                 cpu[8] = 0; \
                 fprintf(stderr, " %s\n", cpu);
#define _SET_ADDR(a) fprintf(stderr, "[INFO] Set address to 0x%x...", a); \
                     SEND_COMMAND(VR_SET_DATA_ADDRESS, a); \
                     fprintf(stderr, " Done!\n");
#define _SEND_FILE(a) fsize = read_file(a, &buffer); \
                      if(fsize == 0) \
                        return -1; \
                      fprintf(stderr, "[INFO] Sending file %s: %d bytes...", a, fsize); \
                      SEND_DATA(buffer, fsize); \
                      free(buffer); \
                      fprintf(stderr, " Done!\n");
#define _VERIFY_DATA(a,c) fprintf(stderr, "[INFO] Verifying data (%s)...", a); \
                          fsize = read_file(a, &buffer); \
                          if(fsize == 0) \
                            return -1; \
                          buffer2 = (unsigned char*)malloc(fsize); \
                          SEND_COMMAND(VR_SET_DATA_ADDRESS, c); \
                          SEND_COMMAND(VR_SET_DATA_LENGTH,  fsize); \
                          GET_DATA(buffer2, fsize); \
                          if(memcmp(buffer, buffer2, fsize) != 0) \
                              fprintf(stderr, "\n[WARN] Sent data isn't the same as received data...\n"); \
                          else \
                              fprintf(stderr, " Done!\n"); \
                          free(buffer); \
                          free(buffer2);
#define _STAGE1(a)     fprintf(stderr, "[INFO] Stage 1 at 0x%x\n", a); \
                       SEND_COMMAND(VR_PROGRAM_START1, a);
#define _STAGE2(a)     fprintf(stderr, "[INFO] Stage 2 at 0x%x\n", a); \
                       SEND_COMMAND(VR_PROGRAM_START2, a);
#define _FLUSH         fprintf(stderr, "[INFO] Flushing caches...\n"); \
                       SEND_COMMAND(VR_FLUSH_CACHES, 0);
#ifdef _WIN32
 #define _SLEEP(x) Sleep(x*1000);
#else
 #define _SLEEP(x) sleep(x);
#endif
int mimic_of(usb_dev_handle *dh, bool vx767)
{
    int err, fsize;
    unsigned char *buffer, *buffer2;
    char cpu[9];

    fprintf(stderr, "[INFO] Start!\n");
    _GET_CPU;
    _SET_ADDR(0x8000 << 16);
    _SEND_FILE("1.bin");
    _GET_CPU;
    _VERIFY_DATA("1.bin", 0x8000 << 16);
    _STAGE1(0x8000 << 16);
    _SLEEP(3);
    _VERIFY_DATA("2.bin", 0xB3020060);
    _GET_CPU;
    _GET_CPU;
    _FLUSH;
    _GET_CPU;
    _GET_CPU;
    _SET_ADDR(0x8000 << 16);
    _SEND_FILE("3.bin");
    _GET_CPU;
    _VERIFY_DATA("3.bin", 0x8000 << 16);
    _GET_CPU;
    _FLUSH;
    _GET_CPU;
    _GET_CPU;
    _SET_ADDR(0x80D0 << 16);
    _SEND_FILE("4.bin");
    _GET_CPU;
    _VERIFY_DATA("4.bin", 0x80D0 << 16);
    _GET_CPU;
    _FLUSH;
    _GET_CPU;
    _GET_CPU;
    _SET_ADDR(0x80E0 << 16);
    _SEND_FILE("5.bin");
    _GET_CPU;
    _VERIFY_DATA("5.bin", 0x80E0 << 16);
    _GET_CPU;
    _FLUSH;
    _GET_CPU;
    _GET_CPU;
    _SET_ADDR(0x80004000);
    _SEND_FILE("6.bin");
    _GET_CPU;
    _VERIFY_DATA("6.bin", 0x80004000);
    _GET_CPU;
    _FLUSH;
    _GET_CPU;
    _GET_CPU;
    _SET_ADDR(0x80FD << 16);
    _SEND_FILE("7.bin");
    _GET_CPU;
    _VERIFY_DATA("7.bin", 0x80FD << 16);
    _GET_CPU;
    _FLUSH;
    _GET_CPU;
    _STAGE2(0x80FD0004);
    _VERIFY_DATA("8.bin", 0x80004004);
    _VERIFY_DATA("9.bin", 0x80004008);
    _SLEEP(2);
    _GET_CPU;
    _SET_ADDR(0x80E0 << 16);
    _SEND_FILE("10.bin");
    _GET_CPU;
    _VERIFY_DATA("10.bin", 0x80E0 << 16);
    _GET_CPU;
    _FLUSH;
    _GET_CPU;
    if(vx767)
    {
        _STAGE2(0x80E10008);
    }
    else
    {
        _STAGE2(0x80E00008);
    }
    fprintf(stderr, "[INFO] Done!\n");
    return 0;
}

int send_rockbox(usb_dev_handle *dh, const char* filename)
{
    int fsize;
    unsigned char *buffer;
    
    fprintf(stderr, "[INFO] Start!\n");
    if(file_exists("jz_xloader.bin"))
    {
        fprintf(stderr, "[INFO] Using jz_xloader.bin\n");
        fsize = read_file("jz_xloader.bin", &buffer);
        upload_data(dh, 0x080000000, buffer, fsize);
        free(buffer);
    }
    else
    {
        fprintf(stderr, "[INFO] Using built-in jz_xloader.bin\n");
        upload_data(dh, 0x080000000, jz_xloader, LEN_jz_xloader);
    }
    boot(dh, 0x080000000, false);
    _SLEEP(1);
    
    fsize = read_file(filename, &buffer);
    upload_data(dh, 0x080004000, buffer, fsize);
    free(buffer);
    boot(dh, 0x080004000, true);
    
    fprintf(stderr, "[INFO] Done!\n");
    
    return 0;
}

#define SEND_NAND_COMMAND(cs, cmd, option) SEND_COMMAND(VR_NAND_OPS, ((cmd&0xF)|((cs&0xFF)<<4)|((option&0xFF)<<12)) );
#define LENGTH 1024*1024*5
int nand_dump(usb_dev_handle *dh)
{
    int err;
    unsigned int n;
    FILE *fd;
    unsigned char* buffer;
    
    fd = fopen("nand_dump.bin", "wb");
    if (fd == NULL)
    {
        fprintf(stderr, "[ERR]  Could not open nand_dump.bin\n");
        return 0;
    }
    
    buffer = (unsigned char*)malloc(LENGTH);
    if (buffer == NULL)
    {
        fprintf(stderr, "[ERR]  Could not allocate memory.\n");
        fclose(fd);
        return 0;
    }
    
    SEND_NAND_COMMAND(0, NAND_INIT, 0);
    /*
    fprintf(stderr, "[INFO] Querying NAND...\n");
    SEND_NAND_COMMAND(0, NAND_QUERY, 0);
    GET_DATA(buffer, 4);
    printf("[INFO] %x %x %x %x\n", buffer[0], buffer[1], buffer[2], buffer[3]);
    */
    SEND_COMMAND(VR_SET_DATA_ADDRESS, 0);
    SEND_COMMAND(VR_SET_DATA_LENGTH, LENGTH);
    SEND_NAND_COMMAND(0, NAND_READ, NO_OOB);
    
    fprintf(stderr, "[INFO] Reading data...\n");
    err = usb_bulk_read(dh, USB_ENDPOINT_IN | EP_BULK_TO, (char*)buffer, LENGTH, TOUT);
    if (err != LENGTH) 
    {
        fprintf(stderr,"\n[ERR]  Error writing data\n");
        fprintf(stderr,"[ERR]  Bulk write error (%d, %s)\n", err, strerror(-err));
        fclose(fd);
        free(buffer);
        return -1;
    }
    
    n = fwrite(buffer, 1, LENGTH, fd);
    if (n != LENGTH)
    {
        fprintf(stderr, "[ERR]  Short write.\n");
        fclose(fd);
        free(buffer);
        return 0;
    }
    fclose(fd);
    free(buffer);
    
    return n;
}

#define ROM_LENGTH 0x1000*16
int rom_dump(usb_dev_handle *dh)
{
    int err;
    unsigned int n;
    FILE *fd;
    unsigned char* buffer;
    
    fd = fopen("rom_dump.bin", "wb");
    if (fd == NULL)
    {
        fprintf(stderr, "[ERR]  Could not open rom_dump.bin\n");
        return 0;
    }
    
    buffer = (unsigned char*)malloc(ROM_LENGTH);
    if (buffer == NULL)
    {
        fprintf(stderr, "[ERR]  Could not allocate memory.\n");
        fclose(fd);
        return 0;
    }
    
    SEND_COMMAND(VR_SET_DATA_ADDRESS, 0x1FC00000);
    SEND_COMMAND(VR_SET_DATA_LENGTH, ROM_LENGTH);
    
    fprintf(stderr, "[INFO] Reading data...\n");
    err = usb_bulk_read(dh, USB_ENDPOINT_IN | EP_BULK_TO, (char*)buffer, ROM_LENGTH, TOUT);
    if (err != ROM_LENGTH) 
    {
        fprintf(stderr,"\n[ERR]  Error writing data\n");
        fprintf(stderr,"[ERR]  Bulk write error (%d, %s)\n", err, strerror(-err));
        fclose(fd);
        free(buffer);
        return -1;
    }
    
    n = fwrite(buffer, 1, ROM_LENGTH, fd);
    if (n != ROM_LENGTH)
    {
        fprintf(stderr, "[ERR]  Short write.\n");
        fclose(fd);
        free(buffer);
        return 0;
    }
    fclose(fd);
    free(buffer);
    
    return n;
}

int jzconnect(int address, unsigned char* buf, int len, int func)
{
    struct usb_bus *bus;
    struct usb_device *tmp_dev;
    struct usb_device *dev = NULL;
    usb_dev_handle *dh;
    int err;

    fprintf(stderr,"[INFO] Searching for device...\n");
 
    usb_init();
    if(usb_find_busses() < 0)
    {
        fprintf(stderr, "[ERR]  Could not find any USB busses.\n");
        return -2;
    }

    if (usb_find_devices() < 0)
    {
        fprintf(stderr, "[ERR]  USB devices not found(nor hubs!).\n");
        return -3;
    }

    for (bus = usb_get_busses(); bus; bus = bus->next)
    {
        for (tmp_dev = bus->devices; tmp_dev; tmp_dev = tmp_dev->next)
        {
            if (tmp_dev->descriptor.idVendor == VID &&
                tmp_dev->descriptor.idProduct == PID)
            {
                dev = tmp_dev;
                goto found;
            }
        }
    }

    fprintf(stderr, "[ERR]  Device not found.\n");
    fprintf(stderr, "[ERR]  Ensure your device is in USB boot mode and run usbtool again.\n");
    return -4;

found:
    if ( (dh = usb_open(dev)) == NULL)
    {
        fprintf(stderr,"[ERR]  Unable to open device.\n");
        return -5;
    }
    
/* usb_set_configuration() calls are already done in Linux */
#ifdef _WIN32
    err = usb_set_configuration(dh, 1);

    if (err < 0)
    {
        fprintf(stderr, "[ERR]  usb_set_configuration failed (%d, %s)\n", err, usb_strerror());
        usb_close(dh);
        return -6;
    }
#endif
    
    /* "must be called" written in the libusb documentation */
    err = usb_claim_interface(dh, 0);
    if (err < 0)
    {
        fprintf(stderr, "[ERR]  Unable to claim interface (%d, %s)\n", err, usb_strerror());
        usb_close(dh);
        return -7;
    }

    fprintf(stderr,"[INFO] Found device, uploading application.\n");

    /* Now we can transfer the application to the device. */ 

    switch(func)
    {
        case 1:
        case 5:
            err = upload_app(dh, address, buf, len, (func == 5));
        break;
        case 2:
            err = read_data(dh, address, buf, len);
        break;
        case 3:
            err = test_device(dh);
        break;
        case 6:
        case 7:
            err = mimic_of(dh, (func == 7));
        break;
        case 8:
            err = nand_dump(dh);
        break;
        case 9:
            err = rom_dump(dh);
        break;
        case 10:
            err = send_rockbox(dh, (char*)buf);
        break;
    }
    
    /* release claimed interface */
    usb_release_interface(dh, 0);

    usb_close(dh);
    
    return err;
}

void print_usage(void)
{
#ifdef _WIN32
    fprintf(stderr, "Usage: usbtool.exe <CMD> [FILE] [ADDRESS] [LEN]\n");
#else
    fprintf(stderr, "Usage: usbtool <CMD> [FILE] [ADDRESS] [LEN]\n");
#endif

    fprintf(stderr, "\t[ADDRESS] has to be in 0xHEXADECIMAL format\n");
    fprintf(stderr, "\tCMD:\n");
    fprintf(stderr, "\t\t 1 -> upload file to specified address and boot from it\n");
    fprintf(stderr, "\t\t 2 -> read data from [ADDRESS] with length [LEN] to [FILE]\n");
    fprintf(stderr, "\t\t 3 -> read device status\n");
    fprintf(stderr, "\t\t 5 -> same as 1 but do a stage 2 boot\n");
    fprintf(stderr, "\t\t 6 -> mimic VX747 OF fw recovery\n");
    fprintf(stderr, "\t\t 7 -> mimic VX767 OF fw recovery\n");
    fprintf(stderr, "\t\t 8 -> do a NAND dump\n");
    fprintf(stderr, "\t\t 9 -> do a ROM dump\n");
    fprintf(stderr, "\t\t10 -> send Rockbox bootloader at [FILE] to SDRAM\n");

#ifdef _WIN32
    fprintf(stderr, "\nExample:\n\t usbtool.exe 1 fw.bin 0x80000000\n");
    fprintf(stderr, "\t usbtool.exe 2 save.bin 0x81000000 1024\n");
#else
    fprintf(stderr, "\nExample:\n\t usbtool 1 fw.bin 0x80000000\n");
    fprintf(stderr, "\t usbtool 2 save.bin 0x81000000 1024\n");
#endif
}

int main(int argc, char* argv[])
{
    unsigned char* buf;
    int n, len, address, cmd=0;
    FILE* fd;

    fprintf(stderr, "USBtool v" VERSION " - (C) 2008 Maurus Cuelenaere\n");
    fprintf(stderr, "This is free software; see the source for copying conditions.  There is NO\n");
    fprintf(stderr, "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");
    
    if(argc > 1)
        sscanf(argv[1], "%d", &cmd);
    switch(cmd)
    {
        case 5:
        case 1:
            if (strcmp(argv[3], "-1") == 0)
                address = 0x80000000;
            else
            {
                if (sscanf(argv[3], "0x%x", &address) <= 0)
                {
                    print_usage();
                    return -1;
                }
            }
            
            fd = fopen(argv[2], "rb");
            if (fd < 0)
            {
                fprintf(stderr, "[ERR]  Could not open %s\n", argv[2]);
                return 4;
            }
            
            len = filesize(fd);

            if (len > MAX_FIRMWARESIZE)
            {
                fprintf(stderr, "[ERR]  Firmware file too big\n");
                fclose(fd);
                return 5;
            }
            
            buf = malloc(len);
            if (buf == NULL)
            {
                fprintf(stderr, "[ERR]  Could not allocate memory.\n");
                fclose(fd);
                return 6;
            }
            
            n = fread(buf, 1, len, fd);
            if (n != len)
            {
                fprintf(stderr, "[ERR]  Short read.\n");
                fclose(fd);
                return 7;
            }
            fclose(fd);
            
            fprintf(stderr, "[INFO] File size: %d bytes\n", n);
            
            return jzconnect(address, buf, len, cmd);
        case 2:
            if (sscanf(argv[3], "0x%x", &address) <= 0)
            {
                print_usage();
                return -1;
            }
            
            fd = fopen(argv[2], "wb");
            if (fd < 0)
            {
                fprintf(stderr, "[ERR]  Could not open %s\n", argv[2]);
                return 4;
            }
            
            sscanf(argv[4], "%d", &len);
            
            buf = malloc(len);
            if (buf == NULL)
            {
                fprintf(stderr, "[ERR]  Could not allocate memory.\n");
                fclose(fd);
                return 6;
            }
            
            int err = jzconnect(address, buf, len, 2);
            
            n = fwrite(buf, 1, len, fd);
            if (n != len)
            {
                fprintf(stderr, "[ERR]  Short write.\n");
                fclose(fd);
                return 7;
            }
            fclose(fd);
            
            return err;
        case 10:
            if(argc < 3)
            {
                print_usage();
                return 1;
            }
            
            if(!file_exists(argv[2]))
            {
                print_usage();
                return 1;
            }
            return jzconnect(address, (unsigned char*)argv[2], 0, 10);
        case 3:
        case 6:
        case 7:
        case 8:
        case 9:
            return jzconnect(address, NULL, 0, cmd);
        default:
            print_usage();
            return 1;
    }

    return 0;
}
