// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "hook_setter.h"

#include <core/base.h>
#include <core/log.h>
#include <process/vm.h>
#include <detours.h>

//------------------------------------------------------------------------------
static void* follow_jump(void* addr)
{
    unsigned char* t = (unsigned char*)addr;

    // Check the opcode.
    if ((t[0] & 0xf0) == 0x40) // REX prefix.
        ++t;

    if (t[0] != 0xff)
        return addr;

    // Check the opcode extension from the modr/m byte.
    if ((t[1] & 070) != 040)
        return addr;

    int* imm = (int*)(t + 2);

    void* dest = addr;
    switch (t[1] & 007)
    {
    case 5:
#if defined(_M_X64)
        // dest = [rip + disp32]
        dest = *(void**)(t + 6 + *imm);
#elif defined(_M_IX86)
        // dest = disp32
        dest = (void*)(intptr_t)(*imm);
#else
#error Processor not supported.
#endif
    }

    LOG("Following jump to %p", dest);
    return dest;
}

//------------------------------------------------------------------------------
hook_setter::hook_setter()
{
    LONG err = NOERROR;

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
}

//------------------------------------------------------------------------------
bool hook_setter::commit()
{
    m_pending = false;
    m_desc_count = 0;

    // TODO: suspend threads?  Currently this relies on CMD being essentially
    // single threaded.

    LONG err = DetourTransactionCommit();
    if (err)
    {
        LOG("<<< Unable to commit hooks (error %u).", err);
        return false;
    }

    LOG("<<< Hook transaction committed.");

    // TODO: resume threads?  Currently this relies on CMD being essentially
    // single threaded.

    return true;
}

//------------------------------------------------------------------------------
bool hook_setter::attach_internal(const char* module, const char* name, hookptr_t hook, hookptrptr_t original)
{
    assert(m_desc_count < sizeof_array(m_descs));
    if (m_desc_count >= sizeof_array(m_descs))
    {
        LOG("Too many hooks in transaction.");
        assert(false);
        return false;
    }

    return attach_detour(module, name, hook, original);
}

//------------------------------------------------------------------------------
bool hook_setter::detach_internal(hookptrptr_t original, hookptr_t hook)
{
    assert(m_desc_count < sizeof_array(m_descs));
    if (m_desc_count >= sizeof_array(m_descs))
    {
        LOG("Too many hooks in transaction.");
        assert(false);
        return false;
    }

    return detach_detour(original, hook);
}

//------------------------------------------------------------------------------
bool hook_setter::attach_detour(const char* module, const char* name, hookptr_t hook, hookptrptr_t original)
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

    // Get the target pointer to hook.
    void* replace = follow_jump(pbCode);
    if (!replace)
    {
        LOG("Unable to get target address.");
        return false;
    }

    hook_desc& desc = m_descs[m_desc_count++];
    desc.replace = replace;

    // Hook the target pointer.
    PDETOUR_TRAMPOLINE trampoline;
    LONG err = DetourAttachEx(&replace, (PVOID)hook, &trampoline, nullptr, nullptr);
    if (err != NOERROR)
    {
        LOG("Unable to hook %s (error %u).", name, err);
        m_desc_count--;
        return false;
    }

    // Return the trampoline in original.
    if (original)
        *original = hookptr_t(trampoline);

    return true;
}

//------------------------------------------------------------------------------
bool hook_setter::detach_detour(hookptrptr_t original, hookptr_t hook)
{
    LOG("Attempting to unhook %p.", hook);

    // Unhook the target pointer.
    LONG err = DetourDetach((PVOID*)original, (PVOID)hook);
    if (err != NOERROR)
    {
        LOG("Unable to unhook %p (error %u).", hook, err);
        return false;
    }

    return true;
}
