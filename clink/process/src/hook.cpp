// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "hook.h"
#include "vm.h"
#include "pe.h"

#include <core/base.h>
#include <core/log.h>

//------------------------------------------------------------------------------
struct repair_iat_node
{
    repair_iat_node* m_next;
    hookptr_t* m_iat;
    hookptr_t m_trampoline;
};

//------------------------------------------------------------------------------
static void write_addr(hookptr_t* where, hookptr_t to_write)
{
    vm vm;
    vm::region region = { vm.get_page(where), 1 };
    unsigned int prev_access = vm.get_access(region);
    vm.set_access(region, vm::access_write);

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
hookptr_t hook_iat(
    void* base,
    const char* dll,
    const char* func_name,
    hookptr_t hook,
    int find_by_name
)
{
    LOG("Attempting to hook IAT for module %p.", base);
    if (find_by_name)
        LOG("Target is %s (by name).", func_name);
    else
        LOG("Target is %s in %s (by address).", func_name, dll);

    hookptr_t* import;

    // Find entry and replace it.
    pe_info pe(base);
    if (find_by_name)
        import = (hookptr_t*)pe.get_import_by_name(nullptr, func_name);
    else
    {
        // Get the address of the function we're going to hook.
        hookptr_t func_addr = get_proc_addr(dll, func_name);
        if (func_addr == nullptr)
        {
            LOG("Failed to find %s in %s.", func_name, dll);
            return nullptr;
        }

        LOG("Looking up import by address %p.", func_addr);
        import = (hookptr_t*)pe.get_import_by_addr(nullptr, (pe_info::funcptr_t)func_addr);
    }

    if (import == nullptr)
    {
        LOG("Unable to find import in IAT.");
        return nullptr;
    }

    LOG("Found import at %p (value is %p).", import, *import);

    hookptr_t prev_addr = *import;
    write_addr(import, hook);

    vm().flush_icache();
    return prev_addr;
}

//------------------------------------------------------------------------------
void* follow_jump(void* addr)
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
bool add_repair_iat_node(
    repair_iat_node*& list,
    void* base,
    const char* dll,
    const char* func_name,
    hookptr_t trampoline,
    bool find_by_name
)
{
    LOG("Attempting to hook IAT for module %p (repair).", base);

    hookptr_t* import;

    // Find entry and replace it.
    pe_info pe(base);
    if (find_by_name)
    {
        LOG("Target is %s (by name).", func_name);
        import = (hookptr_t*)pe.get_import_by_name(nullptr, func_name);
    }
    else
    {
        LOG("Target is %s in %s (by address).", func_name, dll);

        // Get the address of the function we're going to hook.
        hookptr_t func_addr = get_proc_addr(dll, func_name);
        if (func_addr == nullptr)
        {
            LOG("Failed to find %s in %s.", func_name, dll);
            return false;
        }

        LOG("Looking up import by address %p.", func_addr);
        import = (hookptr_t*)pe.get_import_by_addr(nullptr, (pe_info::funcptr_t)func_addr);
    }

    if (import == nullptr)
    {
        LOG("Unable to find import in IAT.");
        return false;
    }

    LOG("Found import at %p (value is %p).", import, *import);

    repair_iat_node* r = new repair_iat_node;
    r->m_next = list;
    r->m_iat = import;
    r->m_trampoline = trampoline;
    list = r;
    return true;
}

void apply_repair_iat_list(repair_iat_node*& list)
{
    vm vm;

    while (list)
    {
        repair_iat_node* r = list;
        list = list->m_next;

        // TODO: need to somehow preserve prev_addr in order for detach to work correctly.
        hookptr_t prev_addr = *r->m_iat;
        write_addr(r->m_iat, r->m_trampoline);

        delete r;
    }

    vm.flush_icache();
}

void free_repair_iat_list(repair_iat_node*& list)
{
    while (list)
    {
        repair_iat_node* r = list;
        list = list->m_next;
        delete r;
    }
}
