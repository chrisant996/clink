/* Copyright (c) 2015 Martin Ridgers
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

#include "catch.hpp"
#include "env_fixture.h"

#include <core/str.h>
#include <Windows.h>

//------------------------------------------------------------------------------
env_fixture::env_fixture(const char** env)
{
    m_env_strings = GetEnvironmentStringsW();

    // Wipe out the exisiting environment.
    wchar_t* env_string = m_env_strings;
    while (*env_string)
    {
        wchar_t* eq = wcschr(env_string, '=');
        if (eq == env_string)
            eq = wcschr(env_string + 1, '='); // skips cmd's hidden "=X:" vars

        env_string += wcslen(env_string) + 1;

        REQUIRE(eq != nullptr);
        *eq = '\0';
    }

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
void env_fixture::clear()
{
    wchar_t* env_string = m_env_strings;
    while (*env_string)
    {
        REQUIRE(SetEnvironmentVariableW(env_string, nullptr) != FALSE);
        env_string += wcslen(env_string) + 1; // name
        env_string += wcslen(env_string) + 1; // value
    }

    wchar_t* env_is_empty = GetEnvironmentStringsW();
    REQUIRE(*env_is_empty == 0);
    FreeEnvironmentStringsW(env_is_empty);
}
