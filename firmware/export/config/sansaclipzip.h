/*
 * This config file is for the Sandisk Sansa Clip Zip
 */
#define TARGET_TREE /* this target is using the target tree system */

/* For Rolo and boot loader */
#define MODEL_NUMBER 79
#define MODEL_NAME   "Sandisk Sansa Zip"
#define FIRMWARE_OFFSET_FILE_DATA 8
#define FIRMWARE_OFFSET_FILE_CRC 0


#ifndef BOOTLOADER
#define HAVE_MULTIDRIVE
#define NUM_DRIVES 2
#define HAVE_HOTSWAP
#endif

#define HW_SAMPR_CAPS       SAMPR_CAP_ALL

/* define this if you have recording possibility */
#define HAVE_RECORDING

#define REC_SAMPR_CAPS      SAMPR_CAP_ALL

/* Define bitmask of input sources - recordable bitmask can be defined
   explicitly if different */
#define INPUT_SRC_CAPS (SRC_CAP_MIC | SRC_CAP_FMRADIO)

/* define this if you have a bitmap LCD display */
#define HAVE_LCD_BITMAP
/* define this if you have a colour LCD */
#define HAVE_LCD_COLOR

/* define this if you want album art for this target */
#define HAVE_ALBUMART

/* define this to enable bitmap scaling */
#define HAVE_BMP_SCALING

/* define this to enable JPEG decoding */
#define HAVE_JPEG

/* define this if you have access to the quickscreen */
#define HAVE_QUICKSCREEN

/* define this if you have access to the pitchscreen */
#define HAVE_PITCHSCREEN

/* define this if you would like tagcache to build on this target */
#define HAVE_TAGCACHE

/* LCD dimensions */
#define LCD_WIDTH  96
#define LCD_HEIGHT 96
#define LCD_DEPTH  16   /* 65536 colours */
#define LCD_PIXELFORMAT RGB565

/* define this if you have LCD enable function */
#define HAVE_LCD_ENABLE

#ifndef BOOTLOADER
/* Define this if your LCD can be put to sleep.
 * HAVE_LCD_ENABLE should be defined as well. */
//#define HAVE_LCD_SLEEP
//#define HAVE_LCD_SLEEP_SETTING
#endif

/* define this if you can flip your LCD */
//#define HAVE_LCD_FLIP

/* define this if you can invert the pixels */
//#define HAVE_LCD_INVERT

/* Define this if your LCD can set contrast */
//#define HAVE_LCD_CONTRAST

//#define MIN_CONTRAST_SETTING        0
//#define MAX_CONTRAST_SETTING        50
//#define DEFAULT_CONTRAST_SETTING    30

//#define IRAM_LCDFRAMEBUFFER IDATA_ATTR /* put the lcd frame buffer in IRAM */

#define CONFIG_KEYPAD SANSA_CLIP_PAD

/* define this if the target has volume keys which can be used in the lists */
#define HAVE_VOLUME_IN_LIST

/* Define this if you do software codec */
#define CONFIG_CODEC SWCODEC
/* There is no hardware tone control */
#define HAVE_SW_TONE_CONTROLS

/* AS3514 or newer */
#define HAVE_AS3514
#define HAVE_AS3543

#ifndef BOOTLOADER
/* define this if you have a real-time clock */
#define CONFIG_RTC RTC_AS3514

/* Define if the device can wake from an RTC alarm */
#define HAVE_RTC_ALARM
#endif

/* Define this if you have a software controlled poweroff */
#define HAVE_SW_POWEROFF

#define HAVE_FAT16SUPPORT

/* The number of bytes reserved for loadable codecs */
#define CODEC_SIZE (0x100000-0x8000)

/* The number of bytes reserved for loadable plugins */
#define PLUGIN_BUFFER_SIZE 0x80000

#define AB_REPEAT_ENABLE

/* FM Tuner */
#define CONFIG_TUNER SI4700 /* in fact SI4702 */
//#define HAVE_TUNER_PWR_CTRL

/* Define this for LCD backlight available */
#define HAVE_BACKLIGHT

#define HAVE_BACKLIGHT_BRIGHTNESS

/* define this if you have a flash memory storage */
#define HAVE_FLASH_STORAGE

/* define this if the flash memory uses the SecureDigital Memory Card protocol */
#define CONFIG_STORAGE STORAGE_SD

#define BATTERY_CAPACITY_DEFAULT 300    /* default battery capacity */
#define BATTERY_CAPACITY_MIN 300        /* min. capacity selectable */
#define BATTERY_CAPACITY_MAX 300        /* max. capacity selectable */
#define BATTERY_CAPACITY_INC 0          /* capacity increment */
#define BATTERY_TYPES_COUNT  1          /* only one type */

/* Charging implemented in a target-specific algorithm */
#define CONFIG_CHARGING CHARGING_TARGET

/* define this if the unit can be powered or charged via USB */
#define HAVE_USB_POWER

/** Non-simulator section **/
#ifndef SIMULATOR

/* Define this if you have a AMS AS3525v2 SoC */
#define CONFIG_CPU AS3525v2

/* Define this if you want to use the AS3525 i2c interface */
#define CONFIG_I2C I2C_AS3525

/* define this if the hardware can be powered off while charging */
/* Sansa can't be powered off while charging */
/* #define HAVE_POWEROFF_WHILE_CHARGING */

/* define current usage levels (based on battery bench) */
#define CURRENT_NORMAL     19               /* TODO copied from clipplus */
#define CURRENT_BACKLIGHT  15               /* TODO copied from clipplus */
#define CURRENT_RECORD     CURRENT_NORMAL   /* TODO */

/* maximum charging current */
#define CURRENT_MAX_CHG   150

/* Define this to the CPU frequency */
#define CPU_FREQ      240000000

/* Type of LCD */
#define CONFIG_LCD LCD_CLIPZIP

/* USB On-the-go */
#define CONFIG_USBOTG USBOTG_AS3525v2

/* enable these for the experimental usb stack */
#define HAVE_USBSTACK
#define USB_HANDLED_BY_OF
//#define USE_ROCKBOX_USB
#define USB_VENDOR_ID 0x0781    /* TODO */
#define USB_PRODUCT_ID 0x74d1   /* TODO */


/* Virtual LED (icon) */
#define CONFIG_LED LED_VIRTUAL

/* Define this if you have adjustable CPU frequency */
//#define HAVE_ADJUSTABLE_CPU_FREQ

#define BOOTFILE_EXT    "sansa"
#define BOOTFILE        "rockbox." BOOTFILE_EXT
#define BOOTDIR "/.rockbox"

#define ICODE_ATTR_TREMOR_NOT_MDCT

#define INCLUDE_TIMEOUT_API

#endif /* SIMULATOR */

/** Port-specific settings **/

/* Main LCD backlight brightness range and defaults */
#define MIN_BRIGHTNESS_SETTING      0
#define MAX_BRIGHTNESS_SETTING      9
#define DEFAULT_BRIGHTNESS_SETTING  6

/* Default recording levels */
#define DEFAULT_REC_MIC_GAIN    23
#define DEFAULT_REC_LEFT_GAIN   23
#define DEFAULT_REC_RIGHT_GAIN  23

/* Define this if a programmable hotkey is mapped */
//#define HAVE_HOTKEY
