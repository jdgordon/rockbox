/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2007 Nicolas Pennequin, Dan Everton, Matthias Mohr
 *               2010 Jonathan Gordon
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
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "core_alloc.h"
#include "file.h"
#include "misc.h"
#include "plugin.h"
#include "viewport.h"

#include "skin_buffer.h"
#include "skin_parser.h"
#include "tag_table.h"

#ifdef __PCTOOL__
#ifdef WPSEDITOR
#include "proxy.h"
#include "sysfont.h"
#else
#include "action.h"
#include "checkwps.h"
#include "audio.h"
#define lang_is_rtl() (false)
#define DEBUGF printf
#endif /*WPSEDITOR*/
#else
#include "debug.h"
#include "language.h"
#endif /*__PCTOOL__*/

#include <ctype.h>
#include <stdbool.h>
#include "font.h"

#include "wps_internals.h"
#include "skin_engine.h"
#include "settings.h"
#include "settings_list.h"
#if CONFIG_TUNER
#include "radio.h"
#include "tuner.h"
#endif

#ifdef HAVE_LCD_BITMAP
#include "bmp.h"
#endif

#ifdef HAVE_ALBUMART
#include "playback.h"
#endif

#include "backdrop.h"
#include "statusbar-skinned.h"

#define WPS_ERROR_INVALID_PARAM         -1

#define GLYPHS_TO_CACHE 256

static bool isdefault(struct skin_tag_parameter *param)
{
    return param->type == DEFAULT;
}


/* which screen are we parsing for? */
static enum screen_type curr_screen;

/* the current viewport */
static struct skin_element *curr_viewport_element;
static struct skin_viewport *curr_vp;

static struct line *curr_line;

static int follow_lang_direction = 0;

typedef int (*parse_function)(struct skin_element *element,
                              struct wps_token *token,
                              struct wps_data *wps_data);

#ifdef HAVE_LCD_BITMAP
/* add a skin_token_list item to the list chain. ALWAYS appended because some of the
 * chains require the order to be kept.
 */
static void add_to_ll_chain(struct skin_token_list **list, struct skin_token_list *item)
{
    if (*list == NULL)
        *list = item;
    else
    {
        struct skin_token_list *t = *list;
        while (t->next)
            t = t->next;
        t->next = item;
    }
}

#endif


void *skin_find_item(const char *label, enum skin_find_what what,
                     struct wps_data *data)
{
    const char *itemlabel = NULL;
    union {
        struct skin_token_list *linkedlist;
        struct skin_element *vplist;
    } list = {NULL};
    bool isvplist = false;
    void *ret = NULL;
    switch (what)
    {
        case SKIN_FIND_UIVP:
        case SKIN_FIND_VP:
            list.vplist = data->tree;
            isvplist = true;
        break;
#ifdef HAVE_LCD_BITMAP
        case SKIN_FIND_IMAGE:
            list.linkedlist = data->images;
        break;
#endif
#ifdef HAVE_TOUCHSCREEN
        case SKIN_FIND_TOUCHREGION:
            list.linkedlist = data->touchregions;
        break;
#endif
#ifdef HAVE_SKIN_VARIABLES
        case SKIN_VARIABLE:
            list.linkedlist = data->skinvars;
        break;
#endif
    }
        
    while (list.linkedlist)
    {
        bool skip = false;
        switch (what)
        {
            case SKIN_FIND_UIVP:
            case SKIN_FIND_VP:
                ret = list.vplist->data;
                itemlabel = ((struct skin_viewport *)ret)->label;
                skip = !(((struct skin_viewport *)ret)->is_infovp == 
                    (what==SKIN_FIND_UIVP));
                break;
#ifdef HAVE_LCD_BITMAP
            case SKIN_FIND_IMAGE:
                ret = list.linkedlist->token->value.data;
                itemlabel = ((struct gui_img *)ret)->label;
                break;
#endif
#ifdef HAVE_TOUCHSCREEN
            case SKIN_FIND_TOUCHREGION:
                ret = list.linkedlist->token->value.data;
                itemlabel = ((struct touchregion *)ret)->label;
                break;
#endif
#ifdef HAVE_SKIN_VARIABLES
            case SKIN_VARIABLE:
                ret = list.linkedlist->token->value.data;
                itemlabel = ((struct skin_var *)ret)->label;
                break;
#endif
                
        }
        if (!skip && itemlabel && !strcmp(itemlabel, label))
            return ret;
        
        if (isvplist)
            list.vplist = list.vplist->next;
        else
            list.linkedlist = list.linkedlist->next;
    }
    return NULL;
}

#ifdef HAVE_LCD_BITMAP

/* create and init a new wpsll item.
 * passing NULL to token will alloc a new one.
 * You should only pass NULL for the token when the token type (table above)
 * is WPS_NO_TOKEN which means it is not stored automatically in the skins token array
 */
static struct skin_token_list *new_skin_token_list_item(struct wps_token *token,
                                                        void* token_data)
{
    struct skin_token_list *llitem = 
        (struct skin_token_list *)skin_buffer_alloc(sizeof(struct skin_token_list));
    if (!token)
        token = (struct wps_token*)skin_buffer_alloc(sizeof(struct wps_token));
    if (!llitem || !token)
        return NULL;
    llitem->next = NULL;
    llitem->token = token;
    if (token_data)
        llitem->token->value.data = token_data;
    return llitem;
}

static int parse_statusbar_tags(struct skin_element* element,
                                struct wps_token *token,
                                struct wps_data *wps_data)
{
    (void)element;
    if (token->type == SKIN_TOKEN_DRAW_INBUILTBAR)
    {
        token->value.data = (void*)&curr_vp->vp;
    }
    else
    {
        struct skin_element *def_vp = wps_data->tree;
        struct skin_viewport *default_vp = def_vp->data;
        if (def_vp->params_count == 0)
        {
            wps_data->wps_sb_tag = true;
            wps_data->show_sb_on_wps = (token->type == SKIN_TOKEN_ENABLE_THEME);
        }
        if (wps_data->show_sb_on_wps)
        {
            viewport_set_defaults(&default_vp->vp, curr_screen);
        }
        else
        {
            viewport_set_fullscreen(&default_vp->vp, curr_screen);
        }
#ifdef HAVE_REMOTE_LCD
        /* viewport_set_defaults() sets the font to FONT_UI+curr_screen.
         * This parser requires font 1 to always be the UI font, 
         * so force it back to FONT_UI and handle the screen number at the end */
        default_vp->vp.font = FONT_UI;
#endif
    }
    return 0;
}
            
static int get_image_id(int c)
{
    if(c >= 'a' && c <= 'z')
        return c - 'a';
    else if(c >= 'A' && c <= 'Z')
        return c - 'A' + 26;
    else
        return -1;
}

char *get_image_filename(const char *start, const char* bmpdir,
                                char *buf, int buf_size)
{
    snprintf(buf, buf_size, "%s/%s", bmpdir, start);
    
    return buf;
}

static int parse_image_display(struct skin_element *element,
                               struct wps_token *token,
                               struct wps_data *wps_data)
{
    char *label = element->params[0].data.text;
    char sublabel = '\0';
    int subimage;
    struct gui_img *img;
    struct image_display *id = skin_buffer_alloc(sizeof(struct image_display));

    if (element->params_count == 1 && strlen(label) <= 2)
    {
        /* backwards compatability. Allow %xd(Aa) to still work */
        sublabel = label[1];
        label[1] = '\0';
    }
    /* sanity check */
    img = skin_find_item(label, SKIN_FIND_IMAGE, wps_data);
    if (!img || !id)
    {
        return WPS_ERROR_INVALID_PARAM;
    }
    id->label = label;
    id->offset = 0;
    id->token = NULL;
    if (img->using_preloaded_icons)
    {
        token->type = SKIN_TOKEN_IMAGE_DISPLAY_LISTICON;
    }
    
    if (element->params_count > 1)
    {
        if (element->params[1].type == CODE)
            id->token = element->params[1].data.code->data;
        /* specify a number. 1 being the first subimage (i.e top) NOT 0 */
        else if (element->params[1].type == INTEGER)
            id->subimage = element->params[1].data.number - 1;
        if (element->params_count > 2)
            id->offset = element->params[2].data.number;
    }
    else
    {
        if ((subimage = get_image_id(sublabel)) != -1)
        {
            if (subimage >= img->num_subimages)
                return WPS_ERROR_INVALID_PARAM;
            id->subimage = subimage;
        } else {
            id->subimage = 0;
        }
    }
    token->value.data = id;
    return 0;
}

