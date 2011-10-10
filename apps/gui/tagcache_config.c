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
#include "plugin.h"

struct child {
    char* name;
    struct folder *folder;
    bool collapse_folder;
};

struct folder {
    char *name;
    bool selected;
    struct child *children;
    int children_count;

    struct folder* previous;
};

static char* buffer_front;
static size_t buffer_remaining;
static char* folder_alloc(size_t size)
{
    char* retval;
    /* 32-bit aligned */
    size = (size + 3) & ~3;
    if (size > buffer_remaining)
        return NULL;

    /* Other code touches audiobuf. Make sure it stays aligned */
    buffer_front = (void *)(((unsigned long)buffer_front + 3) & ~3);

    retval = buffer_front;
    buffer_front += size;
    return retval;
}
static char* folder_alloc_from_end(size_t size)
{
    if (buffer_remaining < size)
        return NULL;
    buffer_remaining -= size;
    return &buffer_front[buffer_remaining];
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
    buf--;
    *buf = '/';
    // remove the trailing /
    buf[strlen(buf)-1] = '\0';
    return buf;
}
/* support function for qsort() */
static int compare(const void* p1, const void* p2)
{
    struct child *left = p1;
    struct child *right = p2;
    return strcasecmp(left->name, right->name);
}

static struct folder* load_folder(struct folder* parent, char *folder)
{
    DIR *dir;
    char* path = get_full_path(parent);
    char fullpath[MAX_PATH];
    struct dirent *entry;
    struct folder* this = folder_alloc(sizeof(struct folder));
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
    this->children = folder_alloc(sizeof(struct child) * child_count);
    if (!this->children)
        return NULL;
    while (child_count)
    {
        this->children[this->children_count].name = first_child;
        this->children[this->children_count].folder = NULL;
        this->children[this->children_count].collapse_folder = true;
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
        if (!foo->collapse_folder)
            count += count_items(foo->folder);
        count++;
    }
    return count;
}    

struct child* find_index(struct folder *start, int index)
{
    int i = 0;
    while (i < start->children_count)
    {
        struct child *foo = &start->children[i];
        if (i == index)
            return foo;
        i++;
        if (!foo->collapse_folder)
        {
            struct child *bar = find_index(foo->folder, index - i);
            if (bar)
                return bar;
            index -= count_items(foo->folder);
        }
    }
    return NULL;
}

static struct folder* find_item_parent(struct folder *start, int index)
{
    int i = 0;
    while (i < start->children_count)
    {
        struct child *foo = &start->children[i];
        if (i == index)
            return start;
        i++;
        if (!foo->collapse_folder)
        {
            struct folder *bar;
            bar = find_item_parent(foo->folder, index - i);
            if (bar)
                return foo;
            index -= count_items(foo->folder);
        }
    }
    return NULL;
}

static const char * folder_get_name(int selected_item, void * data,
                                   char * buffer, size_t buffer_len)
{
    struct child *this = find_index(data, selected_item);
    return this->name;
}
static int folder_action_callback(int action, struct gui_synclist *list)
{
    if (action == ACTION_STD_OK)
    {
        struct child *this = find_index(list->data, list->selected_item);
        if (this->collapse_folder && this->folder == NULL)
        {
            struct folder *parent = find_item_parent(list->data, list->selected_item);
            this->folder = load_folder(parent, this->name);
            this->collapse_folder = false;
        }
        else
            this->collapse_folder = !this->collapse_folder;
        list->nb_items = count_items(list->data);
        return ACTION_REDRAW;
    }
    return action;
}

void tagcache_do_config(void)
{
    struct folder *root;
    struct simplelist_info info;
    buffer_front = plugin_get_buffer(&buffer_remaining);
    root = load_folder(NULL, "");

    simplelist_info_init(&info, "hello", count_items(root), root);
    info.get_name = folder_get_name;
    info.action_callback = folder_action_callback;
    while (1)
        simplelist_show_list(&info);
    
}
