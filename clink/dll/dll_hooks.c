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
#include "dll_hooks.h"
#include "shared/util.h"
#include "shared/hook.h"
#include "shared/vm.h"
#include "shared/pe.h"

//------------------------------------------------------------------------------
static int              (*g_hook_trap)()        = NULL;
static unsigned char*   g_hook_trap_addr        = NULL;
static unsigned char    g_hook_trap_value       = 0;

//------------------------------------------------------------------------------
static int apply_hook_iat(void* self, const hook_decl_t* hook, int by_name)
{
    void* addr;

    addr = hook_iat(hook->base, hook->dll, hook->name_or_addr, hook->hook, by_name);
    if (addr == NULL)
    {
        LOG_INFO(
            "Unable to hook %s in IAT at base %p",
            hook->name_or_addr,
            hook->base
        );
        return 0;
    }

    // If the target's IAT was hooked then the hook destination is now
    // stored in 'addr'. We hook ourselves with this address to maintain
    // any IAT hooks that may already exist.
    if (hook_iat(self, NULL, hook->name_or_addr, addr, 1) == 0)
    {
        LOG_INFO("Failed to hook own IAT for %s", hook->name_or_addr);
        return 0;
    }

    return 1;
}

//------------------------------------------------------------------------------
static int apply_hook_jmp(void* self, const hook_decl_t* hook)
{
    void* addr;

    // Hook into a DLL's import by patching the start of the function. 'addr' is
    // the trampoline to call the original. This method doesn't use the IAT.

    addr = hook_jmp(hook->dll, hook->name_or_addr, hook->hook);
    if (addr == NULL)
    {
        LOG_INFO("Unable to hook %s in %s", hook->name_or_addr, hook->dll);
        return 0;
    }

    // Patch our own IAT with the address of a trampoline that the jmp-style
    // hook creates that calls the original function (i.e. a hook bypass).
    if (hook_iat(self, NULL, hook->name_or_addr, addr, 1) == 0)
    {
        LOG_INFO("Failed to hook own IAT for %s", hook->name_or_addr);
        return 0;
    }

    return 1;
}

//------------------------------------------------------------------------------
static LONG WINAPI hook_trap_veh(EXCEPTION_POINTERS* info)
{
    const EXCEPTION_RECORD* er;
    void** sp_reg;

    // Check exception record is the exception we've forced.
    er = info->ExceptionRecord;
    if (er->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (er->ExceptionAddress != g_hook_trap_addr)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

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
    LOG_INFO("VEH hit - caller is %p.", *sp_reg);

    // Apply hooks.
    if (g_hook_trap != NULL && !g_hook_trap())
    {
        LOG_INFO("Hook trap %p failed.", g_hook_trap);
    }

    return EXCEPTION_CONTINUE_EXECUTION;
}

//------------------------------------------------------------------------------
int apply_hooks(const hook_decl_t* hooks, int hook_count)
{
    const char* func_name;
    void* addr;
    void* self;
    int i;

    // Each hook needs fixing up, so we find the base address of our module.
    self = get_alloc_base(apply_hooks);
    if (self == NULL)
    {
        return 0;
    }

    for (i = 0; i < hook_count; ++i)
    {
        void* addr;
        const hook_decl_t* hook;

        hook = hooks + i;
        switch (hook->type)
        {
        case HOOK_TYPE_IAT_BY_NAME:
        case HOOK_TYPE_IAT_BY_ADDR:
            if (!apply_hook_iat(self, hook, hook->type))
            {
                return 0;
            }
            break;

        case HOOK_TYPE_JMP:
            if (!apply_hook_jmp(self, hook))
            {
                return 0;
            }
            break;
        }
    }

    return 1;
}

//------------------------------------------------------------------------------
int set_hook_trap(const char* dll, const char* func_name, int (*trap)())
{
    void* base;
    void* addr;
    unsigned char to_write;

    // If there's a debugger attached, we can't use VEH.
    if (IsDebuggerPresent())
    {
        return trap();
    }

    base = GetModuleHandle(dll);
    if (base == NULL)
    {
        LOG_INFO("Failed to find base for %s.", dll);
        return 0;
    }

    addr = get_export(base, func_name);
    if (addr == NULL)
    {
        LOG_INFO("Unable to resolve address for %s in %s", dll, func_name);
        return 0;
    }

    g_hook_trap = trap;
    g_hook_trap_addr = addr;
    g_hook_trap_value = *g_hook_trap_addr;

    AddVectoredExceptionHandler(1, hook_trap_veh);

    // Write a HALT instruction to force an exception.
    to_write = 0xf4;
    write_vm(g_current_proc, addr, &to_write, sizeof(to_write));

    return 1;
}

// vim: expandtab