static int parse_image_load(struct skin_element *element,
                            struct wps_token *token,
                            struct wps_data *wps_data)
{
    const char* filename;
    const char* id;
    int x,y;
    struct gui_img *img;

    /* format: %x(n,filename.bmp,x,y)
       or %xl(n,filename.bmp,x,y)
       or %xl(n,filename.bmp,x,y,num_subimages)
    */

    id = element->params[0].data.text;
    filename = element->params[1].data.text;
    x = element->params[2].data.number;
    y = element->params[3].data.number;

    /* check the image number and load state */
    if(skin_find_item(id, SKIN_FIND_IMAGE, wps_data))
    {
        /* Invalid image ID */
        return WPS_ERROR_INVALID_PARAM;
    }
    img = (struct gui_img*)skin_buffer_alloc(sizeof(struct gui_img));
    if (!img)
        return WPS_ERROR_INVALID_PARAM;
    /* save a pointer to the filename */
    img->bm.data = (char*)filename;
    img->label = id;
    img->x = x;
    img->y = y;
    img->num_subimages = 1;
    img->always_display = false;
    img->display = -1;
    img->using_preloaded_icons = false;
    img->buflib_handle = -1;

    /* save current viewport */
    img->vp = &curr_vp->vp;

    if (token->type == SKIN_TOKEN_IMAGE_DISPLAY)
    {
        img->always_display = true;
    }
    else if (element->params_count == 5)
    {
        img->num_subimages = element->params[4].data.number;
        if (img->num_subimages <= 0)
            return WPS_ERROR_INVALID_PARAM;
    }

    if (!strcmp(img->bm.data, "__list_icons__"))
    {
        img->num_subimages = Icon_Last_Themeable;
        img->using_preloaded_icons = true;
    }
    
    struct skin_token_list *item = 
            (struct skin_token_list *)new_skin_token_list_item(NULL, img);
    if (!item)
        return WPS_ERROR_INVALID_PARAM;
    add_to_ll_chain(&wps_data->images, item);

    return 0;
}
struct skin_font {
    int id; /* the id from font_load */
    char *name;  /* filename without path and extension */
    int glyphs;  /* how many glyphs to reserve room for */
};
static struct skin_font skinfonts[MAXUSERFONTS];
static int parse_font_load(struct skin_element *element,
                           struct wps_token *token,
                           struct wps_data *wps_data)
{
    (void)wps_data; (void)token;
    int id = element->params[0].data.number;
    char *filename = element->params[1].data.text;
    int  glyphs;
    char *ptr;
    
    if(element->params_count > 2)
        glyphs = element->params[2].data.number;
    else
        glyphs = GLYPHS_TO_CACHE;
    if (id < 2)
    {
        DEBUGF("font id must be >= 2\n");
        return 1;
    }
#if defined(DEBUG) || defined(SIMULATOR)
    if (skinfonts[id-2].name != NULL)
    {
        DEBUGF("font id %d already being used\n", id);
    }
#endif
    /* make sure the filename contains .fnt, 
     * we dont actually use it, but require it anyway */
    ptr = strchr(filename, '.');
    if (!ptr || strncmp(ptr, ".fnt", 4))
        return WPS_ERROR_INVALID_PARAM;
    skinfonts[id-2].id = -1;
    skinfonts[id-2].name = filename;
    skinfonts[id-2].glyphs = glyphs;

    return 0;
}


#ifdef HAVE_LCD_BITMAP

static int parse_playlistview(struct skin_element *element,
                              struct wps_token *token,
                              struct wps_data *wps_data)
{
    (void)wps_data;
    struct playlistviewer *viewer = 
        (struct playlistviewer *)skin_buffer_alloc(sizeof(struct playlistviewer));
    if (!viewer)
        return WPS_ERROR_INVALID_PARAM;
    viewer->vp = &curr_vp->vp;
    viewer->show_icons = true;
    viewer->start_offset = element->params[0].data.number;
    viewer->line = element->params[1].data.code;
    
    token->value.data = (void*)viewer;
    
    return 0;
}
#endif
#ifdef HAVE_LCD_COLOR
static int parse_viewport_gradient_setup(struct skin_element *element,
                                   struct wps_token *token,
                                   struct wps_data *wps_data)
{
    (void)wps_data;
    struct gradient_config *cfg;
    if (element->params_count < 2) /* only start and end are required */
        return 1;
    cfg = (struct gradient_config *)skin_buffer_alloc(sizeof(struct gradient_config)); 
    if (!cfg)
        return 1;
    if (!parse_color(curr_screen, element->params[0].data.text, &cfg->start) ||
        !parse_color(curr_screen, element->params[1].data.text, &cfg->end))
        return 1;
    if (element->params_count > 2)
    {
        if (!parse_color(curr_screen, element->params[2].data.text, &cfg->text))
            return 1;
    }
    else
    {
        cfg->text = curr_vp->vp.fg_pattern;
    }

    token->value.data = cfg;
    return 0; 
}
#endif

static int parse_listitem(struct skin_element *element,
                        struct wps_token *token,
                        struct wps_data *wps_data)
{
    (void)wps_data;
    struct listitem *li = (struct listitem *)skin_buffer_alloc(sizeof(struct listitem));
    if (!li)
        return 1;
    token->value.data = li;
    if (element->params_count == 0)
        li->offset = 0;
    else
    {
        li->offset = element->params[0].data.number;
        if (element->params_count > 1)
            li->wrap = strcasecmp(element->params[1].data.text, "nowrap") != 0;
        else
            li->wrap = true;
    }
    return 0;
}

static int parse_listitemviewport(struct skin_element *element,
                                  struct wps_token *token,
                                  struct wps_data *wps_data)
{
#ifndef __PCTOOL__
    struct listitem_viewport_cfg *cfg = 
        (struct listitem_viewport_cfg *)skin_buffer_alloc(
                                sizeof(struct listitem_viewport_cfg));
    if (!cfg)
        return -1;
    cfg->data = wps_data;
    cfg->tile = false;
    cfg->label = element->params[0].data.text;
    cfg->width = -1;
    cfg->height = -1;
    if (!isdefault(&element->params[1]))
        cfg->width = element->params[1].data.number;
    if (!isdefault(&element->params[2]))
        cfg->height = element->params[2].data.number;
    if (element->params_count > 3 &&
        !strcmp(element->params[3].data.text, "tile"))
        cfg->tile = true;
    token->value.data = (void*)cfg;
#endif
    return 0;
}

#if (LCD_DEPTH > 1) || (defined(HAVE_REMOTE_LCD) && (LCD_REMOTE_DEPTH > 1))
static int parse_viewporttextstyle(struct skin_element *element,
                                   struct wps_token *token,
                                   struct wps_data *wps_data)
{
    (void)wps_data;
    int style;
    char *mode = element->params[0].data.text;
    unsigned colour;
    
    if (!strcmp(mode, "invert"))
    {
        style = STYLE_INVERT;
    }
    else if (!strcmp(mode, "colour") || !strcmp(mode, "color"))
    {
        if (element->params_count < 2 ||
            !parse_color(curr_screen, element->params[1].data.text, &colour))
            return 1;
        style = STYLE_COLORED|(STYLE_COLOR_MASK&colour); 
    }
#ifdef HAVE_LCD_COLOR
    else if (!strcmp(mode, "gradient"))
    {
        int num_lines;
        if (element->params_count < 2)
            num_lines = 1;
        else /* atoi() instead of using a number in the parser is because [si]
              * will select the number for something which looks like a colour
              * making the "colour" case (above) harder to parse */
            num_lines = atoi(element->params[1].data.text);
        style = STYLE_GRADIENT|NUMLN_PACK(num_lines)|CURLN_PACK(0);
    }
#endif
    else if (!strcmp(mode, "clear"))
    {
        style = STYLE_DEFAULT;
    }
    else
        return 1;
    token->value.l = style;
    return 0;
}

