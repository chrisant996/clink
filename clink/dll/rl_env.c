/* Copyright (c) 2013 Martin Ridgers
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

//------------------------------------------------------------------------------
static void apply_env(const char* env, int clear)
{
    int size;
    char* strings;
    char* c;

    // Find length of the environment block.
    c = env;
    while (*c)
    {
        c += strlen(c) + 1;
    }

    // Copy the block.
    size = (int)(c - env + 1);
    strings = malloc(size);
    memcpy(strings, env, size);

    // Apply each environment variable.
    c = strings;
    while (*c)
    {
        int len = (int)strlen(c);
        char* value = strchr(c, '=');
        if ((value != NULL) && (*c != '='))
        {
            *value = '\0';
            ++value;

            SetEnvironmentVariable(c, clear ? NULL : value);
        }

        c += len + 1;
    }

    free(strings);
}

//------------------------------------------------------------------------------
static void* push_env()
{
    return GetEnvironmentStrings();
}

//------------------------------------------------------------------------------
static void pop_env(void* handle)
{
    void* to_clear;

    to_clear = GetEnvironmentStrings();
    if (to_clear != NULL)
    {
        apply_env(to_clear, 1);
        FreeEnvironmentStrings(to_clear);
    }

    apply_env(handle, 0);
    FreeEnvironmentStrings(handle);
}

//------------------------------------------------------------------------------
void prepare_env_for_inputrc()
{
    // Give readline a chance to find the inputrc by modifying the
    // environment slightly.

    char buffer[1024];
    void* env_handle;

    env_handle = push_env();

    // HOME is where Readline will expand ~ to.
    {
        static const char home_eq[] = "HOME=";
        int size = sizeof_array(home_eq);

        strcpy(buffer, home_eq);
        get_config_dir(buffer + size - 1, sizeof_array(buffer) - size);

        putenv(buffer);
    }

    // INPUTRC is the path where looks for it's configuration file.
    {
        static const char inputrc_eq[] = "INPUTRC=";
        int size = sizeof_array(inputrc_eq);

        strcpy(buffer, inputrc_eq);
        get_dll_dir(buffer + size - 1, sizeof_array(buffer) - size);
        str_cat(buffer, "/clink_inputrc_base", sizeof_array(buffer));

        putenv(buffer);
    }

    pop_env(env_handle);
}
