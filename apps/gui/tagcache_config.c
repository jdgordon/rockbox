/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2011 Jonathan Gordon
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
#include <stdlib.h>
#include <string.h>
#include "inttypes.h"
#include "config.h"
#include "core_alloc.h"
#include "filetypes.h"
#include "language.h"
#include "list.h"
#include "lang.h"
#include "settings.h"
#include "plugin.h"


/*
 * Order for changing child states:
 * 1) expand folder (skip to 2 if empty)
 * 2) collapse and select
 * 3) unselect
 */

enum child_state {
    EXPANDED,
    SELECTED,
    COLLAPSED
};
    

struct child {
    char* name;
    struct folder *folder;
    enum child_state state;
};

struct folder {
    char *name;
    struct child *children;
    int children_count;
    int depth;

    struct folder* previous;
};

static char *buffer_front, *buffer_end;
static char* folder_alloc(size_t size)
{
    char* retval;
    /* 32-bit aligned */
    size = (size + 3) & ~3;
    if (buffer_front + size > buffer_end)
    {
        printf("OOM folder_alloc()\n");
        return NULL;
    }
    retval = buffer_front;
    buffer_front += size;
    return retval;
}
static char* folder_alloc_from_end(size_t size)
{
    if (buffer_end - size < buffer_front)
    {
        printf("OOM folder_alloc_from_end()\n");
        return NULL;
    }
    buffer_end -= size;
    return buffer_end;
}
    

static char* get_full_path(struct folder *start)
{
    static char buffer[MAX_PATH];
    char *buf = &buffer[MAX_PATH-1];
    int remaining = MAX_PATH-1;
    struct folder *this = start;
    if (start == NULL)
        return "/";
    buf[0] = '\0';
    buf--;
    remaining--;
    while (this)
    {
        int len = strlen(this->name);
        if (len + 1 > remaining)
            return NULL;
        buf -= len + 1;
        memcpy(buf, this->name, len);
        buf[len] = '/';
        this = this->previous;
    }
    if (remaining < 1)
        return NULL;
    /* remove the trailing / */
    buf[strlen(buf)-1] = '\0';
    return buf;
}
/* support function for qsort() */
static int compare(const void* p1, const void* p2)
{
    struct child *left = (struct child*)p1;
    struct child *right = (struct child*)p2;
    return strcasecmp(left->name, right->name);
}

static struct folder* load_folder(struct folder* parent, char *folder)
{
    DIR *dir;
    char* path = get_full_path(parent);
    char fullpath[MAX_PATH];
    struct dirent *entry;
    struct folder* this = (struct folder*)folder_alloc(sizeof(struct folder));
    int child_count = 0;
    char *first_child = NULL;

    snprintf(fullpath, MAX_PATH, "%s/%s", parent ? path : "", folder);
    
    if (!this)
        return NULL;
    dir = opendir(fullpath);
    if (!dir)
        return NULL;
    this->previous = parent;
    this->name = folder;
    this->children = NULL;
    this->children_count = 0;
    this->depth = parent ? parent->depth + 1 : -1;
    
    while ((entry = readdir(dir))) {
        int len = strlen((char *)entry->d_name);
        struct dirinfo info;

        info = dir_get_info(dir, entry);

        /* skip anything not a directory */
        if ((info.attribute & ATTR_DIRECTORY) == 0) {
            continue;
        }
        /* skip directories . and .. */
        if ((((len == 1) && (!strncmp((char *)entry->d_name, ".", 1))) ||
             ((len == 2) && (!strncmp((char *)entry->d_name, "..", 2))))) {
            continue;
        }
        char *name = folder_alloc_from_end(len+1);
        if (!name)
            return NULL;
        memcpy(name, (char *)entry->d_name, len+1);
        child_count++;
        first_child = name;
    }
    closedir(dir);
    /* now put the names in the array */
    this->children = (struct child*)folder_alloc(sizeof(struct child) * child_count);
    if (!this->children)
        return NULL;
    while (child_count)
    {
        this->children[this->children_count].name = first_child;
        this->children[this->children_count].folder = NULL;
        this->children[this->children_count].state = COLLAPSED;
        this->children_count++;
        first_child += strlen(first_child) + 1;
        child_count--;
    }
    qsort(this->children, this->children_count, sizeof(struct child), compare);
    return this;
}

static int count_items(struct folder *start)
{
    int count = 0;
    int i;

    for (i=0; i<start->children_count; i++)
    {
        struct child *foo = &start->children[i];
        if (foo->state == EXPANDED)
            count += count_items(foo->folder);
        count++;
    }
    return count;
}    