static int parse_viewportcolour(struct skin_element *element,
                                struct wps_token *token,
                                struct wps_data *wps_data)
{
    (void)wps_data;
    struct skin_tag_parameter *param = element->params;
    struct viewport_colour *colour = 
        (struct viewport_colour *)skin_buffer_alloc(sizeof(struct viewport_colour));
    if (!colour)
        return -1;
    if (isdefault(param))
    {
        colour->colour = get_viewport_default_colour(curr_screen,
                                   token->type == SKIN_TOKEN_VIEWPORT_FGCOLOUR);
    }
    else
    {
        if (!parse_color(curr_screen, param->data.text, &colour->colour))
            return -1;
    }
    colour->vp = &curr_vp->vp;
    token->value.data = colour;
    if (element->line == curr_viewport_element->line)
    {
        if (token->type == SKIN_TOKEN_VIEWPORT_FGCOLOUR)
        {
            curr_vp->start_fgcolour = colour->colour;
            curr_vp->vp.fg_pattern = colour->colour;
        }
        else
        {
            curr_vp->start_bgcolour = colour->colour;
            curr_vp->vp.bg_pattern = colour->colour;
        }
    }
    return 0;
}

static int parse_image_special(struct skin_element *element,
                               struct wps_token *token,
                               struct wps_data *wps_data)
{
    (void)wps_data; /* kill warning */
    (void)token;

#if LCD_DEPTH > 1
    char *filename;
    if (token->type == SKIN_TOKEN_IMAGE_BACKDROP)
    {
        if (isdefault(&element->params[0]))
        {
            filename = "-";
        }
        else
        {
            filename = element->params[0].data.text;
            /* format: %X(filename.bmp) or %X(d) */
            if (!strcmp(filename, "d"))
                filename = NULL;
        }
        wps_data->backdrop = filename;
    }
#endif

    return 0;
}
#endif

#endif /* HAVE_LCD_BITMAP */

static int parse_setting_and_lang(struct skin_element *element,
                                  struct wps_token *token,
                                  struct wps_data *wps_data)
{
    /* NOTE: both the string validations that happen in here will
     * automatically PASS on checkwps because its too hard to get
     * settings_list.c and english.lang built for it. 
     * If that ever changes remove the #ifndef __PCTOOL__'s here 
     */
    (void)wps_data;
    char *temp = element->params[0].data.text;
    int i;
    
    if (token->type == SKIN_TOKEN_TRANSLATEDSTRING)
    {
#ifndef __PCTOOL__
        i = lang_english_to_id(temp);
        if (i < 0)
            return WPS_ERROR_INVALID_PARAM;
#endif
    }
    else
    {
#ifndef __PCTOOL__
        if (find_setting_by_cfgname(temp, &i) == NULL)
            return WPS_ERROR_INVALID_PARAM;
#endif
    }
    /* Store the setting number */
    token->value.i = i;
    return 0;
}
static int parse_logical_if(struct skin_element *element,
                             struct wps_token *token,
                             struct wps_data *wps_data)
{
    (void)wps_data;
    char *op = element->params[1].data.text;
    struct logical_if *lif = skin_buffer_alloc(sizeof(struct logical_if));
    if (!lif)
        return -1;
    token->value.data = lif;
    lif->token = element->params[0].data.code->data;
    
    if (!strncmp(op, "=", 1))
        lif->op = IF_EQUALS;
    else if (!strncmp(op, "!=", 2))
        lif->op = IF_NOTEQUALS;
    else if (!strncmp(op, ">=", 2))
        lif->op = IF_GREATERTHAN_EQ;
    else if (!strncmp(op, "<=", 2))
        lif->op = IF_LESSTHAN_EQ;
    else if (!strncmp(op, ">", 2))
        lif->op = IF_GREATERTHAN;
    else if (!strncmp(op, "<", 1))
        lif->op = IF_LESSTHAN;
    
    memcpy(&lif->operand, &element->params[2], sizeof(lif->operand));
    if (element->params_count > 3)
        lif->num_options = element->params[3].data.number;
    else
        lif->num_options = TOKEN_VALUE_ONLY;
    return 0;
    
}

static int parse_timeout_tag(struct skin_element *element,
                             struct wps_token *token,
                             struct wps_data *wps_data)
{
    (void)wps_data;
    int val = 0;
    if (element->params_count == 0)
    {
        switch (token->type)
        {
            case SKIN_TOKEN_SUBLINE_TIMEOUT:
                return -1;
            case SKIN_TOKEN_BUTTON_VOLUME:
            case SKIN_TOKEN_TRACK_STARTING:
            case SKIN_TOKEN_TRACK_ENDING:
                val = 10;
                break;
            default:
                break;
        }
    }
    else
        val = element->params[0].data.number;
    token->value.i = val * TIMEOUT_UNIT;
    return 0;
}

static int parse_substring_tag(struct skin_element* element,
                                 struct wps_token *token,
                                 struct wps_data *wps_data)
{
    (void)wps_data;
    struct substring *ss = (struct substring*)skin_buffer_alloc(sizeof(struct substring));
    if (!ss)
        return 1;
    ss->start = element->params[0].data.number;
    if (element->params[1].type == DEFAULT)
        ss->length = -1;
    else
        ss->length = element->params[1].data.number;
    ss->token = element->params[2].data.code->data;
    token->value.data = ss;
    return 0;
}

static int parse_progressbar_tag(struct skin_element* element,
                                 struct wps_token *token,
                                 struct wps_data *wps_data)
{
#ifdef HAVE_LCD_BITMAP
    struct progressbar *pb;
    struct viewport *vp = &curr_vp->vp;
    struct skin_tag_parameter *param = element->params;
    int curr_param = 0;
    char *image_filename = NULL;
    
    if (element->params_count == 0 && 
        element->tag->type != SKIN_TOKEN_PROGRESSBAR)
        return 0; /* nothing to do */
    pb = (struct progressbar*)skin_buffer_alloc(sizeof(struct progressbar));
    
    token->value.data = pb;
    
    if (!pb)
        return WPS_ERROR_INVALID_PARAM;
    pb->vp = vp;
    pb->follow_lang_direction = follow_lang_direction > 0;
    pb->nofill = false;
    pb->nobar = false;
    pb->image = NULL;
    pb->slider = NULL;
    pb->backdrop = NULL;
    pb->invert_fill_direction = false;
    pb->horizontal = true;
    
    if (element->params_count == 0)
    {
        pb->x = 0;
        pb->width = vp->width;
        pb->height = SYSFONT_HEIGHT-2;
        pb->y = -1; /* Will be computed during the rendering */
        pb->type = element->tag->type;
        return 0;
    }
    
    /* (x, y, width, height, ...) */
    if (!isdefault(param))
        pb->x = param->data.number;
    else
        pb->x = 0;
    param++;
    
    if (!isdefault(param))
        pb->y = param->data.number;
    else
        pb->y = -1; /* computed at rendering */
    param++;
    
    if (!isdefault(param))
        pb->width = param->data.number;
    else
        pb->width = vp->width - pb->x;
    param++;
    
    if (!isdefault(param))
    {
        /* A zero height makes no sense - reject it */
        if (param->data.number == 0)
            return WPS_ERROR_INVALID_PARAM;

        pb->height = param->data.number;
    }
    else
    {
        if (vp->font > FONT_UI)
            pb->height = -1; /* calculate at display time */
        else
        {
#ifndef __PCTOOL__
            pb->height = font_get(vp->font)->height;
#else
            pb->height = 8;
#endif
        }
    }
    /* optional params, first is the image filename if it isnt recognised as a keyword */
    
    curr_param = 4;
    if (isdefault(&element->params[curr_param]))
    {
        param++;
        curr_param++;
    }

    pb->horizontal = pb->width > pb->height;
    while (curr_param < element->params_count)
    {
        param++;
        if (!strcmp(param->data.text, "invert"))
            pb->invert_fill_direction = true;
        else if (!strcmp(param->data.text, "nofill"))
            pb->nofill = true;
        else if (!strcmp(param->data.text, "nobar"))
            pb->nobar = true;
        else if (!strcmp(param->data.text, "slider"))
        {
            if (curr_param+1 < element->params_count)
            {
                curr_param++;
                param++;
                pb->slider = skin_find_item(param->data.text, 
                                            SKIN_FIND_IMAGE, wps_data);
            }
            else /* option needs the next param */
                return -1;
        }
        else if (!strcmp(param->data.text, "image"))
        {
            if (curr_param+1 < element->params_count)
            {
                curr_param++;
                param++;
                image_filename = param->data.text;
                
            }
            else /* option needs the next param */
                return -1;
        }
        else if (!strcmp(param->data.text, "backdrop"))
        {
            if (curr_param+1 < element->params_count)
            {
                curr_param++;
                param++;
                pb->backdrop = skin_find_item(param->data.text, 
                                              SKIN_FIND_IMAGE, wps_data);
                
            }
            else /* option needs the next param */
                return -1;
        }
        else if (!strcmp(param->data.text, "vertical"))
        {
            pb->horizontal = false;
            if (isdefault(&element->params[3]))
                pb->height = vp->height - pb->y;
        }
        else if (!strcmp(param->data.text, "horizontal"))
            pb->horizontal = true;
        else if (curr_param == 4)
            image_filename = param->data.text;
            
        curr_param++;
    }

