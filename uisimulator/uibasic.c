/***************************************************************************
 *             __________               __   ___.                  
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___  
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /  
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <   
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \  
 *                     \/            \/     \/    \/            \/ 
 * $Id$
 *
 * Copyright (C) 2002 by Daniel Stenberg <daniel@haxx.se>
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <errno.h>
#include <ctype.h>
#include <time.h>

#include "screenhack.h"
#include "alpha.h"

#include "version.h"

#define MAX(x,y) ((x)>(y)?(x):(y))
#define MIN(x,y) ((x)<(y)?(x):(y))

#define PROGNAME "rockboxui"

/* -- -- */

static GC draw_gc, erase_gc;
static Colormap cmap;
static XColor color_track, color_car;

static long maxx, maxy;
static double track_zoom=1;

Display *dpy;
Window window;

XrmOptionDescRec options [] = {
  /* { "-subtractive",	".additive",	XrmoptionNoArg, "false" }, */
  { "-server",		".server",	XrmoptionSepArg, 0 },
  { "-help",		".help",	XrmoptionNoArg, "false" },
  { 0, 0, 0, 0 }
};
char *progclass = "rockboxui";

char *defaults [] = {
  ".background:	black",
  ".foreground:	white",
  "*help:       false",
  0
};

#define LOGFILE "xgui.log"
void Logf(char *fmt, ...)
{
  va_list args;
  FILE *log;
  struct tm *t;
  time_t now=time(NULL);

  va_start(args, fmt);

  t = localtime(&now);
  log = fopen(LOGFILE, "a");
  if(log) {
    fprintf(log, "%02d.%02d.%02d ",
            t->tm_hour, t->tm_min, t->tm_sec);
    vfprintf(log, fmt, args);
    fprintf(log, "\n");

    fclose(log);
  }

  fprintf(stderr, "%02d.%02d.%02d ",
          t->tm_hour, t->tm_min, t->tm_sec);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
}

void init_window ()
{
  XGCValues gcv;
  XWindowAttributes xgwa;
  char *test_p;

  XGetWindowAttributes (dpy, window, &xgwa);

  color_track.red=65535;
  color_track.green=65535;
  color_track.blue=65535;

  color_car.red=65535;
  color_car.green=65535;
  color_car.blue=0;

  cmap = xgwa.colormap;

  gcv.function = GXxor;
  gcv.foreground =
    get_pixel_resource ("foreground", "Foreground", dpy, cmap);
  draw_gc = erase_gc = XCreateGC (dpy, window, GCForeground, &gcv);
  XAllocColor (dpy, cmap, &color_track);
  XAllocColor (dpy, cmap, &color_car);

  screen_resized(200, 100);
}

void screen_resized(int width, int height)
{
#if 0
  XWindowAttributes xgwa;
  XGetWindowAttributes (dpy, window, &xgwa);
  maxx = ((long)(xgwa.width));
  maxy = ((long)(xgwa.height));
#else
  maxx = width-1;
  maxy = height-1;
#endif
  XSetForeground (dpy, draw_gc, get_pixel_resource ("background", "Background",
                                                    dpy, cmap));
  XFillRectangle(dpy, window, draw_gc, 0, 0, width, height);

}

static void help(void)
{
  printf(PROGNAME " " ROCKBOXUI_VERSION " " __DATE__ "\n"
         "usage: " PROGNAME "\n"
         );
}

void drawline(int color, int x1, int y1, int x2, int y2)
{
  if (color==0) {
    XSetForeground(dpy, draw_gc,
                   get_pixel_resource("background", "Background", dpy, cmap));
  }
  else
    XSetForeground(dpy, draw_gc,
                   get_pixel_resource("foreground", "Foreground", dpy, cmap));

  XDrawLine(dpy, window, draw_gc, 
            (int)(x1*track_zoom), 
            (int)(y1*track_zoom), 
            (int)(x2*track_zoom), 
            (int)(y2*track_zoom));
}

void drawtext(int color, int x, int y, char *text)
{
  if (color==0) {
    XSetForeground(dpy, draw_gc,
                   get_pixel_resource("background", "Background", dpy, cmap));
  }
  else
    XSetForeground(dpy, draw_gc,
                   get_pixel_resource("foreground", "Foreground", dpy, cmap));

  XDrawString(dpy, window, draw_gc, x, y, text, strlen(text));
}


void
screenhack (Display *the_dpy, Window the_window)
{
  unsigned short porttouse=0;
  char *proxy = NULL;
  char *url = NULL;
  int i;
  char *guiname="Rock-the-box";
  Bool helpme;

  /* This doesn't work, but I don't know why (Daniel 1999-12-01) */
  helpme = get_boolean_resource ("help", "Boolean");
  if(helpme) {
    help();
  }
  printf(PROGNAME " " ROCKBOXUI_VERSION " (" __DATE__ ")\n");

  dpy=the_dpy;
  window=the_window;

  init_window();

  drawtext(1, 20, 20, PROGNAME);
  drawline(1, 0, 0, 40, 50);

  Logf("Rockbox will kill ya!");

  while (1) {
    /* deal with input here */
    
    XSync (dpy, False);
    screenhack_handle_events (dpy);
  }
}

void screen_redraw()
{
  /* does nothing yet */
  drawtext(1, 20, 20, PROGNAME);
  drawline(1, 0, 0, 40, 50);
}
