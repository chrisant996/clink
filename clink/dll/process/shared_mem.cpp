// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "shared_mem.h"

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
    if (ptr == nullptr)
    {
        CloseHandle(handle);
        return nullptr;
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
    handle = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        size, name);
    if (handle == nullptr)
        return nullptr;

    // Resolve to a pointer
    ptr = map_shared_mem(handle, size);
    if (ptr == nullptr)
        return nullptr;

    info = (shared_mem_t*)malloc(sizeof(shared_mem_t));
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
    if (handle == nullptr)
        return nullptr;

    // Resolve it to a memory address.
    size = get_shared_mem_size(page_count);
    ptr = map_shared_mem(handle, size);
    if (ptr == nullptr)
        return nullptr;

    info = (shared_mem_t*)malloc(sizeof(shared_mem_t));
    info->handle = handle;
    info->ptr = ptr;
    info->size = size;
    return info;
}

//------------------------------------------------------------------------------
void close_shared_mem(shared_mem_t* info)
{
    if (info->ptr != nullptr)
    {
        UnmapViewOfFile(info->ptr);
    }

    if (info->handle != nullptr)
    {
        CloseHandle(info->handle);
    }

    free(info);
}
