// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "vm.h"

#include <Windows.h>

//------------------------------------------------------------------------------
vm_access::vm_access(int pid)
: m_handle(nullptr)
{
    if (pid > 0)
        m_handle = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|
            PROCESS_VM_WRITE|PROCESS_VM_READ, FALSE, pid);
    else
        m_handle = GetCurrentProcess();
}

//------------------------------------------------------------------------------
vm_access::~vm_access()
{
    if (m_handle != nullptr)
        CloseHandle(m_handle);
}

//------------------------------------------------------------------------------
void* vm_access::alloc(size_t size)
{
    if (m_handle == nullptr)
        return nullptr;

    return VirtualAllocEx(m_handle, nullptr, size, MEM_COMMIT, PAGE_READWRITE);
}

//------------------------------------------------------------------------------
bool vm_access::free(void* address)
{
    if (m_handle == nullptr)
        return false;

    return (VirtualFreeEx(m_handle, address, 0, MEM_RELEASE) != FALSE);
}

//------------------------------------------------------------------------------
bool vm_access::write(void* dest, const void* src, size_t size)
{
    if (m_handle == nullptr)
        return false;

    return (WriteProcessMemory(m_handle, dest, src, size, nullptr) != FALSE);
}

//------------------------------------------------------------------------------
bool vm_access::read(void* dest, const void* src, size_t size)
{
    if (m_handle == nullptr)
        return false;

    return (ReadProcessMemory(m_handle, src, dest, size, nullptr) != FALSE);
}



//------------------------------------------------------------------------------
vm_region::vm_region(void* address)
: m_access(0)
, m_modified(false)
{
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(address, &mbi, sizeof(mbi));

    m_parent_base = mbi.AllocationBase;
    m_base = mbi.BaseAddress;
    m_size = mbi.RegionSize;

    if (mbi.Protect & 0x22)
        m_access |= readable;

    if (mbi.Protect & 0x44)
        m_access |= writeable|readable;

    if (mbi.Protect & 0x88)
        m_access |= copyonwrite|writeable|readable;

    if (mbi.Protect & 0xf0)
        m_access |= executable;
}

//------------------------------------------------------------------------------
vm_region::~vm_region()
{
    if (m_modified)
        set_access(m_access);
}

//------------------------------------------------------------------------------
void vm_region::set_access(int flags, bool permanent)
{
    DWORD vp_flags = PAGE_NOACCESS;
    if (flags & copyonwrite)
        vp_flags = PAGE_WRITECOPY;
    else if (flags & writeable)
        vp_flags = PAGE_READWRITE;
    else if (flags & readable)
        vp_flags = PAGE_READONLY;

    if (flags & executable)
        vp_flags <<= 4;

    VirtualProtect(m_base, m_size, vp_flags, &vp_flags);

    m_modified = !permanent;
}
