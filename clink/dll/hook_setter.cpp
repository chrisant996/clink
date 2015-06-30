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
#include "hook_setter.h"
#include "process/hook.h"
#include "process/pe.h"
#include "process/vm.h"

#include <core/base.h>
#include <core/log.h>

//------------------------------------------------------------------------------
static void             dummy() {}
static bool             (*g_hook_trap)()        = nullptr;
static void*            g_hook_trap_addr        = nullptr;
static unsigned char    g_hook_trap_value       = 0;



//------------------------------------------------------------------------------
static LONG WINAPI hook_trap_veh(EXCEPTION_POINTERS* info)
{
    const EXCEPTION_RECORD* er;
    void** sp_reg;

    // Check exception record is the exception we've forced.
    er = info->ExceptionRecord;
    if (er->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;

    if (er->ExceptionAddress != g_hook_trap_addr)
        return EXCEPTION_CONTINUE_SEARCH;

    // Restore original instruction.
    write_vm(
        g_current_proc,
        g_hook_trap_addr,
        &g_hook_trap_value,
        sizeof(g_hook_trap_value)
    );

    // Who called us?
#if defined(_M_IX86)
    sp_reg = (void**)info->ContextRecord->Esp;
#elif defined(_M_X64)
    sp_reg = (void**)info->ContextRecord->Rsp;
#endif
    LOG("VEH hit - caller is %p.", *sp_reg);

    // Apply hooks.
    if (g_hook_trap != nullptr && !g_hook_trap())
        LOG("Hook trap %p failed.", g_hook_trap);

    return EXCEPTION_CONTINUE_EXECUTION;
}

//------------------------------------------------------------------------------
bool set_hook_trap(void* module, const char* func_name, bool (*trap)())
{
    // If there's a debugger attached, we can't use VEH.
    if (IsDebuggerPresent())
        return trap();

    void* addr = get_export(module, func_name);
    if (addr == nullptr)
    {
        char dll[96] = {};
        GetModuleFileName(HMODULE(module), dll, sizeof_array(dll));
        LOG("Unable to resolve address for %s in %s", dll, func_name);
        return false;
    }

    g_hook_trap = trap;
    g_hook_trap_addr = addr;
    g_hook_trap_value = *(unsigned char*)g_hook_trap_addr;

    AddVectoredExceptionHandler(1, hook_trap_veh);

    // Write a HALT instruction to force an exception.
    unsigned char to_write = 0xf4;
    write_vm(g_current_proc, addr, &to_write, sizeof(to_write));

    return true;
}



//------------------------------------------------------------------------------
hook_setter::hook_setter()
: m_desc_count(0)
{
}

//------------------------------------------------------------------------------
bool hook_setter::add_iat(void* module, const char* name, void* hook)
{
    return (add_desc(HOOK_TYPE_IAT_BY_NAME, module, name, hook) != nullptr);
}

//------------------------------------------------------------------------------
bool hook_setter::add_jmp(void* module, const char* name, void* hook)
{
    return (add_desc(HOOK_TYPE_JMP, module, name, hook) != nullptr);
}

//------------------------------------------------------------------------------
bool hook_setter::add_trap(void* module, const char* name, bool (*trap)())
{
    return (add_desc(HOOK_TYPE_TRAP, module, name, trap) != nullptr);
}

//------------------------------------------------------------------------------
int hook_setter::commit()
{
    // Each hook needs fixing up, so we find the base address of our module.
    void* self = get_alloc_base(dummy);
    if (self == nullptr)
        return 0;

    // Apply all the hooks add to the setter.
    int success = 0;
    for (int i = 0; i < m_desc_count; ++i)
    {
        const hook_desc& desc = m_descs[i];
        switch (desc.type)
        {
        case HOOK_TYPE_IAT_BY_NAME: success += !!commit_iat(self, desc);  break;
        case HOOK_TYPE_JMP:         success += !!commit_jmp(self, desc);  break;
        case HOOK_TYPE_TRAP:        success += !!commit_trap(self, desc); break;
        }
    }

    return success;
}

//------------------------------------------------------------------------------
hook_setter::hook_desc* hook_setter::add_desc(
    hook_type type,
    void* module,
    const char* name,
    void* hook)
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
bool
hook_setter::commit_iat(void* self, const hook_desc& desc)
{
    void* addr = hook_iat(desc.module, nullptr, desc.name, desc.hook, 1);
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
bool
hook_setter::commit_jmp(void* self, const hook_desc& desc)
{
    // Hook into a DLL's import by patching the start of the function. 'addr' is
    // the trampoline that can be used to call the original. This method doesn't
    // use the IAT.

    void* addr = hook_jmp(desc.module, desc.name, desc.hook);
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

//------------------------------------------------------------------------------
bool
hook_setter::commit_trap(void* self, const hook_desc& desc)
{
    return set_hook_trap(desc.module, desc.name, (bool (*)())(desc.hook));
}