    if (image_filename)
    {
        pb->image = skin_find_item(image_filename, SKIN_FIND_IMAGE, wps_data);
        if (!pb->image) /* load later */
        {           
            struct gui_img* img = (struct gui_img*)skin_buffer_alloc(sizeof(struct gui_img));
            if (!img)
                return WPS_ERROR_INVALID_PARAM;
            /* save a pointer to the filename */
            img->bm.data = (char*)image_filename;
            img->label = image_filename;
            img->x = 0;
            img->y = 0;
            img->num_subimages = 1;
            img->always_display = false;
            img->display = -1;
            img->using_preloaded_icons = false;
            img->buflib_handle = -1;
            img->vp = &curr_vp->vp;
            struct skin_token_list *item = 
                    (struct skin_token_list *)new_skin_token_list_item(NULL, img);
            if (!item)
                return WPS_ERROR_INVALID_PARAM;
            add_to_ll_chain(&wps_data->images, item);
            pb->image = img;
        }
    }    
        
    if (token->type == SKIN_TOKEN_VOLUME)
        token->type = SKIN_TOKEN_VOLUMEBAR;
    else if (token->type == SKIN_TOKEN_BATTERY_PERCENT)
        token->type = SKIN_TOKEN_BATTERY_PERCENTBAR;
    else if (token->type == SKIN_TOKEN_TUNER_RSSI)
        token->type = SKIN_TOKEN_TUNER_RSSI_BAR;
    else if (token->type == SKIN_TOKEN_PEAKMETER_LEFT)
        token->type = SKIN_TOKEN_PEAKMETER_LEFTBAR;
    else if (token->type == SKIN_TOKEN_PEAKMETER_RIGHT)
        token->type = SKIN_TOKEN_PEAKMETER_RIGHTBAR;
    else if (token->type == SKIN_TOKEN_LIST_NEEDS_SCROLLBAR)
        token->type = SKIN_TOKEN_LIST_SCROLLBAR;
    pb->type = token->type;
        
    return 0;
    
#else
    (void)element;
    if (token->type == SKIN_TOKEN_PROGRESSBAR ||
        token->type == SKIN_TOKEN_PLAYER_PROGRESSBAR)
    {
        wps_data->full_line_progressbar = 
                        token->type == SKIN_TOKEN_PLAYER_PROGRESSBAR;
    }
    return 0;

#endif
}

#ifdef HAVE_ALBUMART
static int parse_albumart_load(struct skin_element* element,
                               struct wps_token *token,
                               struct wps_data *wps_data)
{
    struct dim dimensions;
    int albumart_slot;
    bool swap_for_rtl = lang_is_rtl() && follow_lang_direction;
    struct skin_albumart *aa = 
        (struct skin_albumart *)skin_buffer_alloc(sizeof(struct skin_albumart));
    (void)token; /* silence warning */
    if (!aa)
        return -1;

    /* reset albumart info in wps */
    aa->width = -1;
    aa->height = -1;
    aa->xalign = WPS_ALBUMART_ALIGN_CENTER; /* default */
    aa->yalign = WPS_ALBUMART_ALIGN_CENTER; /* default */

    aa->x = element->params[0].data.number;
    aa->y = element->params[1].data.number;
    aa->width = element->params[2].data.number;
    aa->height = element->params[3].data.number;
    
    aa->vp = &curr_vp->vp;
    aa->draw_handle = -1;

    /* if we got here, we parsed everything ok .. ! */
    if (aa->width < 0)
        aa->width = 0;
    else if (aa->width > LCD_WIDTH)
        aa->width = LCD_WIDTH;

    if (aa->height < 0)
        aa->height = 0;
    else if (aa->height > LCD_HEIGHT)
        aa->height = LCD_HEIGHT;

    if (swap_for_rtl)
        aa->x = LCD_WIDTH - (aa->x + aa->width);

    aa->state = WPS_ALBUMART_LOAD;
    wps_data->albumart = aa;

    dimensions.width = aa->width;
    dimensions.height = aa->height;

    albumart_slot = playback_claim_aa_slot(&dimensions);

    if (0 <= albumart_slot)
        wps_data->playback_aa_slot = albumart_slot;
        
    if (element->params_count > 4 && !isdefault(&element->params[4]))
    {
        switch (*element->params[4].data.text)
        {
            case 'l':
            case 'L':
                if (swap_for_rtl)
                    aa->xalign = WPS_ALBUMART_ALIGN_RIGHT;
                else
                    aa->xalign = WPS_ALBUMART_ALIGN_LEFT;
                break;
            case 'c':
            case 'C':
                aa->xalign = WPS_ALBUMART_ALIGN_CENTER;
                break;
            case 'r':
            case 'R':
                if (swap_for_rtl)
                    aa->xalign = WPS_ALBUMART_ALIGN_LEFT;
                else
                    aa->xalign = WPS_ALBUMART_ALIGN_RIGHT;
                break;
        }
    }
    if (element->params_count > 5 && !isdefault(&element->params[5]))
    {
        switch (*element->params[5].data.text)
        {
            case 't':
            case 'T':
                aa->yalign = WPS_ALBUMART_ALIGN_TOP;
                break;
            case 'c':
            case 'C':
                aa->yalign = WPS_ALBUMART_ALIGN_CENTER;
                break;
            case 'b':
            case 'B':
                aa->yalign = WPS_ALBUMART_ALIGN_BOTTOM;
                break;
        }
    }
    return 0;
}

#endif /* HAVE_ALBUMART */
#ifdef HAVE_SKIN_VARIABLES
static struct skin_var* find_or_add_var(const char* label,
                                        struct wps_data *data)
{
    struct skin_var* ret = skin_find_item(label, SKIN_VARIABLE, data);
    if (!ret)
    {
        ret = (struct skin_var*)skin_buffer_alloc(sizeof(struct skin_var));
        if (!ret)
            return NULL;
        ret->label = label;
        ret->value = 1;
        ret->last_changed = 0xffff;
        struct skin_token_list *item = new_skin_token_list_item(NULL, ret);
        if (!item)
            return NULL;
        add_to_ll_chain(&data->skinvars, item);
    }
    return ret;
}
static int parse_skinvar(  struct skin_element *element,
                           struct wps_token *token,
                           struct wps_data *wps_data)
{
    const char* label = element->params[0].data.text;
    struct skin_var* var = find_or_add_var(label, wps_data);
    if (!var)
        return WPS_ERROR_INVALID_PARAM;
    switch (token->type)
    {
        case SKIN_TOKEN_VAR_GETVAL:
            token->value.data = var;
            break;
        case SKIN_TOKEN_VAR_SET:
        {
            struct skin_var_changer *data = 
                                (struct skin_var_changer*)skin_buffer_alloc(
                                            sizeof(struct skin_var_changer));
            if (!data)
                return WPS_ERROR_INVALID_PARAM;
            data->var = var;
            data->newval = element->params[2].data.number;
            data->max = 0;
            if (!strcmp(element->params[1].data.text, "set"))
                data->direct = true;
            else if (!strcmp(element->params[1].data.text, "inc"))
            {
                data->direct = false;
            }
            else if (!strcmp(element->params[1].data.text, "dec"))
            {
                data->direct = false;
                data->newval *= -1;
            }
            if (element->params_count > 3)
                data->max = element->params[3].data.number;
            token->value.data = data;
        }
        break;
        case SKIN_TOKEN_VAR_TIMEOUT:
        {
            struct skin_var_lastchange *data = 
                                (struct skin_var_lastchange*)skin_buffer_alloc(
                                            sizeof(struct skin_var_lastchange));
            if (!data)
                return WPS_ERROR_INVALID_PARAM;
            data->var = var;
            data->timeout = 10;
            if (element->params_count > 1)
                data->timeout = element->params[1].data.number;
            data->timeout *= TIMEOUT_UNIT;
            token->value.data = data;
        }
        break;
        default: /* kill the warning */
            break;
    }
    return 0;
}
#endif /* HAVE_SKIN_VARIABLES */
#ifdef HAVE_TOUCHSCREEN
static int parse_lasttouch(struct skin_element *element,
                           struct wps_token *token,
                           struct wps_data *wps_data)
{
    struct touchregion_lastpress *data = 
            (struct touchregion_lastpress*)skin_buffer_alloc(
                                sizeof(struct touchregion_lastpress));
    int i;
    if (!data)
        return WPS_ERROR_INVALID_PARAM;
    data->region = NULL;
    data->timeout = 10;
    
