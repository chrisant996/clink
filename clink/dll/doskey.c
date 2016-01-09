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

//------------------------------------------------------------------------------
typedef struct {
    short       start;
    short       length;
} token_t;

static struct {
    wchar_t*    alias_text;
    wchar_t*    alias_next;
    wchar_t*    input;
    token_t     tokens[10];
    unsigned    token_count;
} g_state;

//------------------------------------------------------------------------------
static int tokenise(wchar_t* source, token_t* tokens, int max_tokens)
{
    // The doskey tokenisation (done by conhost.exe on Win7 and in theory
    // available to any console app) is pretty basic. Doesn't take quotes into
    // account. Doesn't skip leading whitespace.

    int i;
    wchar_t* read;

    read = source;
    for (i = 0; i < max_tokens && *read; ++i)
    {
        // Skip whitespace, store the token start.
        while (*read && iswspace(*read))
            ++read;

        tokens[i].start = read - source;

        // Skip token to next whitespace, store token length.
        while (*read && !iswspace(*read))
            ++read;

        tokens[i].length = (read - source) - tokens[i].start;
    }

    // Don't skip initial whitespace.
    tokens[0].length += tokens[0].start;
    tokens[0].start = 0;
    return i;
}

//------------------------------------------------------------------------------
int continue_doskey(wchar_t* chars, unsigned max_chars)
{
    wchar_t* read = g_state.alias_next;

    if (g_state.alias_text == NULL)
        return 0;

    if (*read == '\0')
    {
        free(g_state.alias_text);
        g_state.alias_text = NULL;
        return 0;
    }

    --max_chars;
    while (*read > '\1' && max_chars)
    {
        wchar_t c = *read++;

        // If this isn't a '$X' code then just copy the character out.
        if (c != '$')
        {
            *chars++ = c;
            --max_chars;
            continue;
        }

        // This is is a '$X' code. All but argument codes have been handled so
        // it is just argument codes to expand now.
        c = *read++;
        if (c >= '1' && c <= '9')   c -= '1' - 1; // -1 as first arg is token 1
        else if (c == '*')          c = 0;
        else if (c > '\1')
        {
            --read;
            *chars++ = '$';
            --max_chars;
        }
        else
            break;

        // 'c' is the index to the argument to insert or -1 if it is all of
        // them. 0th token is alias so arguments start at index 1.
        if (g_state.token_count > 1)
        {
            wchar_t* insert_from;
            int insert_length = 0;

            if (c == 0 && g_state.token_count > 1)
            {
                insert_from = g_state.input + g_state.tokens[1].start;
                insert_length = min(wcslen(insert_from), max_chars);
            }
            else if (c < g_state.token_count)
            {
                insert_from = g_state.input + g_state.tokens[c].start;
                insert_length = min(g_state.tokens[c].length, max_chars);
            }

            if (insert_length)
            {
                wcsncpy(chars, insert_from, insert_length);
                max_chars -= insert_length;
                chars += insert_length;
            }
        }
    }

    *chars = '\0';

    // Move g_state.next on to the next command or the end of the expansion.
    g_state.alias_next = read;
    while (*g_state.alias_next > '\1')
        ++g_state.alias_next;

    if (*g_state.alias_next == '\1')
        ++g_state.alias_next;

    return 1;
}

//------------------------------------------------------------------------------
int begin_doskey(wchar_t* chars, unsigned max_chars)
{
    // Find the alias for which to retrieve text for.
    wchar_t alias[64];
    {
        int i, n;
        int found_word = 0;
        const wchar_t* read = chars;
        for (i = 0, n = min(sizeof_array(alias) - 1, max_chars); i < n && *read; ++i)
        {
            if (!!iswspace(*read) == found_word)
            {
                if (!found_word)
                    found_word = 1;
                else
                    break;
            }

            alias[i] = *read++;
        }

        alias[i] = '\0';
    }

    // Find the alias' text.
    {
        int bytes;
        wchar_t* exe;
        wchar_t exe_path[MAX_PATH];

        GetModuleFileNameW(NULL, exe_path, sizeof_array(exe_path));
        exe = wcsrchr(exe_path, L'\\');
        exe = (exe != NULL) ? (exe + 1) : exe_path;

        // Check it exists.
        if (!GetConsoleAliasW(alias, exe_path, 1, exe))
            return 0;

        // It does. Allocate space and fetch it.
        bytes = max_chars * sizeof(wchar_t);
        g_state.alias_text = malloc(bytes * 2);
        GetConsoleAliasW(alias, g_state.alias_text, bytes, exe);

        // Copy the input and tokenise it. Lots of pointer aliasing here...
        g_state.input = g_state.alias_text + max_chars;
        memcpy(g_state.input, chars, bytes);

        g_state.token_count = tokenise(g_state.input, g_state.tokens,
            sizeof_array(g_state.tokens));

        g_state.alias_next = g_state.alias_text;
    }

    // Expand all '$?' codes except those that expand into arguments.
    {
        wchar_t* read = g_state.alias_text;
        wchar_t* write = read;
        while (*read)
        {
            if (read[0] != '$')
            {
                *write++ = *read++;
                continue;
            }

            ++read;
            switch (*read)
            {
            case '$': *write++ = '$'; break;
            case 'g':
            case 'G': *write++ = '>'; break;
            case 'l':
            case 'L': *write++ = '<'; break;
            case 'b':
            case 'B': *write++ = '|'; break;
            case 't':
            case 'T': *write++ = '\1'; break;

            default:
                *write++ = '$';
                *write++ = *read;
            }

            ++read;
        }

        *write = '\0';
    }

    return continue_doskey(chars, max_chars);
}

// vim: expandtab
