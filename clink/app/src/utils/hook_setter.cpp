// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "hook_setter.h"

#include <core/base.h>
#include <core/log.h>
#include <process/hook.h>
#include <process/vm.h>
#include <detours.h>

//------------------------------------------------------------------------------
hook_setter::hook_setter()
{
    LONG err = NOERROR;

    // In order to repair our IAT, we need the base address of our module.
    if (!err)
    {
        m_self = vm().get_alloc_base((void*)"clink");
        if (m_self == nullptr)
            err = GetLastError();
    }

    // Start a detour transaction.
    if (!err)
        err = DetourTransactionBegin();

    if (err)
    {
        LOG("Unable to start hook transaction (error %u).", err);
        return;
    }

    LOG(">>> Started hook transaction.");
    m_pending = true;
}

//------------------------------------------------------------------------------
hook_setter::~hook_setter()
{
    if (m_pending)
    {
        LOG("<<< Hook transaction aborted.");
        DetourTransactionAbort();
    }

    free_repair_iat_list(m_repair_iat);
}

//------------------------------------------------------------------------------
bool hook_setter::commit()
{
    m_pending = false;

    // TODO: suspend threads?  Currently this relies on CMD being essentially
    // single threaded.

    LONG err = DetourTransactionCommit();
    if (!err)
    {
        apply_repair_iat_list(m_repair_iat);
    }
    else
    {
        LOG("<<< Unable to commit hooks (error %u).", err);
        free_repair_iat_list(m_repair_iat);
        m_desc_count = 0;
        return false;
    }

    // Apply any IAT hooks.
    int failed = 0;
    for (int i = 0; i < m_desc_count; ++i)
    {
        const hook_desc& desc = m_descs[i];
        if (desc.type == iat)
            failed += !commit_iat(m_self, desc);
    }
    m_desc_count = 0;

    if (failed)
    {
        LOG("<<< Unable to commit hooks.");
        return false;
    }

    LOG("<<< Hook transaction committed.");

    // TODO: resume threads?  Currently this relies on CMD being essentially
    // single threaded.

    return true;
}

//------------------------------------------------------------------------------
bool hook_setter::add_internal(hook_type type, const char* module, const char* name, hookptr_t hook)
{
    assert(m_desc_count < sizeof_array(m_descs));
    if (m_desc_count >= sizeof_array(m_descs))
    {
        LOG("Too many hooks in transaction.");
        assert(false);
        return false;
    }

    if (type == iat)
        return add_iat(module, name, hookptr_t(hook));
    else if (type == detour)
        return add_detour(module, name, hookptr_t(hook));
    else
        return false;
}

//------------------------------------------------------------------------------
bool hook_setter::add_iat(const char* module, const char* name, hookptr_t hook)
{
    void* base = GetModuleHandleA(module);
    if (!base)
    {
        LOG("Module '%s' is not loaded.", module ? module : "(null)");
        assert(false);
        return false;
    }

    hook_desc& desc = m_descs[m_desc_count++];
    desc.type = iat;
    desc.replace = nullptr;
    desc.base = base;
    desc.hook = hook;
    desc.name = name;
    return true;
}

//------------------------------------------------------------------------------
void* follow_jump(void* addr);
bool hook_setter::add_detour(const char* module, const char* name, hookptr_t hook)
{
    LOG("Attempting to hook %s in %s with %p.", name, module, hook);
    HMODULE hModule = GetModuleHandleA(module);
    if (!hModule)
    {
        LOG("Unable to load %s.", module);
        return false;
    }

    PBYTE pbCode = (PBYTE)GetProcAddress(hModule, name);
    if (!pbCode)
    {
        LOG("Unable to find %s in %s.", name, module);
        return false;
    }

    hook_desc& desc = m_descs[m_desc_count++];
    desc.type = detour;
    desc.replace = nullptr;
    desc.base = (void*)hModule;
    desc.hook = hook;
    desc.name = name;

    // Get the target pointer to hook.
    desc.replace = follow_jump(pbCode);
    if (!desc.replace)
    {
        LOG("Unable to get target address.");
        m_desc_count--;
        return false;
    }

    // Hook the target pointer.
    PDETOUR_TRAMPOLINE trampoline;
    LONG err = DetourAttachEx(&desc.replace, (PVOID)hook, &trampoline, nullptr, nullptr);
    if (err != NOERROR)
    {
        LOG("Unable to hook %s (error %u).", name, err);
        m_desc_count--;
        return false;
    }

    // Hook our IAT back to the original.
    add_repair_iat_node(m_repair_iat, m_self, module, name, hookptr_t(trampoline));

    return true;
}

//------------------------------------------------------------------------------
bool hook_setter::commit_iat(void* self, const hook_desc& desc)
{
    if (desc.type != iat)
        return false;

    hookptr_t addr = hook_iat(desc.base, nullptr, desc.name, desc.hook, 1);
    if (addr == nullptr)
        return false;

    // If the target's IAT was hooked then the hook destination is now
    // stored in 'addr'. We hook ourselves with this address to maintain
    // any IAT hooks that may already exist.
    if (hook_iat(self, nullptr, desc.name, addr, 1) == 0)
    {
        LOG("Failed to hook own IAT for %s", desc.name);
        return false;
    }

    return true;
}