    for (i=0; i<element->params_count; i++)
    {
        if (element->params[i].type == STRING)
            data->region = skin_find_item(element->params[i].data.text,
                                          SKIN_FIND_TOUCHREGION, wps_data);
        else if (element->params[i].type == INTEGER ||
                 element->params[i].type == DECIMAL)
            data->timeout = element->params[i].data.number;
    }

    data->timeout *= TIMEOUT_UNIT;
    token->value.data = data;
    return 0;
}

struct touchaction {const char* s; int action;};
static const struct touchaction touchactions[] = {
    /* generic actions, convert to screen actions on use */
    {"none", ACTION_TOUCHSCREEN},       {"lock", ACTION_TOUCH_SOFTLOCK },
    {"prev", ACTION_STD_PREV },         {"next", ACTION_STD_NEXT },
    {"rwd", ACTION_STD_PREVREPEAT },    {"ffwd", ACTION_STD_NEXTREPEAT },
    {"hotkey", ACTION_STD_HOTKEY},      {"select", ACTION_STD_OK },
    {"menu", ACTION_STD_MENU },         {"cancel", ACTION_STD_CANCEL },
    {"contextmenu", ACTION_STD_CONTEXT},{"quickscreen", ACTION_STD_QUICKSCREEN },
    
    /* list/tree actions */
    { "resumeplayback", ACTION_TREE_WPS}, /* returns to previous music, WPS/FM */
    /* not really WPS specific, but no equivilant ACTION_STD_* */
    {"voldown", ACTION_WPS_VOLDOWN},    {"volup", ACTION_WPS_VOLUP},
    {"mute", ACTION_TOUCH_MUTE },
    
    /* generic settings changers */
    {"setting_inc", ACTION_SETTINGS_INC}, {"setting_dec", ACTION_SETTINGS_DEC},
    {"setting_set", ACTION_SETTINGS_SET}, 

    /* WPS specific actions */
    {"wps_prev", ACTION_WPS_SKIPPREV }, {"wps_next", ACTION_WPS_SKIPNEXT },
    {"browse", ACTION_WPS_BROWSE },
    {"play", ACTION_WPS_PLAY },         {"stop", ACTION_WPS_STOP },
    {"shuffle", ACTION_TOUCH_SHUFFLE }, {"repmode", ACTION_TOUCH_REPMODE },
    {"pitch", ACTION_WPS_PITCHSCREEN},  {"playlist", ACTION_WPS_VIEW_PLAYLIST }, 

#if CONFIG_TUNER    
    /* FM screen actions */
    /* Also allow browse, play, stop from WPS codes */
    {"mode", ACTION_FM_MODE },          {"record", ACTION_FM_RECORD },
    {"presets", ACTION_FM_PRESET}, 
#endif
};

static int touchregion_setup_setting(struct skin_element *element, int param_no,
                                     struct touchregion *region)
{
#ifndef __PCTOOL__
    int p = param_no;
    char *name = element->params[p++].data.text;
    int j;

    region->setting_data.setting = find_setting_by_cfgname(name, &j);
    if (region->setting_data.setting == NULL)
        return WPS_ERROR_INVALID_PARAM;

    if (region->action == ACTION_SETTINGS_SET)
    {
        char* text;
        int temp;
        struct touchsetting *setting = 
            &region->setting_data;
        if (element->params_count < p+1)
            return -1;

        text = element->params[p++].data.text;
        switch (settings[j].flags&F_T_MASK)
        {
        case F_T_CUSTOM:
            setting->value.text = text;
            break;                              
        case F_T_INT:
        case F_T_UINT:
            if (settings[j].cfg_vals == NULL)
            {
                setting->value.number = atoi(text);
            }
            else if (cfg_string_to_int(j, &temp, text))
            {
                if (settings[j].flags&F_TABLE_SETTING)
                    setting->value.number = 
                        settings[j].table_setting->values[temp];
                else
                    setting->value.number = temp;
            }
            else
                return -1;
            break;
        case F_T_BOOL:
            if (cfg_string_to_int(j, &temp, text))
            {
                setting->value.number = temp;
            }
            else
                return -1;
            break;
        default:
            return -1;
        }
    }
    return p-param_no;
#endif /* __PCTOOL__ */
    return 0;
}

static int parse_touchregion(struct skin_element *element,
                             struct wps_token *token,
                             struct wps_data *wps_data)
{
    (void)token;
    unsigned i, imax;
    int p;
    struct touchregion *region = NULL;
    const char *action;
    const char pb_string[] = "progressbar";
    const char vol_string[] = "volume";

    /* format: %T([label,], x,y,width,height,action[, ...])
     * if action starts with & the area must be held to happen
     */

    
    region = (struct touchregion*)skin_buffer_alloc(sizeof(struct touchregion));
    if (!region)
        return WPS_ERROR_INVALID_PARAM;

    /* should probably do some bounds checking here with the viewport... but later */
    region->action = ACTION_NONE;
    
    if (element->params[0].type == STRING)
    {
        region->label = element->params[0].data.text;
        p = 1;
        /* "[SI]III[SI]|SS" is the param list. There MUST be 4 numbers
         * followed by at least one string. Verify that here */
        if (element->params_count < 6 ||
            element->params[4].type != INTEGER)
            return WPS_ERROR_INVALID_PARAM;
    }
    else
    {
        region->label = NULL;
        p = 0;
    }
    
    region->x = element->params[p++].data.number;
    region->y = element->params[p++].data.number;
    region->width = element->params[p++].data.number;
    region->height = element->params[p++].data.number;
    region->wvp = curr_vp;
    region->armed = false;
    region->reverse_bar = false;
    region->value = 0;
    region->last_press = 0xffff;
    region->press_length = PRESS;
    region->allow_while_locked = false;
    action = element->params[p++].data.text;

    /* figure out the action */
    if(!strcmp(pb_string, action))
        region->action = ACTION_TOUCH_SCROLLBAR;
    else if(!strcmp(vol_string, action))
        region->action = ACTION_TOUCH_VOLUME;
    else
    {
        imax = ARRAYLEN(touchactions);
        for (i = 0; i < imax; i++)
        {
            /* try to match with one of our touchregion screens */
            if (!strcmp(touchactions[i].s, action))
            {
                region->action = touchactions[i].action;
                if (region->action == ACTION_SETTINGS_INC ||
                    region->action == ACTION_SETTINGS_DEC ||
                    region->action == ACTION_SETTINGS_SET)
                {
                    int val;
                    if (element->params_count < p+1)
                        return WPS_ERROR_INVALID_PARAM;
                    val = touchregion_setup_setting(element, p, region);
                    if (val < 0)
                        return WPS_ERROR_INVALID_PARAM;
                    p += val;
                }
                break;
            }
        }
        if (region->action == ACTION_NONE)
            return WPS_ERROR_INVALID_PARAM;
    }
    while (p < element->params_count)
    {
        char* param = element->params[p++].data.text;
        if (!strcmp(param, "allow_while_locked"))
            region->allow_while_locked = true;
        else if (!strcmp(param, "reverse_bar"))
            region->reverse_bar = true;
        else if (!strcmp(param, "repeat_press"))
            region->press_length = REPEAT;
        else if (!strcmp(param, "long_press"))
            region->press_length = LONG_PRESS;
    }
    struct skin_token_list *item = new_skin_token_list_item(NULL, region);
    if (!item)
        return WPS_ERROR_INVALID_PARAM;
    add_to_ll_chain(&wps_data->touchregions, item);
    
    if (region->action == ACTION_TOUCH_MUTE)
    {
        region->value = global_settings.volume;
    }
        
    
    return 0;
}
#endif

