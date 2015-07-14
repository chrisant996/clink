// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "vm.h"

//------------------------------------------------------------------------------
void* g_current_proc = (void*)-1;

//------------------------------------------------------------------------------
void* get_alloc_base(void* addr)
{
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(addr, &mbi, sizeof(mbi));
    return mbi.AllocationBase;
}

//------------------------------------------------------------------------------
void get_region_info(void* addr, struct region_info_t* region_info)
{
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(addr, &mbi, sizeof(mbi));

    region_info->base = mbi.BaseAddress;
    region_info->size = mbi.RegionSize;
    region_info->protect = mbi.Protect;
}

//------------------------------------------------------------------------------
void set_region_write_state(struct region_info_t* region_info, int state)
{
    DWORD unused;
    VirtualProtect(
        region_info->base,
        region_info->size,
        (state ? PAGE_EXECUTE_READWRITE : region_info->protect),
        &unused
    );
}

//------------------------------------------------------------------------------
int write_vm(void* proc_handle, void* dest, const void* src, size_t size)
{
    BOOL ok;
    ok = WriteProcessMemory((HANDLE)proc_handle, dest, src, size, nullptr);
    return (ok != FALSE);
}

//------------------------------------------------------------------------------
int read_vm(void* proc_handle, void* dest, const void* src, size_t size)
{
    BOOL ok;
    ok = ReadProcessMemory((HANDLE)proc_handle, src, dest, size, nullptr);
    return (ok != FALSE);
}
