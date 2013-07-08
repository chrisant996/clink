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
const char*         find_next_ansi_code(const char*, int*);
void                lua_filter_prompt(char*, int);

//------------------------------------------------------------------------------
#define MR(x)                    x "\x08"
const char g_prompt_tag[]        = "@CLINK_PROMPT";
const char g_prompt_tag_hidden[] = MR("C") MR("L") MR("I") MR("N") MR("K") MR(" ");
#undef MR

//------------------------------------------------------------------------------
static int filter_prompt()
{
    char* next;
    char prompt[1024];
    char tagged_prompt[sizeof_array(prompt)];

    // Get the prompt from Readline and pass it to Clink's filter framework
    // in Lua.
    prompt[0] = '\0';
    str_cat(prompt, rl_prompt, sizeof_array(prompt));

    lua_filter_prompt(prompt, sizeof_array(prompt));

    // Scan for ansi codes and surround them with Readline's markers for
    // invisible characters.
    tagged_prompt[0] ='\0';
    next = prompt;
    while (*next)
    {
        static const int tp_size = sizeof_array(tagged_prompt);

        int size;
        char* code;

        code = (char*)find_next_ansi_code(next, &size);
        str_cat_n(tagged_prompt, next, tp_size, code - next);
        if (*code)
        {
            static const char* tags[] = { "\001", "\002" };

            str_cat(tagged_prompt, tags[0], tp_size);
            str_cat_n(tagged_prompt, code, tp_size, size);
            str_cat(tagged_prompt, tags[1], tp_size);
        }

        next = code + size;
    }

    rl_set_prompt(tagged_prompt);
    return 0;
}

// vim: expandtab