static bool check_feature_tag(const int type)
{
    switch (type)
    {
        case SKIN_TOKEN_RTC_PRESENT:
#if CONFIG_RTC
            return true;
#else
            return false;
#endif
        case SKIN_TOKEN_HAVE_RECORDING:
#ifdef HAVE_RECORDING
            return true;
#else
            return false;
#endif
        case SKIN_TOKEN_HAVE_TUNER:
#if CONFIG_TUNER
            if (radio_hardware_present())
                return true;
#endif
            return false;
        case SKIN_TOKEN_HAVE_TOUCH:
#ifdef HAVE_TOUCHSCREEN
            return true;
#else
            return false;
#endif

#if CONFIG_TUNER
        case SKIN_TOKEN_HAVE_RDS:
#ifdef HAVE_RDS_CAP
            return true;
#else
            return false;
#endif /* HAVE_RDS_CAP */
#endif /* CONFIG_TUNER */
        default: /* not a tag we care about, just don't skip */
            return true;
    }
}

/* This is used to free any buflib allocations before the rest of
 * wps_data is reset.
 * The call to this in settings_apply_skins() is the last chance to do
 * any core_free()'s before wps_data is trashed and those handles lost
 */
void skin_data_free_buflib_allocs(struct wps_data *wps_data)
{
    (void)wps_data;
#ifdef HAVE_LCD_BITMAP
#ifndef __PCTOOL__
    struct skin_token_list *list = wps_data->images;
    while (list)
    {
        struct gui_img *img = (struct gui_img*)list->token->value.data;
        if (img->buflib_handle > 0)
            core_free(img->buflib_handle);
        list = list->next;
    }
    wps_data->images = NULL;
    if (wps_data->font_ids != NULL)
    {
        while (wps_data->font_count > 0)
            font_unload(wps_data->font_ids[--wps_data->font_count]);
    }
    wps_data->font_ids = NULL;
#endif
#endif
}

/*
 * initial setup of wps_data; does reset everything
 * except fields which need to survive, i.e.
 * Also called if the load fails
 **/
static void skin_data_reset(struct wps_data *wps_data)
{
    skin_data_free_buflib_allocs(wps_data);
#ifdef HAVE_LCD_BITMAP
    wps_data->images = NULL;
#endif
    wps_data->tree = NULL;
#if LCD_DEPTH > 1 || defined(HAVE_REMOTE_LCD) && LCD_REMOTE_DEPTH > 1
    if (wps_data->backdrop_id >= 0)
        skin_backdrop_unload(wps_data->backdrop_id);
    wps_data->backdrop = NULL;
#endif
#ifdef HAVE_TOUCHSCREEN
    wps_data->touchregions = NULL;
#endif
#ifdef HAVE_SKIN_VARIABLES
    wps_data->skinvars = NULL;
#endif
#ifdef HAVE_ALBUMART
    wps_data->albumart = NULL;
    if (wps_data->playback_aa_slot >= 0)
    {
        playback_release_aa_slot(wps_data->playback_aa_slot);
        wps_data->playback_aa_slot = -1;
    }
#endif

#ifdef HAVE_LCD_BITMAP
    wps_data->peak_meter_enabled = false;
    wps_data->wps_sb_tag = false;
    wps_data->show_sb_on_wps = false;
#else /* HAVE_LCD_CHARCELLS */
    /* progress bars */
    int i;
    for (i = 0; i < 8; i++)
    {
        wps_data->wps_progress_pat[i] = 0;
    }
    wps_data->full_line_progressbar = false;
#endif
    wps_data->wps_loaded = false;
}

#ifdef HAVE_LCD_BITMAP
#ifndef __PCTOOL__
static int currently_loading_handle = -1;
static int buflib_move_callback(int handle, void* current, void* new)
{
    (void)current;
    (void)new;
    if (handle == currently_loading_handle)
        return BUFLIB_CB_CANNOT_MOVE;
    return BUFLIB_CB_OK;
}
static struct buflib_callbacks buflib_ops = {buflib_move_callback, NULL};
static void lock_handle(int handle)
{
    currently_loading_handle = handle;
}
static void unlock_handle(void)
{
    currently_loading_handle = -1;
}
#endif

static int load_skin_bmp(struct wps_data *wps_data, struct bitmap *bitmap, char* bmpdir)
{
    (void)wps_data; /* only needed for remote targets */
    char img_path[MAX_PATH];
    int fd;
    int handle;
    get_image_filename(bitmap->data, bmpdir,
                       img_path, sizeof(img_path));

    /* load the image */
    int format;
#ifdef HAVE_REMOTE_LCD
    if (curr_screen == SCREEN_REMOTE)
        format = FORMAT_ANY|FORMAT_REMOTE;
    else
#endif
        format = FORMAT_ANY|FORMAT_TRANSPARENT;

    fd = open(img_path, O_RDONLY);
    if (fd < 0)
    {
        DEBUGF("Couldn't open %s\n", img_path);
        return fd;
    }
#ifndef __PCTOOL__
    size_t buf_size = read_bmp_fd(fd, bitmap, 0, 
                                    format|FORMAT_RETURN_SIZE, NULL);
    handle = core_alloc_ex(bitmap->data, buf_size, &buflib_ops);
    if (handle < 0)
    {
#ifndef APPLICATION
        DEBUGF("Not enough skin buffer: need %zd more.\n", 
                buf_size - skin_buffer_freespace());
#endif
        close(fd);
        return handle;
    }
    lseek(fd, 0, SEEK_SET);
    lock_handle(handle);
    bitmap->data = core_get_data(handle);
    int ret = read_bmp_fd(fd, bitmap, buf_size, format, NULL);
    bitmap->data = NULL; /* do this to force a crash later if the 
                            caller doesnt call core_get_data() */
    unlock_handle();
    close(fd);
    if (ret > 0)
    {
        return handle;
    }
    else
    {
        /* Abort if we can't load an image */
        DEBUGF("Couldn't load '%s'\n", img_path);
        core_free(handle);
        return -1;
    }
#else /* !__PCTOOL__ */
    close(fd);
    return 1;
#endif
}

static bool load_skin_bitmaps(struct wps_data *wps_data, char *bmpdir)
{
    struct skin_token_list *list;
    bool retval = true; /* return false if a single image failed to load */
    
    /* regular images */
    list = wps_data->images;
    while (list)
    {
        struct gui_img *img = (struct gui_img*)list->token->value.data;
        if (img->bm.data)
        {
            if (img->using_preloaded_icons)
            {
                img->loaded = true;
                list->token->type = SKIN_TOKEN_IMAGE_DISPLAY_LISTICON;
            }
            else
            {
                img->buflib_handle = load_skin_bmp(wps_data, &img->bm, bmpdir);
                img->loaded = img->buflib_handle >= 0;
                if (img->loaded)
                    img->subimage_height = img->bm.height / img->num_subimages;
                else
                    retval = false;
            }
        }
        list = list->next;
    }

#if (LCD_DEPTH > 1) || (defined(HAVE_REMOTE_LCD) && (LCD_REMOTE_DEPTH > 1))
    wps_data->backdrop_id = skin_backdrop_assign(wps_data->backdrop, bmpdir, curr_screen);
#endif /* has backdrop support */
    return retval;
}

static bool skin_load_fonts(struct wps_data *data)
{
    /* don't spit out after the first failue to aid debugging */
    int id_array[MAXUSERFONTS];
    int font_count = 0;
    bool success = true;
    struct skin_element *vp_list;
    int font_id;
    /* walk though each viewport and assign its font */
    for(vp_list = data->tree; vp_list; vp_list = vp_list->next)
    {
        /* first, find the viewports that have a non-sys/ui-font font */
        struct skin_viewport *skin_vp =
                (struct skin_viewport*)vp_list->data;
        struct viewport *vp = &skin_vp->vp;

        font_id = skin_vp->parsed_fontid;
        if (font_id == 1)
        {   /* the usual case -> built-in fonts */
            vp->font = global_status.font_id[curr_screen];
            continue;
        }
        else if (font_id <= 0)
        {
            vp->font = FONT_SYSFIXED;
            continue;
        }

        /* now find the corresponding skin_font */
        struct skin_font *font = &skinfonts[font_id-2];
        if (!font->name)
        {
            if (success)
            {
                DEBUGF("font %d not specified\n", font_id);
            }
            success = false;
            continue;
        }

        /* load the font - will handle loading the same font again if
         * multiple viewports use the same */
        if (font->id < 0)
        {
            char path[MAX_PATH];
            snprintf(path, sizeof path, FONT_DIR "/%s", font->name);
#ifndef __PCTOOL__
            font->id = font_load_ex(path,
                font_glyphs_to_bufsize(path, skinfonts[font_id-2].glyphs));
            
#else
                font->id = font_load(path);
#endif
            //printf("[%d] %s -> %d\n",font_id, font->name, font->id);
            id_array[font_count++] = font->id;
        }

        if (font->id < 0)
        {
            DEBUGF("Unable to load font %d: '%s.fnt'\n",
                    font_id, font->name);
            font->name = NULL; /* to stop trying to load it again if we fail */
            success = false;
            continue;
        }

        /* finally, assign the font_id to the viewport */
        vp->font = font->id;
        vp->line_height = font_get(vp->font)->height;
    }
    data->font_ids = skin_buffer_alloc(font_count * sizeof(int));
    if (!success || data->font_ids == NULL)
    {
        while (font_count > 0)
        {
            if(id_array[--font_count] != -1)
                font_unload(id_array[font_count]);
        }
        data->font_ids = NULL;
        return false;
    }
    memcpy(data->font_ids, id_array, sizeof(int)*font_count);
    data->font_count = font_count;
    return success;
}

