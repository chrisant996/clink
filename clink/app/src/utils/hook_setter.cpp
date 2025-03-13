// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "hook_setter.h"

#include <core/base.h>
#include <core/log.h>
#include <core/os.h>
#include <process/pe.h>
#include <process/vm.h>
#include <detours.h>

#if 0
// For future reference:  This is the signature for import library entries,
// which are normally pointed to by the IMAGE_IMPORT_DESCRIPTOR entries.
// Directly accessing these is an alternative to IAT hooking for our own image,
// but doesn't help with IAT hooking in other images in the process.
extern "C" void* __imp_ReadConsoleW;
#endif

//------------------------------------------------------------------------------
static bool use_verbose_hook_logging()
{
    str<16> tmp;
    os::get_env("CLINK_VERBOSE_HOOK_LOGGING", tmp);
    return atoi(tmp.c_str()) > 0;
}

//------------------------------------------------------------------------------
static void* follow_jump(void* addr)
{
    uint8* t = (uint8*)addr;

    // Check the opcode.
    if ((t[0] & 0xf0) == 0x40) // REX prefix.
        ++t;

    if (t[0] != 0xff)
        return addr;

    // Check the opcode extension from the modr/m byte.
    if ((t[1] & 070) != 040)
        return addr;

    int32* imm = (int32*)(t + 2);

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
#elif defined(_M_ARM64)
        // FIXME: just a copy / paste from above
        dest = (void*)(intptr_t)(*imm);
#else
#error Processor not supported.
#endif
    }

    LOG("Following jump to %p", dest);
    return dest;
}

//------------------------------------------------------------------------------
static void write_addr(hookptr_t* where, hookptr_t to_write)
{
    vm vm;
    vm::region region = { vm.get_page(where), 1 };
    uint32 prev_access = vm.get_access(region);
    if (!vm.set_access(region, vm::access_write))
        LOG("VM set write access to %p failed (err = %d)", where, GetLastError());

    if (!vm.write(where, &to_write, sizeof(to_write)))
        LOG("VM write to %p failed (err = %d)", where, GetLastError());

    vm.set_access(region, prev_access);
}

//------------------------------------------------------------------------------
static hookptr_t get_proc_addr(const char* dll, const char* func_name)
{
    if (void* base = LoadLibraryA(dll))
        return (hookptr_t)pe_info(base).get_export(func_name);

    LOG("Failed to load library '%s'", dll);
    return nullptr;
}

//------------------------------------------------------------------------------
bool find_iat(
    void* base,
    const char* dll,
    const char* func_name,
    bool find_by_name,
    hookptrptr_t* import_out,
    hookptr_t* original_out,
    str_base* found_in_module
)
{
    hookptrptr_t import;

    // Find entry and replace it.
    pe_info pe(base);
    str<> table_name;
    if (find_by_name)
    {
        import = (hookptrptr_t)pe.get_import_by_name(nullptr, func_name, found_in_module);
    }
    else
    {
        // Get the address of the function we're going to hook.
        hookptr_t func_addr = get_proc_addr(dll, func_name);
        if (func_addr == nullptr)
        {
            LOG("Failed to find %s in '%s'.", func_name, dll);
            return false;
        }

        LOG("Looking up import by address %p in '%s'.", func_addr, dll);
        import = (hookptrptr_t)pe.get_import_by_addr(nullptr, (pe_info::funcptr_t)func_addr, found_in_module);
    }

    if (import == nullptr)
    {
        LOG("Unable to find import in IAT.");
        return false;
    }

    LOG("Found import at %p (value is %p).", import, *import);

    if (import_out)
        *import_out = import;
    if (original_out)
        *original_out = *import;

    return true;
}

//------------------------------------------------------------------------------
void hook_iat(hookptrptr_t import, hookptr_t hook)
{
    write_addr(import, hook);

    vm().flush_icache();
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
    const int32 count = m_desc_count;

    m_pending = false;
    m_desc_count = 0;

    // REVIEW: suspend threads?  Currently this relies on CMD being essentially
    // single threaded.

    // Apply and Detours hooks.
    LONG err = DetourTransactionCommit();
    if (err)
    {
nope:
        LOG("<<< Unable to commit hooks (error %u).", err);
        return false;
    }

    // Apply any IAT hooks.
    int32 failed = 0;
    for (int32 i = 0; i < count; ++i)
    {
        const hook_desc& desc = m_descs[i];
        if (desc.type == iat && !commit_iat(desc))
        {
            if (desc.required)
            {
                err = GetLastError();
                failed++;
            }
        }
    }
    if (failed)
        goto nope;

    LOG("<<< Hook transaction committed.");

    // REVIEW: resume threads?  Currently this relies on CMD being essentially
    // single threaded.

    return true;
}

//------------------------------------------------------------------------------
bool hook_setter::attach_internal(hook_type type, const char* module, const char* name, hookptr_t hook, hookptrptr_t original, bool required)
{
    assert(m_desc_count < sizeof_array(m_descs));
    if (m_desc_count >= sizeof_array(m_descs))
    {
        LOG("Too many hooks in transaction.");
        assert(false);
        return false;
    }

    assertimplies(!required, type == iat);

    if (type == iat)
        return attach_iat(module, name, hook, original, required);
    else if (type == detour)
        return attach_detour(module, name, hook, original);
    else
        return false;
}

