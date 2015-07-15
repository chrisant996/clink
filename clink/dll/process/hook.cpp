// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "vm.h"
#include "pe.h"

#include <core/base.h>
#include <core/log.h>

//------------------------------------------------------------------------------
static void* current_proc()
{
    return (void*)GetCurrentProcess();
}

//------------------------------------------------------------------------------
static void write_addr(void** where, void* to_write)
{
    struct region_info_t region_info;

    get_region_info(where, &region_info);
    set_region_write_state(&region_info, 1);

    if (!write_vm(current_proc(), where, &to_write, sizeof(to_write)))
        LOG("VM write to %p failed (err = %d)", where, GetLastError());

    set_region_write_state(&region_info, 0);
}

//------------------------------------------------------------------------------
static void* get_proc_addr(const char* dll, const char* func_name)
{
    void* base;

    base = LoadLibraryA(dll);
    if (base == nullptr)
    {
        LOG("Failed to load library '%s'", dll);
        return nullptr;
    }

    return get_export(base, func_name);
}

//------------------------------------------------------------------------------
void* hook_iat(
    void* base,
    const char* dll,
    const char* func_name,
    void* hook,
    int find_by_name
)
{
    LOG("Attempting to hook IAT for module %p", base);
    LOG("Target is %s,%s (by_name=%d)", dll, func_name, find_by_name);

    void** import;

    // Find entry and replace it.
    if (find_by_name)
        import = get_import_by_name(base, nullptr, func_name);
    else
    {
        // Get the address of the function we're going to hook.
        void* func_addr = get_proc_addr(dll, func_name);
        if (func_addr == nullptr)
        {
            LOG("Failed to find function '%s' in '%s'", func_name, dll);
            return nullptr;
        }

        import = get_import_by_addr(base, nullptr, func_addr);
    }

    if (import == nullptr)
    {
        LOG("Unable to find import in IAT (by_name=%d)", find_by_name);
        return nullptr;
    }

    LOG("Found import at %p (value = %p)", import, *import);

    void* prev_addr = *import;
    write_addr(import, hook);

    FlushInstructionCache(current_proc(), 0, 0);
    return prev_addr;
}

//------------------------------------------------------------------------------
static char* alloc_trampoline(void* hint)
{
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);

    void* trampoline = nullptr;
    while (trampoline == nullptr)
    {
        void* vm_alloc_base = get_alloc_base(hint);
        vm_alloc_base = vm_alloc_base ? vm_alloc_base : hint;

        char* tramp_page = (char*)vm_alloc_base - sys_info.dwAllocationGranularity;

        trampoline = VirtualAlloc(tramp_page, sys_info.dwPageSize,
            MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);

        hint = tramp_page;
    }

    return (char*)trampoline;
}

//------------------------------------------------------------------------------
static int get_mask_size(unsigned mask)
{
    mask &= 0x01010101;
    mask += mask >> 16;
    mask += mask >> 8;
    return mask & 0x0f;
}

//------------------------------------------------------------------------------
static char* write_rel_jmp(char* write, void* dest)
{
    // jmp <displacement>
    intptr_t disp = (intptr_t)dest;
    disp -= (intptr_t)write;
    disp -= 5;

    struct {
        char a;
        char b[4];
    } buffer;

    buffer.a = (unsigned char)0xe9;
    *(int*)buffer.b = (int)disp;

    if (!write_vm(current_proc(), write, &buffer, sizeof(buffer)))
    {
        LOG("VM write to %p failed (err = %d)", write, GetLastError());
        return nullptr;
    }

    return write + 5;
}

//------------------------------------------------------------------------------
static char* write_trampoline_out(char* write, void* to_hook, void* hook)
{
    char* patch = (char*)to_hook - 5;

    // Check we've got a nop slide or int3 block to patch into.
    for (int i = 0; i < 5; ++i)
    {
        unsigned char c = patch[i];
        if (c != 0x90 && c != 0xcc)
        {
            LOG("No nop slide or int3 block detected prior to hook target.");
            return nullptr;
        }
    }

    // Patch the API.
    patch = write_rel_jmp(patch, write);
    short temp = (unsigned short)0xf9eb;
    if (!write_vm(current_proc(), patch, &temp, sizeof(temp)))
    {
        LOG("VM write to %p failed (err = %d)", patch, GetLastError());
        return nullptr;
    }

    // Long jmp.
    struct {
        char a[2];
        char b[4];
        char c[sizeof(void*)];
    } inst;

    *(short*)inst.a = 0x25ff;

    unsigned rel_addr = 0;
#ifdef _M_IX86
    rel_addr = (intptr_t)write + 6;
#endif

    *(int*)inst.b = rel_addr;
    *(void**)inst.c = hook;

    if (!write_vm(current_proc(), write, &inst, sizeof(inst)))
    {
        LOG("VM write to %p failed (err = %d)", write, GetLastError());
        return nullptr;
    }

    return write + sizeof(inst);
}

