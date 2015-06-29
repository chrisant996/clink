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

#include "pch.h"
#include "path.h"
#include "str.h"

//------------------------------------------------------------------------------
void path::clean(str_base& in_out)
{
    enum clean_state
    {
        state_write,
        state_slash
    };

    clean_state state = state_write;
    char* write = in_out.data();
    const char* read = write;
    while (char c = *read)
    {
        switch (state)
        {
        case state_write:
            if (c == '\\' || c == '/')
            {
                c = '\\';
                state = state_slash;
            }

            *write = c;
            ++write;
            break;

        case state_slash:
            if (c != '\\' && c != '/')
            {
                state = state_write;
                continue;
            }
            break;
        }

        ++read;
    }

    *write = '\0';
}

//------------------------------------------------------------------------------
bool path::get_base_name(const char* in, str_base& out)
{
    if (!get_name(in, out))
        return false;

    int dot = out.last_of('.');
    if (dot >= 0)
        out.truncate(dot);

    return true;
}

//------------------------------------------------------------------------------
bool path::get_directory(const char* in, str_base& out)
{
    const char* slash = strrchr(in, '\\');
    if (slash == nullptr)
        slash = strrchr(in, '/');

    if (slash == nullptr)
        return false;

    return out.concat(in, slash - in);
}

//------------------------------------------------------------------------------
bool path::get_directory(str_base& in_out)
{
    int slash = in_out.last_of('\\');
    if (slash < 0)
        slash = in_out.last_of('/');

    if (slash < 0)
        return false;

    in_out.truncate(slash);
    return true;
}

//------------------------------------------------------------------------------
bool path::get_drive(const char* in, str_base& out)
{
    if ((in[1] != ':') || (unsigned(tolower(in[0]) - 'a') > ('z' - 'a')))
        return false;

    return out.concat(in, 2);
}

//------------------------------------------------------------------------------
bool path::get_drive(str_base& in_out)
{
    if ((in_out[1] != ':') || (unsigned(tolower(in_out[0]) - 'a') > ('z' - 'a')))
        return false;

    in_out.truncate(2);
    return (in_out.size() > 2);
}

//------------------------------------------------------------------------------
bool path::get_extension(const char* in, str_base& out)
{
    const char* dot = strrchr(in, '.');
    if (dot == nullptr)
        return false;

    return out.concat(dot);
}

//------------------------------------------------------------------------------
bool path::get_name(const char* in, str_base& out)
{
    const char* slash = strrchr(in, '\\');
    if (slash == nullptr)
        slash = strrchr(in, '/');

    if (slash == nullptr)
        return out.concat(in);

    return out.concat(slash + 1);
}

//------------------------------------------------------------------------------
bool path::join(const char* lhs, const char* rhs, str_base& out)
{
    out << lhs;

    int last = int(strlen(lhs) - 1);
    if (lhs[last] != '\\' && lhs[last] != '/' && rhs[0] != '\\' && rhs[0] != '/')
        out << "\\";

    return out.concat(rhs);
}
