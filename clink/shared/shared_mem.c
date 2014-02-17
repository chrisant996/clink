/* Copyright (c) 2013 Martin Ridgers
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "shared_mem.h"
#include "util.h"

//------------------------------------------------------------------------------
static int get_shared_mem_size(int page_count)
{
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    return page_count * sys_info.dwPageSize;
}

//------------------------------------------------------------------------------
static void generate_shared_mem_name(char* out, const char* tag, int id)
{
    strcpy(out, "Local\\");
    itoa(id, out + strlen(out), 16);
    strcat(out, "_");
    strcat(out, tag);
}

//------------------------------------------------------------------------------
static void* map_shared_mem(HANDLE handle, int size)
{
    void* ptr;

    ptr = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (ptr == NULL)
    {
        LOG_ERROR("Failed to map shared memory %p", handle);
        CloseHandle(handle);
        return NULL;
    }

    return ptr;
}

//------------------------------------------------------------------------------
shared_mem_t* create_shared_mem(int page_count, const char* tag, int id)
{
    char name[256];
    void* ptr;
    HANDLE handle;
    shared_mem_t* info;
    int size;

    generate_shared_mem_name(name, tag, id);

    // Create the mapping
    size = get_shared_mem_size(page_count);
    handle = CreateFileMapping(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        size,
        name
    );
    if (handle == NULL)
    {
        LOG_ERROR("Failed to create shared memory %s", name);
        return NULL;
    }

    // Resolve to a pointer
    ptr = map_shared_mem(handle, size);
    if (ptr == NULL)
    {
        LOG_ERROR("Failed to map shared memory");
        return NULL;
    }

    info = malloc(sizeof(shared_mem_t));
    info->handle = handle;
    info->ptr = ptr;
    info->size = size;
    return info;
}

//------------------------------------------------------------------------------
shared_mem_t* open_shared_mem(int page_count, const char* tag, int id)
{
    char name[256];
    void* ptr;
    HANDLE handle;
    shared_mem_t* info;
    int size;

    generate_shared_mem_name(name, tag, id);

    // Open the shared page.
    handle = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (handle == NULL)
    {
        LOG_ERROR("Failed to open shared memory %s", name);
        return NULL;
    }

    // Resolve it to a memory address.
    size = get_shared_mem_size(page_count);
    ptr = map_shared_mem(handle, size);
    if (ptr == NULL)
    {
        LOG_ERROR("Failed to map shared memory");
        return NULL;
    }

    info = malloc(sizeof(shared_mem_t));
    info->handle = handle;
    info->ptr = ptr;
    info->size = size;
    return info;
}

//------------------------------------------------------------------------------
void close_shared_mem(shared_mem_t* info)
{
    if (info->ptr != NULL)
    {
        UnmapViewOfFile(info->ptr);
    }

    if (info->handle != NULL)
    {
        CloseHandle(info->handle);
    }

    free(info);
}

// vim: expandtab