static struct child* find_index(struct folder *start, int index, struct folder **parent)
{
    int i = 0;
    while (i < start->children_count)
    {
        struct child *foo = &start->children[i];
        if (i == index)
        {
            *parent = start;
            return foo;
        }
        i++;
        if (foo->state == EXPANDED)
        {
            struct child *bar = find_index(foo->folder, index - i, parent);
            if (bar)
            {
                return bar;
            }
            index -= count_items(foo->folder);
        }
    }
    return NULL;
}

static const char * folder_get_name(int selected_item, void * data,
                                   char * buffer, size_t buffer_len)
{
    (void)buffer_len;
    struct folder *root = (struct folder*)data;
    struct folder *parent = NULL;
    struct child *this = find_index(root, selected_item, &parent);
    
    buffer[0] = '\0';
    if (parent->depth >= 0)
    {
        int i = 0;
        while (i < parent->depth + 1)
        {
            strcat(buffer, "\t");
            i++;
        }
    }
    strcat(buffer, this->name);
    return buffer;
}

static enum themable_icons folder_get_icon(int selected_item, void * data)
{
    struct folder *root = (struct folder*)data;
    struct folder *parent = NULL;
    struct child *this = find_index(root, selected_item, &parent);

    switch (this->state)
    {
        case SELECTED:
            return Icon_Cursor;
        case COLLAPSED:
            return Icon_Folder;
        case EXPANDED:
            return Icon_Submenu;
    }
    return Icon_NOICON;
}
static bool list_dirty;
static int folder_action_callback(int action, struct gui_synclist *list)
{
    struct folder *root = (struct folder*)list->data;
    if (action == ACTION_STD_OK)
    {
        struct folder *parent = NULL;
        struct child *this = find_index(root, list->selected_item, &parent);        
        switch (this->state)
        {
            case EXPANDED:
                this->state = SELECTED;
                break;
            case SELECTED:
                this->state = COLLAPSED;
                if (this->folder && this->folder->children_count == 0)
                    this->state = COLLAPSED;
                break;
            case COLLAPSED:
                if (this->folder == NULL)
                    this->folder = load_folder(parent, this->name);
                this->state = this->folder->children_count == 0 ?
                        SELECTED : EXPANDED;
        }
        list->nb_items = count_items(root);
        list_dirty = true;
        return ACTION_REDRAW;
    }
    return action;
}

static struct child* find_from_filename(char* filename, struct folder *root)
{
    char *slash = strchr(filename, '/');
    int i = 0;
    if (slash)
        *slash = '\0';
    if (!root)
        return NULL;

    while (i < root->children_count)
    {
        struct child *this = &root->children[i];
        if (!strcasecmp(this->name, filename))
        {
            if (!slash)
                return this;
            if (!this->folder)
                this->folder = load_folder(root, this->name);
            this->state = EXPANDED;
            return find_from_filename(slash+1, this->folder);
        }
        i++;
    }
    return NULL;
}

static int readline_callback(int n, char *buf, void *parameters)
{
    (void)n;
    struct folder *root = (struct folder*)parameters;
    char* slash = strchr(buf, '/');
    struct child *item = find_from_filename(slash ? slash + 1 : buf, root);
    if (item)
        item->state = SELECTED;
    return 0;
}

static void save_folders(struct folder *root, int fd)
{
    int i = 0;

    while (i < root->children_count)
    {
        struct child *this = &root->children[i];
        if (this->state == SELECTED)
        {
            if (this->folder)
                snprintf(buffer_front, buffer_end - buffer_front,
                        "%s\n", get_full_path(this->folder));
            else
                snprintf(buffer_front, buffer_end - buffer_front,
                        "%s/%s\n", get_full_path(root), this->name);
            write(fd, buffer_front, strlen(buffer_front));
        }
        else if (this->state == EXPANDED)
            save_folders(this->folder, fd);
        i++;
    }
}
        
void tagcache_do_config(void)
{
    struct folder *root;
    struct simplelist_info info;
    size_t buf_size;
    int fd;
    char buf[512];

    buffer_front = plugin_get_buffer(&buf_size);
    buffer_end = buffer_front + buf_size;
    root = load_folder(NULL, "");
    
    fd = open_utf8(ROCKBOX_DIR "/database.txt", O_RDONLY);
    if (fd >= 0)
    {
        fast_readline(fd, buf, sizeof buf, root, readline_callback);
        close(fd);
    }
    list_dirty = false;

    simplelist_info_init(&info, str(LANG_SELECT_TAGCACHE_FOLDERS),
            count_items(root), root);
    info.get_name = folder_get_name;
    info.action_callback = folder_action_callback;
    info.get_icon = folder_get_icon;
    simplelist_show_list(&info);

    if (list_dirty && yesno_pop(ID2P(LANG_SAVE_CHANGES)))
    {
        fd = open_utf8(ROCKBOX_DIR "/database.txt", O_CREAT|O_TRUNC|O_RDWR);
        if (fd >= 0)
            save_folders(root, fd);
    }
}