//------------------------------------------------------------------------------
bool hook_setter::detach_internal(hook_type type, const char* module, const char* name, hookptrptr_t original, hookptr_t hook)
{
    assert(m_desc_count < sizeof_array(m_descs));
    if (m_desc_count >= sizeof_array(m_descs))
    {
        LOG("Too many hooks in transaction.");
        assert(false);
        return false;
    }

    if (type == iat)
        return detach_iat(module, name, original, hook);
    else if (type == detour)
        return detach_detour(original, hook);
    else
        return false;
}

//------------------------------------------------------------------------------
bool hook_setter::attach_detour(const char* module, const char* name, hookptr_t hook, hookptrptr_t original)
{
    LOG("Attempting to detour %s in %s with %p.", name, module, hook);
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
    desc.type = detour;
    desc.replace = replace;
    desc.base = nullptr;
    desc.module = module;
    desc.name = name;
    desc.hook = hook;
    desc.required = true;

    // Hook the target pointer.  For Detours desc.replace is a pointer to the
    // function to hook.
    PDETOUR_TRAMPOLINE trampoline;
    LONG err = DetourAttachEx(&desc.replace, (PVOID)hook, &trampoline, nullptr, nullptr);
    if (err != NOERROR)
    {
        LOG("Unable to detour %s (error %u).", name, err);
        m_desc_count--;
        return false;
    }

    // Return the trampoline in original.
    if (original)
        *original = hookptr_t(trampoline);

    return true;
}

//------------------------------------------------------------------------------
bool hook_setter::attach_iat(const char* module, const char* name, hookptr_t hook, hookptrptr_t original, bool required)
{
    void* base = GetModuleHandleA(module);
    if (!base)
    {
        LOG("Module '%s' is not loaded.", module ? module : "(null)");
        assert(false);
        return false;
    }

    hookptrptr_t replace;
    str<> found_in_module;
    {
        logging_group lg(use_verbose_hook_logging());
        LOG("Attempting to hook %s in IAT for module %p.", name, base);

        if (!find_iat(base, module, name, true/*find_by_name*/, &replace, original, &found_in_module))
            return false;

        lg.discard();

        if (!lg.is_verbose())
            LOG("Hooking %s in IAT for module %p in '%s' at %p (value was %p).", name, base, found_in_module.c_str(), replace, *original);
    }

    hook_desc& desc = m_descs[m_desc_count++];
    desc.type = iat;
    desc.replace = replace;
    desc.base = base;
    desc.module = module;
    desc.name = name;
    desc.hook = hook;
    desc.required = required;
    return true;
}

//------------------------------------------------------------------------------
bool hook_setter::detach_detour(hookptrptr_t original, hookptr_t hook)
{
    LOG("Attempting to detach detour %p.", hook);

    // Unhook the target pointer.
    LONG err = DetourDetach((PVOID*)original, (PVOID)hook);
    if (err != NOERROR)
    {
        LOG("Unable to detach detour %p (error %u).", hook, err);
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
bool hook_setter::detach_iat(const char* module, const char* name, hookptrptr_t original, hookptr_t hook)
{
    void* base = GetModuleHandleA(module);
    if (!base)
    {
        LOG("Module '%s' is not loaded.", module ? module : "(null)");
        assert(false);
        return false;
    }

    hookptrptr_t replace;
    str<> found_in_module;
    {
        logging_group lg(use_verbose_hook_logging());
        LOG("Attempting to unhook %p from %s in IAT for module %p.", hook, name, base);

        hookptr_t was;
        if (!find_iat(base, module, name, true/*find_by_name*/, &replace, &was, &found_in_module))
            return false;

        if (*was != hook)
        {
            LOG("Unable to unhook %p; the IAT has %p instead.", hook, was);
            return false;
        }

        lg.discard();

        if (!lg.is_verbose())
            LOG("Unhooking %s in IAT for module %p in '%s' at %p.", name, base, found_in_module.c_str(), *replace);
    }

    hook_desc& desc = m_descs[m_desc_count++];
    desc.type = iat;
    desc.replace = replace;
    desc.base = base;
    desc.module = module;
    desc.name = name;
    desc.hook = *original;
    return true;
}

//------------------------------------------------------------------------------
bool hook_setter::commit_iat(const hook_desc& desc)
{
    if (desc.type != iat)
        return false;

    // For IAT desc.replace is a pointer to the import pointer to replace.
    hook_iat(hookptrptr_t(desc.replace), desc.hook);

#if 0
    // If the target's IAT was hooked then the hook destination is now
    // stored in 'addr'. We hook ourselves with this address to maintain
    // any IAT hooks that may already exist.
    if (hook_iat(m_self, nullptr, desc.name, addr, true/*find_by_name*/) == 0)
    {
        LOG("Failed to hook own IAT for %s", desc.name);
        return false;
    }
#endif

    return true;
}
