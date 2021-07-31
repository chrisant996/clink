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
        const hook_iat_desc& desc = m_descs[i];
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
bool hook_setter::add_desc(const char* module, const char* name, hookptr_t hook)
{
    assert(m_desc_count < sizeof_array(m_descs));
    if (m_desc_count >= sizeof_array(m_descs))
    {
        assert(false);
        return false;
    }

    void* base = GetModuleHandleA(module);

    hook_iat_desc& desc = m_descs[m_desc_count++];
    desc.base = base;
    desc.hook = hook;
    desc.name = name;
    return true;
}

//------------------------------------------------------------------------------
void* follow_jump(void* addr);
bool hook_setter::add_detour(const char* module, const char* name, hookptr_t detour)
{
    LOG("Attempting to hook %s in %s with %p.", name, module, detour);
    PVOID proc = DetourFindFunction(module, name);
    if (!proc)
    {
        LOG("Unable to find %s in %s.", name, module);
        return false;
    }

    // Get the target pointer to hook.
    PVOID replace = follow_jump(proc);
    if (!replace)
    {
        LOG("Unable to get target address.");
        return false;
    }

    // Hook the target pointer.
    PDETOUR_TRAMPOLINE trampoline;
    LONG err = DetourAttachEx(&replace, (PVOID)detour, &trampoline, nullptr, nullptr);
    if (err != NOERROR)
    {
        LOG("Unable to hook %s (error %u).", name, err);
        return false;
    }

    // Hook our IAT back to the original.
    add_repair_iat_node(m_repair_iat, m_self, module, name, hookptr_t(trampoline));

    return true;
}

//------------------------------------------------------------------------------
bool hook_setter::commit_iat(void* self, const hook_iat_desc& desc)
{
    hookptr_t addr = hook_iat(desc.base, nullptr, desc.name, desc.hook, 1);
    if (addr == nullptr)
    {
        LOG("Unable to hook %s in IAT at base %p", desc.name, desc.base);
        return false;
    }

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
