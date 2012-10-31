/* Copyright (c) 2012 Martin Ridgers
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
#include "vm.h"
#include "util.h"

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
    ok = WriteProcessMemory((HANDLE)proc_handle, dest, src, size, NULL);
    return (ok != FALSE);
}

//------------------------------------------------------------------------------
int read_vm(void* proc_handle, void* dest, const void* src, size_t size)
{
    BOOL ok;
    ok = ReadProcessMemory((HANDLE)proc_handle, src, dest, size, NULL);
    return (ok != FALSE);
}

// vim: expandtab
