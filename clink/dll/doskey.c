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
static int tokenise(wchar_t* source, wchar_t** tokens, int max_tokens)
{
    // The doskey tokenisation (done by conhost.exe on Win7 and in theory
    // available to any console app) is pretty basic. Doesn't take quotes into
    // account. Doesn't skip leading whitespace.

    int i;
    wchar_t* read;

    read = source;
    for (i = 0; i < max_tokens && *read; ++i)
    {
        // Skip whitespace, nulling as we go (not first time through though).
        while (*read && iswspace(*read))
        {
            *read = i ? '\0' : *read;
            ++read;
        }
        
        // Store start of a token.
        tokens[i] = read;

        // Skip token to next whitespace.
        while (*read && !iswspace(*read))
        {
            ++read;
        }
    }

    // Don't skip initial whitespace (in keeping with cmd.exe).
    tokens[0] = source;
    return i;
}

//------------------------------------------------------------------------------
void emulate_doskey(wchar_t* buffer, unsigned max_chars)
{
    // ReadConsoleW() does the alias and argument expansion. Win7 onwards, this
    // actually happens in the associated conhost.exe via an undocumented LPC.
    // As such there's no public API for argument expansion.

    const wchar_t* read;
    wchar_t* read_end;
    wchar_t* write;
    int write_size;
    int in_quote;

    wchar_t* alias;
    wchar_t* scratch[2];

    wchar_t exe_buf[MAX_PATH];
    wchar_t* exe;

    int arg_count;
    struct {
        wchar_t* command;
        wchar_t* args[16];
    } parts;

#ifdef __MINGW32__
    HANDLE kernel32 = LoadLibraryA("kernel32.dll");

    typedef DWORD (WINAPI *_GetConsoleAliasW)(LPWSTR, LPWSTR, DWORD, LPWSTR);
    _GetConsoleAliasW GetConsoleAliasW = (_GetConsoleAliasW)GetProcAddress(
        kernel32,
        "GetConsoleAliasW"
    );
#endif // __MINGW32__

    // Allocate some buffers and fill them.
    alias = malloc(max_chars * sizeof(wchar_t) * 3);
    scratch[0] = alias + max_chars;
    scratch[1] = scratch[0] + max_chars;
    memcpy(scratch[0], buffer, max_chars * sizeof(*scratch[0]));
    memcpy(scratch[1], buffer, max_chars * sizeof(*scratch[1]));

    // Tokenise
    arg_count = tokenise(scratch[0], (wchar_t**)&parts, sizeof_array(parts.args) + 1);
    --arg_count;

    // Find alias
    GetModuleFileNameW(NULL, exe_buf, sizeof(exe_buf));
    exe = wcsrchr(exe_buf, L'\\');
    exe = (exe != NULL) ? (exe + 1) : exe_buf;

    if (!GetConsoleAliasW(parts.command, alias, max_chars, exe))
    {
        free(alias);
        return;
    }

    // Copy alias to buffer, expanding arguments as we go.
    read = alias;
    read_end = alias + wcslen(alias);
    write = buffer;
    write_size = max_chars;
    in_quote = 0;

    memset(write, 0, write_size * sizeof(*write));

    while (write_size > 0 && read < read_end)
    {
        if (*read == '$')
        {
            int insert_len;
            const wchar_t* insert = L"";
            wchar_t c = *(++read);

            ++read;

            c = towlower(c);
            if (c >= '1' && c <= '9')
            {
                c -= '1';
                insert = (c < arg_count) ? parts.args[c] : L"";
            }
            else if (c >= 'a' && c <= 'f')
            {
                c -= 'a' - 9;
                insert = (c < arg_count) ? parts.args[c] : L"";
            }
            else if (c == '*')
            {
                if (arg_count)
                    insert = scratch[1] + (parts.args[0] - parts.command);
            }
            else if (c == '$')
            {
                insert = L"$";
            }
            else if (towlower(c) == 't')
            {
                // Normally, the alias expansion would add a crlf pair for this
                // tag, and then feed it piece by piece to each ReadConsole()
                // call. This is most inconvenient. Instead we're using &.
                // Maybe this makes this emulation incompatible with other exes?
                insert = L"&";
            }
            else if (towlower(c) == 'g')
            {
                insert = L">";
            }
            else if (towlower(c) == 'l')
            {
                insert = L"<";
            }
            else if (towlower(c) == 'b')
            {
                insert = L"|";
            }
            else
            {
                continue;
            }

            insert_len = wcslen(insert);
            insert_len = (insert_len > write_size) ? write_size : insert_len;
            wcsncpy(write, insert, insert_len);

            write_size -= insert_len;
            write += insert_len;
        }
        else if (!in_quote && read[1] == '%' && read[0] != '^')
        {
            if (write_size > 2)
            {
                write[0] = *read;
                write[1] = '^';
                write[2] = '%';
                write += 3;
                read += 2;
            }
        }
        else
        {
            if (*read == '\"')
                in_quote ^= 1;

            *write = *read;

            ++write;
            ++read;
            --write_size;
        }
    }

    buffer[max_chars - 1] = '\0';
    free(alias);
}

// vim: expandtab
