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
#define MR(x)                        L##x L"\x08"
const wchar_t  g_prompt_tag[]        = L"@CLINK_PROMPT";
const wchar_t  g_prompt_tag_hidden[] = MR("C") MR("L") MR("I") MR("N") MR("K") MR(" ");
const wchar_t* g_prompt_tags[]       = { g_prompt_tag, g_prompt_tag_hidden };
#undef MR

//------------------------------------------------------------------------------
wchar_t* detect_tagged_prompt_w(const wchar_t* buffer, int length)
{
    int i;

    // For each accepted tag...
    for (i = 0; i < sizeof_array(g_prompt_tags); ++i)
    {
        const wchar_t* tag = g_prompt_tags[i];
        int tag_length = wcslen(tag);

        // Found a match? Convert the remainer to Utf8 and return it.
        if (wcsncmp(buffer, tag, tag_length) == 0)
        {
            wchar_t* out;
            
            out = malloc(length * sizeof(*out));
            length -= tag_length;

            wcsncpy(out, buffer + tag_length, length);
            out[length] = '\0';

            return out;
        }
    }

    return NULL;
}

//------------------------------------------------------------------------------
char* detect_tagged_prompt(const char* buffer, int length)
{
    int i;

    // For each accepted tag...
    for (i = 0; i < sizeof_array(g_prompt_tags); ++i)
    {
        const wchar_t* tag = g_prompt_tags[i];
        int tag_length = wcslen(tag);
        int j, n;

        // Count the number of matching characters.
        int matched = 0;
        for (j = 0, n = min(length, tag_length); j < n; ++j)
        {
            matched += (tag[j] == buffer[j]);
        }

        // Does the buffer start with the tag?
        if (matched == tag_length)
        {
            char* out;

            out = malloc(length * sizeof(*out));
            length -= tag_length;

            strncpy(out, buffer + tag_length, length);
            out[length] = '\0';

            return out;
        }
    }

    return NULL;
}

//------------------------------------------------------------------------------
void free_prompt(void* buffer)
{
    free(buffer);
}

//------------------------------------------------------------------------------
static int parse_backspaces(char* prompt, int n)
{
    // This function does not null terminate!

    char* write;
    char* read;

    write = prompt;
    read = prompt;
    while (*read && read < (prompt + n))
    {
        if (*read == '\b')
        {
            if (write > prompt)
            {
                --write;
            }
        }
        else
        {
            *write = *read;
            ++write;
        }

        ++read;
    }

    return write - prompt;
}

//------------------------------------------------------------------------------
char* filter_prompt(const char* in_prompt)
{
    static const int buf_size = 0x4000;

    char* next;
    char* out_prompt;
    char* lua_prompt;

    // Allocate the buffers. We'll allocate once and divide it in two.
    out_prompt = malloc(buf_size * 2);
    lua_prompt = out_prompt + buf_size;

    // Get the prompt from Readline and pass it to Clink's filter framework
    // in Lua.
    lua_prompt[0] = '\0';
    str_cat(lua_prompt, in_prompt, buf_size);

    lua_filter_prompt(lua_prompt, buf_size);

    // Scan for ansi codes and surround them with Readline's markers for
    // invisible characters.
    out_prompt[0] ='\0';
    next = lua_prompt;
    while (*next)
    {
        int len;
        int ansi_size;
        char* ansi_code;

        ansi_code = (char*)find_next_ansi_code(next, &ansi_size);

        len = parse_backspaces(next, ansi_code - next);
        str_cat_n(out_prompt, next, buf_size, len);

        if (*ansi_code)
        {
            static const char* tags[] = { "\001", "\002" };

            len = parse_backspaces(ansi_code, ansi_size);

            str_cat(out_prompt, tags[0], buf_size);
            str_cat_n(out_prompt, ansi_code, buf_size, len);
            str_cat(out_prompt, tags[1], buf_size);
        }

        next = ansi_code + ansi_size;
    }

    return out_prompt;
}

//------------------------------------------------------------------------------
void* extract_prompt(int ret_as_utf8)
{
    char* prompt;
    wchar_t* buffer;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE handle;
    int length;
    COORD cur;
    DWORD chars_read;

    // Find where the cursor is (tip; it's at the end of the prompt).
    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(handle, &csbi) == FALSE)
        return NULL;

    // Work out prompt length and allocate some working buffer space.
    cur = csbi.dwCursorPosition;
    length = cur.X;
    if (length < 0)
        return NULL;

    cur.X = 0;
    prompt = malloc(length * 8);

    // Get the prompt from the terminal.
    buffer = (wchar_t*)prompt + length + 2;
    buffer[0] = L'\0';
    if (ReadConsoleOutputCharacterW(handle, buffer, length, cur, &chars_read))
        buffer[chars_read] = L'\0';

    // Convert to Utf8 and return.
    if (ret_as_utf8)
    {
        length = WideCharToMultiByte(
            CP_UTF8, 0,
            buffer, length,
            prompt, (char*)buffer - prompt,
            NULL, NULL
        );

        if (length <= 0)
        {
            return NULL;
        }

        prompt[length] = '\0';
        return prompt;
    }

    wcscpy((wchar_t*)prompt, buffer);
    return prompt;
}

// vim: expandtab
