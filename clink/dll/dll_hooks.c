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
#include "shared/util.h"
#include "shared/vm.h"
#include "shared/pe.h"

//------------------------------------------------------------------------------
void*                   hook_iat(void*, const char*, const char*, void*, int);
void*                   hook_jmp(const char*, const char*, void*);
static unsigned char*   g_hook_trap_addr        = NULL;
static unsigned char    g_hook_trap_value       = 0;
static void*            g_prev_seh_func         = NULL;
BOOL WINAPI             hooked_read_console(HANDLE, wchar_t*, DWORD, LPDWORD,
                                            PCONSOLE_READCONSOLE_CONTROL);
BOOL WINAPI             hooked_write_console(HANDLE, const wchar_t*, DWORD,
                                             LPDWORD, void*);

//------------------------------------------------------------------------------
static const char* get_kernel_dll()
{
    // We're going to use a different DLL for Win8 (and onwards).

    OSVERSIONINFOEX osvi;
    DWORDLONG mask = 0;
    int op=VER_GREATER_EQUAL;

    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    osvi.dwMajorVersion = 6;
    osvi.dwMinorVersion = 2;

    VER_SET_CONDITION(mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(mask, VER_MINORVERSION, VER_GREATER_EQUAL);

    if (VerifyVersionInfo(&osvi, VER_MAJORVERSION|VER_MINORVERSION, mask))
    {
        return "kernelbase.dll";
    }
   
    return "kernel32.dll";
}

//------------------------------------------------------------------------------
static int apply_hooks()
{
    const char* func_name;
    void* addr;
    void* self;
    void* base;

    base = GetModuleHandle("cmd.exe");
    if (base == NULL)
    {
        LOG_INFO("Invalid process base address; %p.", base);
    }

    self = get_alloc_base(apply_hooks);
    if (self == NULL)
    {
        return 0;
    }

    // Write hook
    func_name = "WriteConsoleW";
    addr = hook_iat(base, NULL, func_name, hooked_write_console, 1);
    if (addr == NULL)
    {
        LOG_INFO("Unable to hook %s in IAT at base %p", func_name, base);
        return 0;
    }

    // If the target's IAT was hooked then the hook destination is now stored in
    // 'addr'. We hook ourselves with this address to maintain the hook.
    if (hook_iat(self, NULL, func_name, addr, 1) == 0)
    {
        LOG_INFO("Failed to hook own IAT for %s", func_name);
        return 0;
    }

    // Read hook - So as to not disturb another utility's hooks that maybe in 
    // place we use an alternative hooking method that doesn't involve patching
    // the target's IAT.
    func_name = "ReadConsoleW";
    addr = hook_jmp(get_kernel_dll(), func_name, hooked_read_console);
    if (addr == NULL)
    {
        LOG_INFO("Unable to hook %s in %s", func_name, get_kernel_dll());
        return 0;
    }

    if (hook_iat(self, NULL, func_name, addr, 1) == 0)
    {
        LOG_INFO("Failed to hook own IAT for %s", func_name);
        return 0;
    }

    return 1;
}

//------------------------------------------------------------------------------
static LONG WINAPI hook_trap(EXCEPTION_POINTERS* info)
{
    const EXCEPTION_RECORD* er;
    void* base;
    void** sp_reg;

    // Check exception record is the exception we've forced.
    er = info->ExceptionRecord;
    if (er->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION)
    {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    if (er->ExceptionAddress != g_hook_trap_addr)
    {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // Restore original instruction.
    write_vm(
        g_current_proc,
        g_hook_trap_addr,
        &g_hook_trap_value,
        sizeof(g_hook_trap_value)
    );

    // Who called us?
#ifdef _M_IX86
    sp_reg = (void**)info->ContextRecord->Esp; 
#elif defined(_M_X64)
    sp_reg = (void**)info->ContextRecord->Rsp; 
#endif
    LOG_INFO("SEH hit - caller is %p.", *sp_reg);

    // Apply hooks.
    if (apply_hooks())
    {
        LOG_INFO("Success!");
    }

    // Tidy up. There's no need to flush the instruction cache as making the
    // hooks would have done it multiple times.
    SetUnhandledExceptionFilter(g_prev_seh_func);
    return EXCEPTION_CONTINUE_EXECUTION;
}

//------------------------------------------------------------------------------
int set_hook_trap()
{
    void* base;
    void* addr;
    unsigned char to_write;

    // If there's a debugger attached, we can't use SEH.
    if (IsDebuggerPresent())
    {
        return apply_hooks();
    }

    base = GetModuleHandle(get_kernel_dll());
    if (base == NULL)
    {
        LOG_INFO("Failed to find base for %s.", get_kernel_dll());
        return 0;
    }

    addr = get_export(base, "GetCurrentDirectoryW");
    if (addr == NULL)
    {
        LOG_INFO(
            "Unable to resolve address for GetCurrentDirectoryW in %s",
            get_kernel_dll()
        );
        return 0;
    }

    g_hook_trap_addr = addr;
    g_hook_trap_value = *g_hook_trap_addr;

    g_prev_seh_func = SetUnhandledExceptionFilter(hook_trap);

    // Write a HALT instruction to force an exception.
    to_write = 0xf4;
    write_vm(g_current_proc, addr, &to_write, sizeof(to_write));

    return 1;
}
