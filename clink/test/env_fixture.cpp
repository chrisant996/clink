// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "env_fixture.h"

#include <core/str.h>
#include <Windows.h>

//------------------------------------------------------------------------------
env_fixture::env_fixture(const char** env)
{
    // Store the process' current environment so it can be restored.
    m_env_strings = GetEnvironmentStringsW();
    convert_eq_to_null(m_env_strings);

    clear();

    // Apply the test environment.
    while (*env)
    {
        REQUIRE(SetEnvironmentVariable(env[0], env[1]) != FALSE);
        env += 2;
    }
}

//------------------------------------------------------------------------------
env_fixture::~env_fixture()
{
    clear();

    // Restore previous environment.
    wchar_t* env_string = m_env_strings;
    while (*env_string)
    {
        wchar_t* value = env_string + (wcslen(env_string) + 1);
        REQUIRE(SetEnvironmentVariableW(env_string, value) != FALSE);
        env_string += wcslen(value) + 1;
    }

    FreeEnvironmentStringsW(m_env_strings);
}

//------------------------------------------------------------------------------
void env_fixture::convert_eq_to_null(wchar_t* env_strings)
{
    wchar_t* env_string = env_strings;
    while (*env_string)
    {
        wchar_t* eq = wcschr(env_string, '=');
        if (eq == env_string)
            eq = wcschr(env_string + 1, '='); // skips cmd's hidden "=X:" vars

        env_string += wcslen(env_string) + 1;

        REQUIRE(eq != nullptr);
        *eq = '\0';
    }
}

//------------------------------------------------------------------------------
void env_fixture::clear()
{
    wchar_t* env_strings = GetEnvironmentStringsW();
    convert_eq_to_null(env_strings);

    wchar_t* env_string = env_strings;
    while (*env_string)
    {
        REQUIRE(SetEnvironmentVariableW(env_string, nullptr) != FALSE);
        env_string += wcslen(env_string) + 1; // name
        env_string += wcslen(env_string) + 1; // value
    }

    FreeEnvironmentStringsW(env_strings);

    // Confirm it is empty.
    wchar_t* env_is_empty = GetEnvironmentStringsW();
    REQUIRE(*env_is_empty == 0);
    FreeEnvironmentStringsW(env_is_empty);
}
