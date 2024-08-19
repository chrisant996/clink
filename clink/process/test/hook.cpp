// Copyright (c) 2021 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <process/process.h>
#include <utils/hook_setter.h>

typedef process::funcptr_t funcptr_t;

//------------------------------------------------------------------------------
enum
{
    visitor_set_env_var     = 1 << 0,
    visitor_write_console   = 1 << 1,
    visitor_get_env_var     = 1 << 2,
    visitor_read_console    = 1 << 3,
    visitor_set_title       = 1 << 4,
};

#if defined(__MINGW32__) || defined(__MINGW64__)
#define __CONSOLE_READCONSOLE_CONTROL VOID
#else
#define __CONSOLE_READCONSOLE_CONTROL CONSOLE_READCONSOLE_CONTROL
#endif

struct originals
{
    BOOL    (WINAPI *set_env_var)(const wchar_t*, const wchar_t*);
    BOOL    (WINAPI *write_console)(HANDLE, const void*, DWORD, LPDWORD, LPVOID);
    DWORD   (WINAPI *get_env_var)(const wchar_t*, wchar_t*, DWORD);
    BOOL    (WINAPI *read_console)(HANDLE, void*, DWORD, LPDWORD, __CONSOLE_READCONSOLE_CONTROL*);
    BOOL    (WINAPI *set_title)(const wchar_t*);
};

static originals    g_originals;
static uint32       g_visited_bits;

//------------------------------------------------------------------------------
BOOL WINAPI set_env_var(const wchar_t* name, const wchar_t* value)
{
    g_visited_bits |= visitor_set_env_var;
    return g_originals.set_env_var(name, value);
}

//------------------------------------------------------------------------------
BOOL WINAPI write_console(
    HANDLE output,
    const void* chars,
    DWORD to_write,
    LPDWORD written,
    LPVOID unused)
{
    g_visited_bits |= visitor_write_console;
    return g_originals.write_console(output, chars, to_write, written, unused);
}

//------------------------------------------------------------------------------
DWORD WINAPI get_env_var(const wchar_t* name, wchar_t* buffer, DWORD size)
{
    g_visited_bits |= visitor_get_env_var;
    return g_originals.get_env_var(name, buffer, size);
}

//------------------------------------------------------------------------------
BOOL WINAPI read_console(HANDLE, void*, DWORD, LPDWORD, __CONSOLE_READCONSOLE_CONTROL*)
{
    g_visited_bits |= visitor_read_console;
    return TRUE;
}

//------------------------------------------------------------------------------
BOOL WINAPI set_title(const wchar_t*)
{
    g_visited_bits |= visitor_set_title;
    return TRUE;
}



//------------------------------------------------------------------------------
funcptr_t hook_iat(void* /*base*/, const char* /*dll*/, const char* name, funcptr_t func, int32 /*find_by_name*/)
{
    funcptr_t original;

    hook_setter hooks;
    if (!hooks.attach(hook_type::iat, nullptr, name, func, &original))
        return nullptr;
    if (!hooks.commit())
        return nullptr;

    return original;
}



//------------------------------------------------------------------------------
__declspec(noinline) void apply_hooks()
{
    // The compiler can cache the IAT lookup into a register before the IAT is
    // patched causing later calls in the same to bypass the hook. To workaround
    // this the hooks are applied in a no-inlined function

    void* base = GetModuleHandle(nullptr);

    funcptr_t original;

    g_originals.set_env_var = SetEnvironmentVariableW;
    original = hook_iat(base, nullptr, "SetEnvironmentVariableW", funcptr_t(set_env_var), 1);
    REQUIRE(original == funcptr_t(g_originals.set_env_var));

    g_originals.write_console = WriteConsoleW;
    original = hook_iat(base, nullptr, "WriteConsoleW", funcptr_t(write_console), 1);
    REQUIRE(original == funcptr_t(g_originals.write_console));

    g_originals.get_env_var = GetEnvironmentVariableW;
    original = hook_iat(base, nullptr, "GetEnvironmentVariableW", funcptr_t(get_env_var), 1);
    REQUIRE(original == funcptr_t(g_originals.get_env_var));

    g_originals.read_console = ReadConsoleW;
    original = hook_iat(base, nullptr, "ReadConsoleW", funcptr_t(read_console), 1);
    REQUIRE(original == funcptr_t(g_originals.read_console));

    g_originals.set_title = SetConsoleTitleW;
    original = hook_iat(base, nullptr, "SetConsoleTitleW", funcptr_t(set_title), 1);
    REQUIRE(original == funcptr_t(g_originals.set_title));
}

//------------------------------------------------------------------------------
TEST_CASE("Hooks")
{
    apply_hooks();

    g_visited_bits = 0;

    SetEnvironmentVariableW(L"clink_test", nullptr);
    REQUIRE(g_visited_bits & visitor_set_env_var);

    WriteConsoleW(nullptr, nullptr, 0, nullptr, nullptr);
    REQUIRE(g_visited_bits & visitor_write_console);

    WCHAR buffer[64];
    GetEnvironmentVariableW(L"clink_test", buffer, _countof(buffer));
    REQUIRE(g_visited_bits & visitor_get_env_var);

    ReadConsoleW(nullptr, nullptr, 0, nullptr, nullptr);
    REQUIRE(g_visited_bits & visitor_read_console);

    SetConsoleTitleW(nullptr);
    REQUIRE(g_visited_bits & visitor_set_title);

    // Detach hooks so they don't affect subsequent tests.
    hook_setter hooks;
    hooks.detach(hook_type::iat, nullptr, "SetEnvironmentVariableW", &g_originals.set_env_var, set_env_var);
    hooks.detach(hook_type::iat, nullptr, "WriteConsoleW", &g_originals.write_console, write_console);
    hooks.detach(hook_type::iat, nullptr, "GetEnvironmentVariableW", &g_originals.get_env_var, get_env_var);
    hooks.detach(hook_type::iat, nullptr, "ReadConsoleW", &g_originals.read_console, read_console);
    hooks.detach(hook_type::iat, nullptr, "SetConsoleTitleW", &g_originals.set_title, set_title);
    REQUIRE(hooks.commit());
}