#endif /* HAVE_LCD_BITMAP */
static int convert_viewport(struct wps_data *data, struct skin_element* element)
{
    struct skin_viewport *skin_vp = 
        (struct skin_viewport *)skin_buffer_alloc(sizeof(struct skin_viewport));
    struct screen *display = &screens[curr_screen];
    
    if (!skin_vp)
        return CALLBACK_ERROR;
        
    skin_vp->hidden_flags = 0;
    skin_vp->label = NULL;
    skin_vp->is_infovp = false;
    skin_vp->parsed_fontid = 1;
    element->data = skin_vp;
    curr_vp = skin_vp;
    curr_viewport_element = element;
    
    viewport_set_defaults(&skin_vp->vp, curr_screen);
    
#if (LCD_DEPTH > 1) || (defined(HAVE_REMOTE_LCD) && (LCD_REMOTE_DEPTH > 1))
    skin_vp->start_fgcolour = skin_vp->vp.fg_pattern;
    skin_vp->start_bgcolour = skin_vp->vp.bg_pattern;
#endif
#ifdef HAVE_LCD_COLOR
    skin_vp->start_gradient.start = skin_vp->vp.lss_pattern;
    skin_vp->start_gradient.end = skin_vp->vp.lse_pattern;
    skin_vp->start_gradient.text = skin_vp->vp.lst_pattern;
#endif
    

    struct skin_tag_parameter *param = element->params;
    if (element->params_count == 0) /* default viewport */
    {
        if (!data->tree) /* first viewport in the skin */
            data->tree = element;
        skin_vp->label = VP_DEFAULT_LABEL;
        return CALLBACK_OK;
    }
    
    if (element->params_count == 6)
    {
        if (element->tag->type == SKIN_TOKEN_UIVIEWPORT_LOAD)
        {
            skin_vp->is_infovp = true;
            if (isdefault(param))
            {
                skin_vp->hidden_flags = VP_NEVER_VISIBLE;
                skin_vp->label = VP_DEFAULT_LABEL;
            }
            else
            {
                skin_vp->hidden_flags = VP_NEVER_VISIBLE;
                skin_vp->label = param->data.text;
            }
        }
        else
        {
                skin_vp->hidden_flags = VP_DRAW_HIDEABLE|VP_DRAW_HIDDEN;
                skin_vp->label = param->data.text;
        }
        param++;
    }
    /* x */
    if (!isdefault(param))
    {
        skin_vp->vp.x = param->data.number;
        if (param->data.number < 0)
            skin_vp->vp.x += display->lcdwidth;
    }
    param++;
    /* y */
    if (!isdefault(param))
    {
        skin_vp->vp.y = param->data.number;
        if (param->data.number < 0)
            skin_vp->vp.y += display->lcdheight;
    }
    param++;
    /* width */
    if (!isdefault(param))
    {
        skin_vp->vp.width = param->data.number;
        if (param->data.number < 0)
            skin_vp->vp.width = (skin_vp->vp.width + display->lcdwidth) - skin_vp->vp.x;
    }
    else
    {
        skin_vp->vp.width = display->lcdwidth - skin_vp->vp.x;
    }
    param++;
    /* height */
    if (!isdefault(param))
    {
        skin_vp->vp.height = param->data.number;
        if (param->data.number < 0)
            skin_vp->vp.height = (skin_vp->vp.height + display->lcdheight) - skin_vp->vp.y;
    }
    else
    {
        skin_vp->vp.height = display->lcdheight - skin_vp->vp.y;
    }
    param++;
#ifdef HAVE_LCD_BITMAP
    /* font */
    if (!isdefault(param))
        skin_vp->parsed_fontid = param->data.number;
#endif
    if ((unsigned) skin_vp->vp.x >= (unsigned) display->lcdwidth ||
        skin_vp->vp.width + skin_vp->vp.x > display->lcdwidth ||
        (unsigned) skin_vp->vp.y >= (unsigned) display->lcdheight ||
        skin_vp->vp.height + skin_vp->vp.y > display->lcdheight)
        return CALLBACK_ERROR;

    return CALLBACK_OK;
}

