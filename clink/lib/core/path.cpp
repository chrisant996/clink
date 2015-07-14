// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "path.h"
#include "str.h"

//------------------------------------------------------------------------------
void path::clean(str_base& in_out, int sep)
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
            if (c == '\\' || c == '/' || c == sep)
            {
                c = sep;
                state = state_slash;
            }

            *write = c;
            ++write;
            break;

        case state_slash:
            if (c != '\\' && c != '/' && c != sep)
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
    if (const char* slash = max(strrchr(in, '\\'), strrchr(in, '/')))
        return out.concat(in, int(slash - in));

    return false;
}

//------------------------------------------------------------------------------
bool path::get_directory(str_base& in_out)
{
    int slash = max(in_out.last_of('\\'), in_out.last_of('/'));
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
    if (const char* slash = max(strrchr(in, '\\'), strrchr(in, '/')))
        return out.concat(slash + 1);

    return out.concat(in);
}

//------------------------------------------------------------------------------
bool path::join(const char* lhs, const char* rhs, str_base& out)
{
    out << lhs;
    return append(out, rhs);
}

//------------------------------------------------------------------------------
bool path::append(str_base& out, const char* rhs)
{
    int last = int(out.length() - 1);
    if (last >= 0 && *rhs)
        if (out[last] != '\\' && out[last] != '/' && rhs[0] != '\\' && rhs[0] != '/')
            out << "\\";

    return out.concat(rhs);
}
