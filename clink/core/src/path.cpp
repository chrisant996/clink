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
static bool is_seperator(int c)
{
#if defined(PLATFORM_WINDOWS)
    return (c == '/' || c == '\\');
#else
    return (c == '/');
#endif
}

//------------------------------------------------------------------------------
static const char* get_last_separator(const char* in)
{
#if defined(PLATFORM_WINDOWS)
    return max(strrchr(in, '/'), strrchr(in, '\\'));
#else
    return strrchr(in, '/');
#endif
}



//------------------------------------------------------------------------------
void path::clean(str_base& in_out, int sep)
{
    path::clean(in_out.data(), sep);
}

//------------------------------------------------------------------------------
void path::clean(char* in_out, int sep)
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
            if (is_seperator(c) || c == sep)
            {
                c = sep;
                state = state_slash;
            }

            *write = c;
            ++write;
            break;

        case state_slash:
            if (!is_seperator(c) && c != sep)
            {
                state = state_write;
                continue;
            }
            break;
        }

        ++read;
    }

    // No trailing slash unless it roots the path.
    if (state == state_slash && write > (in_out + 1))
        --write;

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
    int end = get_directory_end(in);
    return out.concat(in, end);
}

//------------------------------------------------------------------------------
bool path::get_directory(str_base& in_out)
{
    int end = get_directory_end(in_out.c_str());
    in_out.truncate(end);
    return true;
}

//------------------------------------------------------------------------------
int path::get_directory_end(const char* path)
{
    if (const char* slash = get_last_separator(path))
    {
        // Last slash isn't used to find end of the directory.
        if (slash[1] == '\0')
            for (int i = 0; i < 2; ++i)
                while (slash > path)
                    if (is_seperator(*--slash) != (i == 0))
                        break;

        // Trim consecutive slashes unless they're leading ones.
        const char* first_slash = slash;
        while (first_slash >= path)
        {
            if (!is_seperator(*first_slash))
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

//------------------------------------------------------------------------------
bool path::get_drive(const char* in, str_base& out)
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
bool path::get_drive(str_base& in_out)
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
    return out.concat(get_name(in));
}

//------------------------------------------------------------------------------
const char* path::get_name(const char* in)
{
    if (const char* slash = get_last_separator(in))
    {
        // Like get_directory() a trailing slash is not considered a delimiter.
        if (slash[1] != '\0')
            return slash + 1;

        // Two passes; 1st to skip seperators, 2nd to skip word.
        int pass = 0;
        for (pass = 0; pass < 2 && slash > in; ++pass)
            while (slash > in)
                if (is_seperator(*--slash) != (pass == 0))
                    break;

        // If the two passes didn't complete 'in' only contains seperators
        if (pass < 2)
            return in;

        return slash + is_seperator(*slash);
    }

#if defined(PLATFORM_WINDOWS)
    if (in[0] && in[1] == ':')
        in += 2;
#endif

    return in;
}

//------------------------------------------------------------------------------
bool path::is_root(const char* path)
{
#if defined(PLATFORM_WINDOWS)
    // Windows' drives prefixes.
    // "X:" ?
    if (path[0] && path[1] == ':')
    {
        if (path[2] == '\0')
            return true;

        // "X:\" or "X://" ?
        if (path[3] == '\0' && is_seperator(path[2]))
            return true;
    }
#endif

    // "[/ or /]+" ?
    while (is_seperator(*path))
        ++path;

    return (*path == '\0');
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
    bool add_seperator = true;

    int last = int(out.length() - 1);
    if (last >= 0)
    {
        add_seperator &= !is_seperator(out[last]);

#if defined(PLATFORM_WINDOWS)
        add_seperator &= !(out[1] == ':' && out[2] == '\0');
#endif
    }
    else
        add_seperator = false;

    if (add_seperator && !is_seperator(rhs[0]))
        out << PATH_SEP;

    return out.concat(rhs);
}
