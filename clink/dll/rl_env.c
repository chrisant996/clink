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
struct env_block
{
    int         size;
    const void* data;
};
typedef struct env_block env_block_t;

//------------------------------------------------------------------------------
static void apply_env_impl(const env_block_t* block, int clear)
{
    int size;
    wchar_t* strings;
    wchar_t* c;

    // Copy the block.
    strings = malloc(block->size);
    memcpy(strings, block->data, block->size);

    // Apply each environment variable.
    c = strings;
    while (*c)
    {
        int len = (int)wcslen(c);
        wchar_t* value = wcschr(c, '=');
        if ((value != NULL) && (*c != '='))
        {
            *value = '\0';
            ++value;

            SetEnvironmentVariableW(c, clear ? NULL : value);
        }

        c += len + 1;
    }

    free(strings);
}

//------------------------------------------------------------------------------
static void capture_env(env_block_t* block)
{
    void* env;
    const wchar_t* c;
    void* data;
    int size;

    env = GetEnvironmentStringsW();

    size = 0;
    c = (const wchar_t*)env;
    while (1)
    {
        if (*((const int*)(c)) == 0)
        {
            size += 4;
            break;
        }

        size += 2;
        ++c;
    }

    data = malloc(size);
    memcpy(data, env, size);

    block->data = data;
    block->size = size;

    FreeEnvironmentStringsW(env);
}

//------------------------------------------------------------------------------
static void free_env(const env_block_t* block)
{
    free((void*)(block->data));
}

//------------------------------------------------------------------------------
static void apply_env(const env_block_t* block)
{
    env_block_t to_clear;

    capture_env(&to_clear);
    apply_env_impl(&to_clear, 1);
    free_env(&to_clear);

    apply_env_impl(block, 0);
}

//------------------------------------------------------------------------------
void prepare_env_for_inputrc()
{
    // Give readline a chance to find the inputrc by modifying the
    // environment slightly.

    char buffer[1024];
    void* env_handle;
    env_block_t env_block;

    capture_env(&env_block);

    // HOME is where Readline will expand ~ to.
    if (getenv("home") == NULL)
    {
        static const char home_eq[] = "HOME=";
        int size = sizeof_array(home_eq);

        strcpy(buffer, home_eq);
        if (getenv("homedrive") && getenv("homepath"))
        {
            str_cat(buffer, getenv("homedrive"), sizeof_array(buffer));
            str_cat(buffer, getenv("homepath"), sizeof_array(buffer));
        }
        else if (getenv("userprofile"))
        {
            str_cat(buffer, getenv("userprofile"), sizeof_array(buffer));
        }

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

    apply_env(&env_block);
    free_env(&env_block);
}

// vim: expandtab
