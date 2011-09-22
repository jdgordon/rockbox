
#include <string.h>
#include <stdlib.h>
#include "core_alloc.h"
#include "buflib.h"
#include "buffer.h"

/* not static so it can be discovered by core_get_data() */
struct buflib_context core_ctx;
void core_allocator_init(void)
{
    buffer_init();
    size_t size;
    void *start = buffer_get_buffer(&size);
    buflib_init(&core_ctx, start, size);
    buffer_release_buffer(size);
}

int core_alloc_ex(const char* name, size_t size, struct buflib_callbacks *ops)
{
#ifdef APPLICATION
    int handle = buflib_alloc_ex(&core_ctx, sizeof(void*), name, ops);
    char* buffer = (char*)malloc(size);

    if (handle >= 0 && buffer)
    {
        char** data_ptr = core_get_data(handle);
        *data_ptr = buffer;
        return CORE_TO_APP(handle);
    }
    else if (buffer)
    {
        free(buffer);
        handle = -1;
    }
    else
        buflib_free(&core_ctx, handle);
#else
    int handle = buflib_alloc_ex(&core_ctx, size, name, ops);
#endif
    return handle;
}

int core_alloc(const char* name, size_t size)
{
    return core_alloc_ex(name, size, NULL);
}

size_t core_available(void)
{
    return buflib_available(&core_ctx);
}

int core_free(int handle)
{
#ifdef APPLICATION
    if (IS_APP_HANDLE(handle))
    {
        handle = APP_TO_CORE(handle);
        char** buf = core_get_data(handle);
        free(*buf);
    }
#endif
    return buflib_free(&core_ctx, handle);
}

int core_alloc_maximum(const char* name, size_t *size, struct buflib_callbacks *ops)
{
    int handle = buflib_alloc_maximum(&core_ctx, name, size, ops);
    return handle;
}

bool core_shrink(int handle, void* new_start, size_t new_size)
{
#ifdef APPLICATION
    if (IS_APP_HANDLE(handle))
    {
        return false;
    }
#endif
    return buflib_shrink(&core_ctx, handle, new_start, new_size);
}

int core_get_num_blocks(void)
{
    return buflib_get_num_blocks(&core_ctx);
}

void core_print_block_at(int block_num, char* buf, size_t bufsize)
{
    buflib_print_block_at(&core_ctx, block_num, buf, bufsize);
}
