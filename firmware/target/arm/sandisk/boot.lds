#include "config.h"

/* Can't link all Sansa PP devices the same way at this time */
#ifdef HAVE_BOOTLOADER_USB_MODE
#include "../boot-pp502x-bl-usb.lds"
#else /* !HAVE_BOOTLOADER_USB_MODE */

ENTRY(start)
OUTPUT_FORMAT(elf32-littlearm)
OUTPUT_ARCH(arm)
STARTUP(target/arm/crt0-pp-bl.o)

#define DRAMSIZE (MEMORYSIZE * 0x100000)

#ifdef SANSA_VIEW
#define DRAMORIG 0x10f00000
#else
#define DRAMORIG 0x10000000
#endif

#ifndef IRAMORIG
#define IRAMORIG 0x40000000
#endif
#define IRAMSIZE 0x20000
#define FLASHORIG 0x001f0000
#define FLASHSIZE 2M

MEMORY
{
    DRAM : ORIGIN = DRAMORIG, LENGTH = DRAMSIZE
    IRAM : ORIGIN = IRAMORIG, LENGTH = IRAMSIZE
}

SECTIONS
{
#ifdef SANSA_PP_ERASE
  . = IRAMORIG+0x4000;
#else
  . = IRAMORIG;
#endif

  .text : {
    *(.init.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
  } > IRAM

  .data : {
    *(.icode)
    *(.irodata)
    *(.idata)
    *(.data*)
    *(.ncdata*)
    *(.rodata*)
    _dataend = . ;
  } > IRAM

  .stack (NOLOAD) : {
     *(.stack)
     _stackbegin = .;
     stackbegin = .;
     . += 0x2000;
     _stackend = .;
     stackend = .;
  } > IRAM

  /* The bss section is too large for IRAM - we just move it 16MB into the
     DRAM */

  . = DRAMORIG;
  .bss . + (16*1024*1024) (NOLOAD) : {
     _edata = .;
     *(.bss*);
     *(.ibss);
     *(COMMON)
     *(.ncbss*);
     _end = .;
  } > DRAM
}
#endif /* HAVE_BOOTLOADER_USB_MODE */