//------------------------------------------------------------------------------
static char* write_trampoline_in(char* write, void* to_hook, int n)
{
    for (int i = 0; i < n; ++i)
    {
        if (!write_vm(current_proc(), write, (char*)to_hook + i, 1))
        {
            LOG("VM write to %p failed (err = %d)", write, GetLastError());
            return nullptr;
        }

        ++write;
    }

    // If the moved instruction is JMP (e9) then the displacement is relative
    // to its original location. As we have relocated the jump the displacement
    // needs adjusting.
    if (*(unsigned char*)to_hook == 0xe9)
    {
        int displacement = *(int*)(write - 4);
        intptr_t old_ip = (intptr_t)to_hook + n;
        intptr_t new_ip = (intptr_t)write;

        *(int*)(write - 4) = (int)(displacement + old_ip - new_ip);
    }

    return write_rel_jmp(write, (char*)to_hook + n);
}

//------------------------------------------------------------------------------
static int get_instruction_length(void* addr)
{
    static const struct {
        unsigned expected;
        unsigned mask;
    } asm_tags[] = {
#ifdef _M_X64
        { 0x38ec8348, 0xffffffff },  // sub rsp,38h
        { 0x0000f3ff, 0x0000ffff },  // push rbx
        { 0x00005340, 0x0000ffff },  // push rbx
        { 0x00dc8b4c, 0x00ffffff },  // mov r11, rsp
        { 0x0000b848, 0x0000f8ff },  // mov reg64, imm64  = 10-byte length
#elif defined _M_IX86
        { 0x0000ff8b, 0x0000ffff },  // mov edi,edi
#endif
        { 0x000000e9, 0x000000ff },  // jmp addr32        = 5-byte length
    };

    unsigned prolog = *(unsigned*)(addr);
    for (int i = 0; i < sizeof_array(asm_tags); ++i)
    {
        unsigned expected = asm_tags[i].expected;
        unsigned mask = asm_tags[i].mask;

        if (expected != (prolog & mask))
            continue;

        int length = get_mask_size(mask);

        // Checks for instructions that "expected" only partially matches.
        if (expected == 0x0000b848)
            length = 10;
        else if (expected == 0xe9)
            length = 5; // jmp [imm32]

        LOG("Matched prolog %08X (mask = %08X)", prolog, mask);
        return length;
    }

    return 0;
}

//------------------------------------------------------------------------------
static void* follow_jump(void* addr)
{
    unsigned char* t = (unsigned char*)addr;
    int* imm = (int*)(t + 2);

    // Check the opcode.
    if (t[0] != 0xff)
        return addr;

    // Check the opcode extension from the modr/m byte.
    if ((t[1] & 070) != 040)
        return addr;

    void* dest = addr;
    switch (t[1] & 007)
    {
    case 5:
#ifdef _M_X64
        // dest = [rip + disp32]
        dest = *(void**)(t + 6 + *imm);
#elif defined _M_IX86
        // dest = disp32
        dest = (void*)(intptr_t)(*imm);
#endif
    }

    LOG("Following jump to %p", dest);
    return dest;
}

//------------------------------------------------------------------------------
static void* hook_jmp_impl(void* to_hook, void* hook)
{
    struct region_info_t region_info;
    int inst_len;

    LOG("Attempting to hook at %p with %p", to_hook, hook);

    to_hook = follow_jump(to_hook);

    // Work out the length of the first instruction. It will be copied it into
    // the trampoline.
    inst_len = get_instruction_length(to_hook);
    if (inst_len <= 0)
    {
        LOG("Unable to match instruction %08X", *(int*)(to_hook));
        return nullptr;
    }

    // Prepare
    char* trampoline = alloc_trampoline(to_hook);
    if (trampoline == nullptr)
    {
        LOG("Failed to allocate a page for trampolines.");
        return nullptr;
    }

    // In
    char* write = write_trampoline_in(trampoline, to_hook, inst_len);
    if (write == nullptr)
    {
        LOG("Failed to write trampoline in.");
        return nullptr;
    }

    // Out
    get_region_info(to_hook, &region_info);
    set_region_write_state(&region_info, 1);
    write = write_trampoline_out(write, to_hook, hook);
    set_region_write_state(&region_info, 0);
    if (write == nullptr)
    {
        LOG("Failed to write trampoline out.");
        return nullptr;
    }

    return trampoline;
}

//------------------------------------------------------------------------------
void* hook_jmp(void* module, const char* func_name, void* hook)
{
    void* func_addr;
    void* trampoline;
    char module_name[96];

    module_name[0] = '\0';
    GetModuleFileName(HMODULE(module), module_name, sizeof_array(module_name));

    // Get the address of the function we're going to hook.
    func_addr = get_export(module, func_name);
    if (func_addr == nullptr)
    {
        LOG("Failed to find function '%s' in '%s'", func_name, module_name);
        return nullptr;
    }

    LOG("Attemping jump hook.");
    LOG("Target is %s, %s @ %p", module_name, func_name, func_addr);

    // Install the hook.
    trampoline = hook_jmp_impl(func_addr, hook);
    if (trampoline == nullptr)
    {
        LOG("Jump hook failed.");
        return nullptr;
    }

    LOG("Success!");
    FlushInstructionCache(current_proc(), 0, 0);
    return trampoline;
}
