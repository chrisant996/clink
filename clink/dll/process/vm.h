// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
struct region_info_t
{
    void*       base;
    size_t      size;
    unsigned    protect;
};

//------------------------------------------------------------------------------
extern void* g_current_proc;

//------------------------------------------------------------------------------
void*   get_alloc_base(void* addr);
void    get_region_info(void* addr, struct region_info_t* region_info);
void    set_region_write_state(struct region_info_t* region_info, int state);
int     write_vm(void* proc_handle, void* dest, const void* src, size_t size);
int     read_vm(void* proc_handle, void* dest, const void* src, size_t size);
