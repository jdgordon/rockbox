
#ifndef __CORE_ALLOC_H__
#define __CORE_ALLOC_H__
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "buflib.h"

/* All functions below are wrappers for functions in buflib.h, except
 * they have a predefined context
 */
void core_allocator_init(void);
int core_alloc(const char* name, size_t size);
int core_alloc_ex(const char* name, size_t size, struct buflib_callbacks *ops);
int core_alloc_maximum(const char* name, size_t *size, struct buflib_callbacks *ops);
bool core_shrink(int handle, void* new_start, size_t new_size);
int core_free(int handle);
size_t core_available(void);

/* DO NOT ADD wrappers for buflib_buffer_out/in. They do not call
 * the move callbacks and are therefore unsafe in the core */

#ifdef BUFLIB_DEBUG_BLOCKS
void core_print_allocs(void (*print)(const char*));
void core_print_blocks(void (*print)(const char*));
#endif
#ifdef BUFLIB_DEBUG_BLOCK_SINGLE
int  core_get_num_blocks(void);
void core_print_block_at(int block_num, char* buf, size_t bufsize);
#endif

/* 
 * macros to help convert between application handles and core handles.
 * 1<<31 would be a negative number so an error value, so use bit 30 to
 * identify application handles. App handles use malloc() as the actual data
 * store
 */
#define APP_TO_CORE(h) ((h)&(~(1<<30)))
#define CORE_TO_APP(h) ((h)|(1<<30))
#define IS_APP_HANDLE(h) ((h)&(1<<30))

static inline void* core_get_data(int handle)
{
    extern struct buflib_context core_ctx;
#ifdef APPLICATION
    if (IS_APP_HANDLE(handle))
    {
        char **buf = buflib_get_data(&core_ctx, APP_TO_CORE(handle));
        return *buf;
    }
#endif
    return buflib_get_data(&core_ctx, handle);
}
#endif /* __CORE_ALLOC_H__ */