static int skin_element_callback(struct skin_element* element, void* data)
{
    struct wps_data *wps_data = (struct wps_data *)data;
    struct wps_token *token;
    parse_function function = NULL;
    
    switch (element->type)
    {
        /* IMPORTANT: element params are shared, so copy them if needed
         *            or use then NOW, dont presume they have a long lifespan
         */
        case TAG:
        {
            token = (struct wps_token*)skin_buffer_alloc(sizeof(struct wps_token));
            memset(token, 0, sizeof(*token));
            token->type = element->tag->type;
            
            if (element->tag->flags&SKIN_RTC_REFRESH)
            {
#if CONFIG_RTC
                curr_line->update_mode |= SKIN_REFRESH_DYNAMIC;
#else
                curr_line->update_mode |= SKIN_REFRESH_STATIC;
#endif
            }
            else
                curr_line->update_mode |= element->tag->flags&SKIN_REFRESH_ALL;
            
            element->data = token;
            
            /* Some tags need special handling for the tag, so add them here */
            switch (token->type)
            {
                case SKIN_TOKEN_ALIGN_LANGDIRECTION:
                    follow_lang_direction = 2;
                    break;
                case SKIN_TOKEN_LOGICAL_IF:
                    function = parse_logical_if;
                    break;
                case SKIN_TOKEN_SUBSTRING:
                    function = parse_substring_tag;
                    break;
                case SKIN_TOKEN_PROGRESSBAR:
                case SKIN_TOKEN_VOLUME:
                case SKIN_TOKEN_BATTERY_PERCENT:
                case SKIN_TOKEN_PLAYER_PROGRESSBAR:
                case SKIN_TOKEN_PEAKMETER_LEFT:
                case SKIN_TOKEN_PEAKMETER_RIGHT:
                case SKIN_TOKEN_LIST_NEEDS_SCROLLBAR:
#ifdef HAVE_RADIO_RSSI
                case SKIN_TOKEN_TUNER_RSSI:
#endif
                    function = parse_progressbar_tag;
                    break;
                case SKIN_TOKEN_SUBLINE_TIMEOUT:
                case SKIN_TOKEN_BUTTON_VOLUME:
                case SKIN_TOKEN_TRACK_STARTING:
                case SKIN_TOKEN_TRACK_ENDING:
                    function = parse_timeout_tag;
                    break;
#ifdef HAVE_LCD_BITMAP
                case SKIN_TOKEN_LIST_ITEM_TEXT:
                case SKIN_TOKEN_LIST_ITEM_ICON:
                    function = parse_listitem;
                    break;
                case SKIN_TOKEN_DISABLE_THEME:
                case SKIN_TOKEN_ENABLE_THEME:
                case SKIN_TOKEN_DRAW_INBUILTBAR:
                    function = parse_statusbar_tags;
                    break;
                case SKIN_TOKEN_LIST_TITLE_TEXT:
#ifndef __PCTOOL__
                    sb_skin_has_title(curr_screen);
#endif
                    break;
#endif
                case SKIN_TOKEN_FILE_DIRECTORY:
                    token->value.i = element->params[0].data.number;
                    break;
#if (LCD_DEPTH > 1) || (defined(HAVE_REMOTE_LCD) && (LCD_REMOTE_DEPTH > 1))
                case SKIN_TOKEN_VIEWPORT_FGCOLOUR:
                case SKIN_TOKEN_VIEWPORT_BGCOLOUR:
                    function = parse_viewportcolour;
                    break;
                case SKIN_TOKEN_IMAGE_BACKDROP:
                    function = parse_image_special;
                    break;
                case SKIN_TOKEN_VIEWPORT_TEXTSTYLE:
                    function = parse_viewporttextstyle;
                    break;
#endif
#ifdef HAVE_LCD_COLOR
                case SKIN_TOKEN_VIEWPORT_GRADIENT_SETUP:
                    function = parse_viewport_gradient_setup;
                    break;
#endif
                case SKIN_TOKEN_TRANSLATEDSTRING:
                case SKIN_TOKEN_SETTING:
                    function = parse_setting_and_lang;
                    break;
#ifdef HAVE_LCD_BITMAP
                case SKIN_TOKEN_VIEWPORT_CUSTOMLIST:
                    function = parse_playlistview;
                    break;
                case SKIN_TOKEN_LOAD_FONT:
                    function = parse_font_load;
                    break;
                case SKIN_TOKEN_VIEWPORT_ENABLE:
                case SKIN_TOKEN_UIVIEWPORT_ENABLE:
                    token->value.data = element->params[0].data.text;
                    break;
                case SKIN_TOKEN_IMAGE_PRELOAD_DISPLAY:
                    function = parse_image_display;
                    break;
                case SKIN_TOKEN_IMAGE_PRELOAD:
                case SKIN_TOKEN_IMAGE_DISPLAY:
                    function = parse_image_load;
                    break;
                case SKIN_TOKEN_LIST_ITEM_CFG:
                    function = parse_listitemviewport;
                    break;
#endif
#ifdef HAVE_TOUCHSCREEN
                case SKIN_TOKEN_TOUCHREGION:
                    function = parse_touchregion;
                    break;
                case SKIN_TOKEN_LASTTOUCH:
                    function = parse_lasttouch;
                    break;
#endif
#ifdef HAVE_ALBUMART
                case SKIN_TOKEN_ALBUMART_DISPLAY:
                    if (wps_data->albumart)
                        wps_data->albumart->vp = &curr_vp->vp;
                    break;
                case SKIN_TOKEN_ALBUMART_LOAD:
                    function = parse_albumart_load;
                    break;
#endif
#ifdef HAVE_SKIN_VARIABLES
                case SKIN_TOKEN_VAR_SET:
                case SKIN_TOKEN_VAR_GETVAL:
                case SKIN_TOKEN_VAR_TIMEOUT:
                    function = parse_skinvar;
                    break;
#endif
                default:
                    break;
            }
            if (function)
            {
                if (function(element, token, wps_data) < 0)
                    return CALLBACK_ERROR;
            }
            /* tags that start with 'F', 'I' or 'D' are for the next file */
            if ( *(element->tag->name) == 'I' || *(element->tag->name) == 'F' ||
                 *(element->tag->name) == 'D')
                token->next = true;
            if (follow_lang_direction > 0 )
                follow_lang_direction--;
            break;
        }
        case VIEWPORT:
            return convert_viewport(wps_data, element);
        case LINE:
        {
            struct line *line = 
                (struct line *)skin_buffer_alloc(sizeof(struct line));
            line->update_mode = SKIN_REFRESH_STATIC;
            curr_line = line;
            element->data = line;
        }
        break;
        case LINE_ALTERNATOR:
        {
            struct line_alternator *alternator = 
                (struct line_alternator *)skin_buffer_alloc(sizeof(struct line_alternator));
            alternator->current_line = 0;
#ifndef __PCTOOL__
            alternator->next_change_tick = current_tick;
#endif
            element->data = alternator;
        }
        break;
        case CONDITIONAL:
        {
            struct conditional *conditional = 
                (struct conditional *)skin_buffer_alloc(sizeof(struct conditional));
            conditional->last_value = -1;
            conditional->token = element->data;
            element->data = conditional;
            if (!check_feature_tag(element->tag->type))
            {
                return FEATURE_NOT_AVAILABLE;
            }
            return CALLBACK_OK;
        }
        case TEXT:
            curr_line->update_mode |= SKIN_REFRESH_STATIC;
            break;
        default:
            break;
    }
    return CALLBACK_OK;
}

/* to setup up the wps-data from a format-buffer (isfile = false)
   from a (wps-)file (isfile = true)*/
bool skin_data_load(enum screen_type screen, struct wps_data *wps_data,
                    const char *buf, bool isfile)
{
    char *wps_buffer = NULL;
    if (!wps_data || !buf)
        return false;
#ifdef HAVE_ALBUMART
    int status;
    struct mp3entry *curtrack;
    long offset;
    struct skin_albumart old_aa = {.state = WPS_ALBUMART_NONE};
    if (wps_data->albumart)
    {
        old_aa.state = wps_data->albumart->state;
        old_aa.height = wps_data->albumart->height;
        old_aa.width = wps_data->albumart->width;
    }
#endif
#ifdef HAVE_LCD_BITMAP
    int i;
    for (i=0;i<MAXUSERFONTS;i++)
    {
        skinfonts[i].id = -1;
        skinfonts[i].name = NULL;
    }
#endif
#ifdef DEBUG_SKIN_ENGINE
    if (isfile && debug_wps)
    {
        DEBUGF("\n=====================\nLoading '%s'\n=====================\n", buf);
    }
#endif


    skin_data_reset(wps_data);
    wps_data->wps_loaded = false;
    curr_screen = screen;
    curr_line = NULL;
    curr_vp = NULL;
    curr_viewport_element = NULL;

    if (isfile)
    {
        int fd = open_utf8(buf, O_RDONLY);

        if (fd < 0)
            return false;

        /* get buffer space from the plugin buffer */
        size_t buffersize = 0;
        wps_buffer = (char *)plugin_get_buffer(&buffersize);

        if (!wps_buffer)
            return false;

        /* copy the file's content to the buffer for parsing,
           ensuring that every line ends with a newline char. */
        unsigned int start = 0;
        while(read_line(fd, wps_buffer + start, buffersize - start) > 0)
        {
            start += strlen(wps_buffer + start);
            if (start < buffersize - 1)
            {
                wps_buffer[start++] = '\n';
                wps_buffer[start] = 0;
            }
        }
        close(fd);
        if (start <= 0)
            return false;
    }
    else
    {
        wps_buffer = (char*)buf;
    }
#if LCD_DEPTH > 1 || defined(HAVE_REMOTE_LCD) && LCD_REMOTE_DEPTH > 1
    wps_data->backdrop = "-";
    wps_data->backdrop_id = -1;
#endif
    /* parse the skin source */
#ifndef APPLICATION
    skin_buffer_save_position();
#endif
    wps_data->tree = skin_parse(wps_buffer, skin_element_callback, wps_data);
    if (!wps_data->tree) {
        skin_data_reset(wps_data);
#ifndef APPLICATION
        skin_buffer_restore_position();
#endif
        return false;
    }

#ifdef HAVE_LCD_BITMAP
    char bmpdir[MAX_PATH];
    if (isfile)
    {
        /* get the bitmap dir */
        char *dot = strrchr(buf, '.');
        strlcpy(bmpdir, buf, dot - buf + 1);
    }
    else
    {
        snprintf(bmpdir, MAX_PATH, "%s", BACKDROP_DIR);
    }
    /* load the bitmaps that were found by the parsing */
    if (!load_skin_bitmaps(wps_data, bmpdir) ||
        !skin_load_fonts(wps_data)) 
    {
        skin_data_reset(wps_data);
#ifndef APPLICATION
        skin_buffer_restore_position();
#endif
        return false;
    }
#endif
#if defined(HAVE_ALBUMART) && !defined(__PCTOOL__)
    status = audio_status();
    if (status & AUDIO_STATUS_PLAY)
    {
        struct skin_albumart *aa = wps_data->albumart;
        if (aa && ((aa->state && !old_aa.state) ||
            (aa->state &&
            (((old_aa.height != aa->height) ||
            (old_aa.width != aa->width))))))
        {
            curtrack = audio_current_track();
            offset = curtrack->offset;
            audio_stop();
            if (!(status & AUDIO_STATUS_PAUSE))
                audio_play(offset);
        }
    }
#endif
    wps_data->wps_loaded = true;
#ifdef DEBUG_SKIN_ENGINE
 //   if (isfile && debug_wps)
 //       debug_skin_usage();
#endif
    return true;
}
