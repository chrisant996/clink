// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "vm.h"

//------------------------------------------------------------------------------
static uint32 g_alloc_granularity = 0;
static uint32 g_page_size         = 0;

//------------------------------------------------------------------------------
static void initialise_page_constants()
{
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    g_alloc_granularity = system_info.dwAllocationGranularity;
    g_page_size = system_info.dwPageSize;
}

//------------------------------------------------------------------------------
static uint32 to_access_flags(uint32 ms_flags)
{
    uint32 ret = 0;
    if (ms_flags & 0x22) ret |= vm::access_read;
    if (ms_flags & 0x44) ret |= vm::access_write|vm::access_read;
    if (ms_flags & 0x88) ret |= vm::access_cow|vm::access_write|vm::access_read;
    if (ms_flags & 0xf0) ret |= vm::access_execute;
    return ret;
}

//------------------------------------------------------------------------------
static uint32 to_ms_flags(uint32 access_flags)
{
    uint32 ret = PAGE_NOACCESS;
    if (access_flags & vm::access_cow)          ret = PAGE_WRITECOPY;
    else if (access_flags & vm::access_write)   ret = PAGE_READWRITE;
    else if (access_flags & vm::access_read)    ret = PAGE_READONLY;
    if (access_flags & vm::access_execute)      ret <<= 4;
    return ret;
}



//------------------------------------------------------------------------------
vm::vm(int32 pid)
{
    if (pid > 0)
        m_handle = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|
            PROCESS_VM_WRITE|PROCESS_VM_READ, FALSE, pid);
    else
        m_handle = GetCurrentProcess();
}

//------------------------------------------------------------------------------
vm::~vm()
{
    if (m_handle != nullptr)
        CloseHandle(m_handle);
}

//------------------------------------------------------------------------------
size_t vm::get_block_granularity()
{
    if (!g_alloc_granularity)
        initialise_page_constants();

    return g_alloc_granularity;
}

//------------------------------------------------------------------------------
size_t vm::get_page_size()
{
    if (!g_page_size)
        initialise_page_constants();

    return g_page_size;
}

//------------------------------------------------------------------------------
void* vm::get_alloc_base(void* address)
{
    if (m_handle == nullptr)
        return nullptr;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQueryEx(m_handle, address, &mbi, sizeof(mbi)))
        return mbi.AllocationBase;

    return nullptr;
}

//------------------------------------------------------------------------------
vm::region vm::get_region(void* address)
{
    if (m_handle == nullptr)
        return {};

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQueryEx(m_handle, address, &mbi, sizeof(mbi)))
        return {mbi.BaseAddress, unsigned(mbi.RegionSize / get_page_size())};

    return {};
}

//------------------------------------------------------------------------------
void* vm::get_page(void* address)
{
    return (void*)(uintptr_t(address) & ~(get_page_size() - 1));
}

//------------------------------------------------------------------------------
vm::region vm::alloc_region(uint32 page_count, uint32 access)
{
    if (m_handle == nullptr)
        return {};

    int32 ms_access = to_ms_flags(access);
    size_t size = page_count * get_page_size();
    if (void* base = VirtualAllocEx(m_handle, nullptr, size, MEM_COMMIT, ms_access))
        return {base, page_count};

    return {};
}

//------------------------------------------------------------------------------
void vm::free_region(const region& region)
{
    if (m_handle == nullptr)
        return;

    size_t size = region.page_count * get_page_size();
    VirtualFreeEx(m_handle, region.base, size, MEM_RELEASE);
}

//------------------------------------------------------------------------------
int32 vm::get_access(const region& region)
{
    if (m_handle == nullptr)
        return -1;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQueryEx(m_handle, region.base, &mbi, sizeof(mbi)))
        return to_access_flags(mbi.Protect);

    return -1;
}

//------------------------------------------------------------------------------
bool vm::set_access(const region& region, uint32 access)
{
    if (m_handle == nullptr)
        return false;

    DWORD ms_flags = to_ms_flags(access);
    size_t size = region.page_count * get_page_size();
    return !!VirtualProtectEx(m_handle, region.base, size, ms_flags, &ms_flags);
}

//------------------------------------------------------------------------------
bool vm::read(void* dest, const void* src, size_t size)
{
    if (m_handle == nullptr)
        return false;

    return (ReadProcessMemory(m_handle, src, dest, size, nullptr) != FALSE);
}

//------------------------------------------------------------------------------
bool vm::write(void* dest, const void* src, size_t size)
{
    if (m_handle == nullptr)
        return false;

    return (WriteProcessMemory(m_handle, dest, src, size, nullptr) != FALSE);
}

//------------------------------------------------------------------------------
void vm::flush_icache(const region& region)
{
    if (m_handle == nullptr)
        return;

    size_t size = region.page_count * get_page_size();
    FlushInstructionCache(m_handle, region.base, size);
}
