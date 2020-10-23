// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "hook_setter.h"

#include <core/base.h>
#include <core/log.h>
#include <process/hook.h>
#include <process/pe.h>
#include <process/vm.h>

//------------------------------------------------------------------------------
hook_setter::hook_setter()
: m_desc_count(0)
{
}

//------------------------------------------------------------------------------
int hook_setter::commit()
{
    // Each hook needs fixing up, so we find the base address of our module.
    void* self = vm().get_alloc_base("clink");
    if (self == nullptr)
        return 0;

    // Apply all the hooks add to the setter.
    int success = 0;
    for (int i = 0; i < m_desc_count; ++i)
    {
        const hook_desc& desc = m_descs[i];
        switch (desc.type)
        {
        case hook_type_iat_by_name: success += !!commit_iat(self, desc);  break;
        case hook_type_jmp:         success += !!commit_jmp(self, desc);  break;
        }
    }

    return success;
}

//------------------------------------------------------------------------------
hook_setter::hook_desc* hook_setter::add_desc(
    hook_type type,
    void* module,
    const char* name,
    hookptr_t hook)
{
    if (m_desc_count >= sizeof_array(m_descs))
        return nullptr;

    hook_desc& desc = m_descs[m_desc_count];
    desc.type = type;
    desc.module = module;
    desc.hook = hook;
    desc.name = name;

    ++m_desc_count;
    return &desc;
}

//------------------------------------------------------------------------------
bool hook_setter::commit_iat(void* self, const hook_desc& desc)
{
    hookptr_t addr = hook_iat(desc.module, nullptr, desc.name, desc.hook, 1);
    if (addr == nullptr)
    {
        LOG("Unable to hook %s in IAT at base %p", desc.name, desc.module);
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

//------------------------------------------------------------------------------
bool hook_setter::commit_jmp(void* self, const hook_desc& desc)
{
    // Hook into a DLL's import by patching the start of the function. 'addr' is
    // the trampoline that can be used to call the original. This method doesn't
    // use the IAT.

    auto* addr = hook_jmp(desc.module, desc.name, desc.hook);
    if (addr == nullptr)
    {
        LOG("Unable to hook %s in %p", desc.name, desc.module);
        return false;
    }

    // Patch our own IAT with the address of a trampoline so out use of this
    // function calls the original.
    if (hook_iat(self, nullptr, desc.name, addr, 1) == 0)
    {
        LOG("Failed to hook own IAT for %s", desc.name);
        return false;
    }

    return true;
}
