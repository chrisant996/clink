// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "base.h"
#include "path.h"
#include "str.h"

#if defined(PLATFORM_WINDOWS)
#   define PATH_SEP "\\"
#else
#   define PATH_SEP "/"
#endif

//------------------------------------------------------------------------------
static const char* get_last_separator(const char* in)
{
    for (int i = int(strlen(in)) - 1; i >= 0; --i)
        if (path::is_separator(in[i]))
            return in + i;

    return nullptr;
}

//------------------------------------------------------------------------------
static int get_directory_end(const char* path)
{
    if (const char* slash = get_last_separator(path))
    {
        // Trim consecutive slashes unless they're leading ones.
        const char* first_slash = slash;
        while (first_slash >= path)
        {
            if (!path::is_separator(*first_slash))
                break;

            --first_slash;
        }
        ++first_slash;

        if (first_slash != path)
            slash = first_slash;

        // Don't strip '/' if it's the first char.
        if (slash == path)
            ++slash;

#if defined(PLATFORM_WINDOWS)
        // Same for Windows and it's drive prefixes.
        if (path[0] && path[1] == ':' && slash == path + 2)
            ++slash;
#endif

        return int(slash - path);
    }

#if defined(PLATFORM_WINDOWS)
    if (path[0] && path[1] == ':')
        return 2;
#endif

    return 0;
}



namespace path
{

//------------------------------------------------------------------------------
void clean(str_base& in_out, int sep)
{
    clean(in_out.data(), sep);
}

//------------------------------------------------------------------------------
void clean(char* in_out, int sep)
{
    if (!sep)
        sep = PATH_SEP[0];

    enum clean_state
    {
        state_write,
        state_slash
    };

    clean_state state = state_write;
    char* write = in_out;
    const char* read = write;
    while (char c = *read)
    {
        switch (state)
        {
        case state_write:
            if (is_separator(c) || c == sep)
            {
                c = sep;
                state = state_slash;
            }

            *write = c;
            ++write;
            break;

        case state_slash:
            if (!is_separator(c) && c != sep)
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
bool is_separator(int c)
{
#if defined(PLATFORM_WINDOWS)
    return (c == '/' || c == '\\');
#else
    return (c == '/');
#endif
}

//------------------------------------------------------------------------------
const char* next_element(const char* in)
{
    for (; *in; ++in)
        if (is_separator(*in))
            return in + 1;

    return nullptr;
}

//------------------------------------------------------------------------------
bool get_base_name(const char* in, str_base& out)
{
    if (!get_name(in, out))
        return false;

    int dot = out.last_of('.');
    if (dot >= 0)
        out.truncate(dot);

    return true;
}

//------------------------------------------------------------------------------
bool get_directory(const char* in, str_base& out)
{
    int end = get_directory_end(in);
    return out.concat(in, end);
}

//------------------------------------------------------------------------------
bool get_directory(str_base& in_out)
{
    int end = get_directory_end(in_out.c_str());
    in_out.truncate(end);
    return true;
}

//------------------------------------------------------------------------------
bool get_drive(const char* in, str_base& out)
{
#if defined(PLATFORM_WINDOWS)
    if ((in[1] != ':') || (unsigned(tolower(in[0]) - 'a') > ('z' - 'a')))
        return false;

    return out.concat(in, 2);
#else
    return false;
#endif
}

//------------------------------------------------------------------------------
bool get_drive(str_base& in_out)
{
#if defined(PLATFORM_WINDOWS)
    if ((in_out[1] != ':') || (unsigned(tolower(in_out[0]) - 'a') > ('z' - 'a')))
        return false;

    in_out.truncate(2);
    return (in_out.size() > 2);
#else
    return false;
#endif
}

//------------------------------------------------------------------------------
bool get_extension(const char* in, str_base& out)
{
    const char* dot = strrchr(in, '.');
    if (dot == nullptr)
        return false;

    return out.concat(dot);
}

//------------------------------------------------------------------------------
bool get_name(const char* in, str_base& out)
{
    return out.concat(get_name(in));
}

//------------------------------------------------------------------------------
const char* get_name(const char* in)
{
    if (const char* slash = get_last_separator(in))
        return slash + 1;

#if defined(PLATFORM_WINDOWS)
    if (in[0] && in[1] == ':')
        in += 2;
#endif

    return in;
}

//------------------------------------------------------------------------------
bool is_root(const char* path)
{
#if defined(PLATFORM_WINDOWS)
    // Windows' drives prefixes.
    // "X:" ?
    if (path[0] && path[1] == ':')
    {
        if (path[2] == '\0')
            return true;

        // "X:\" or "X://" ?
        if (path[3] == '\0' && is_separator(path[2]))
            return true;
    }
#endif

    // "[/ or /]+" ?
    while (is_separator(*path))
        ++path;

    return (*path == '\0');
}

//------------------------------------------------------------------------------
bool join(const char* lhs, const char* rhs, str_base& out)
{
    out << lhs;
    return append(out, rhs);
}

//------------------------------------------------------------------------------
bool append(str_base& out, const char* rhs)
{
    bool add_seperator = true;

    int last = int(out.length() - 1);
    if (last >= 0)
    {
        add_seperator &= !is_separator(out[last]);

#if defined(PLATFORM_WINDOWS)
        add_seperator &= !(out[1] == ':' && out[2] == '\0');
#endif
    }
    else
        add_seperator = false;

    if (add_seperator && !is_separator(rhs[0]))
        out << PATH_SEP;

    return out.concat(rhs);
}

}; // namespace path
