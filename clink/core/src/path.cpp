// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "base.h"
#include "path.h"
#include "os.h"
#include "str.h"
#include "str_tokeniser.h"

#include <string>
#include <map>

#include <Shlobj.h>

//------------------------------------------------------------------------------
struct ext_comparer
{
    bool operator()(const std::wstring& a, const std::wstring& b) const
    {
        return (CompareStringW(LOCALE_USER_DEFAULT,
                               NORM_IGNORECASE|NORM_LINGUISTIC_CASING,
                               a.c_str(), int(a.length()),
                               b.c_str(), int(b.length())) == CSTR_LESS_THAN);
    }
};

//------------------------------------------------------------------------------
static bool s_have_pathexts = false;
static std::map<std::wstring, bool, ext_comparer> s_pathexts;

//------------------------------------------------------------------------------
template<typename TYPE> static unsigned int past_unc(const TYPE* path)
{
    unsigned int start = 2;
    while (path[start] && !path::is_separator(path[start]))
        start++;
    while (path[start] && path::is_separator(path[start]))
        start++;
    while (path[start] && !path::is_separator(path[start]))
        start++;
    return start;
}

//------------------------------------------------------------------------------
static const char* get_last_separator(const char* in)
{
    for (int i = int(strlen(in)) - 1; i >= 0; --i)
        if (path::is_separator(in[i]))
            return in + i;

    return nullptr;
}

//------------------------------------------------------------------------------
static const wchar_t* get_last_separator(const wchar_t* in)
{
    for (int i = int(wcslen(in)) - 1; i >= 0; --i)
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
        // Same for Windows and its drive prefixes and UNC paths.
        if (path[0] && path[1] == ':' && slash == path + 2)
            ++slash;
        else if (path::is_separator(path[0]) && path::is_separator(path[1]))
            slash = max(slash, path + past_unc(path));
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
void refresh_pathext()
{
    s_have_pathexts = false;
    s_pathexts.clear();
}



//------------------------------------------------------------------------------
void normalise(str_base& in_out, int sep)
{
    normalise(in_out.data(), sep);
}

//------------------------------------------------------------------------------
void normalise(char* in_out, int sep)
{
    if (!sep)
        sep = PATH_SEP[0];

    bool test_unc = true;
#if defined(PLATFORM_WINDOWS)
    if (in_out[0] && in_out[1] == ':')
    {
        in_out += 2;
        test_unc = false;
    }
#endif

    unsigned int piece_count = 0;

    char* __restrict write = in_out;
    if (is_separator(*write))
    {
        *write++ = char(sep);
        piece_count = INT_MAX;

        // UNC.
        if (test_unc && is_separator(*write))
            *write++ = char(sep);
    }

    const char* __restrict start = write;
    const char* __restrict read = write;
    for (; const char* __restrict next = next_element(read); read = next)
    {
        while (is_separator(*read))
            ++read;

        if (read[0] == '.')
        {
            bool two_dot = (read[1] == '.');

            char c = *(read + 1 + two_dot);
            if (is_separator(c) || !c)
            {
                if (!two_dot)
                    continue;

                if (piece_count)
                {
                    while (write > start)
                    {
                        --write;
                        if (is_separator(write[-1]))
                            break;
                    }

                    piece_count -= !!piece_count;
                    continue;
                }
            }
        }
        else
            ++piece_count;

        for (; read < next; ++read)
            *write++ = is_separator(*read) ? char(sep) : *read;
    }

    *write = '\0';
}

//------------------------------------------------------------------------------
void normalise_separators(str_base& in_out, int sep)
{
    normalise_separators(in_out.data(), sep);
}

//------------------------------------------------------------------------------
void normalise_separators(char* in_out, int sep)
{
    if (!sep)
        sep = PATH_SEP[0];

    for (char* next = in_out; *next; next++)
        if (is_separator(*next) && *next != sep)
            *next = sep;
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
    if (*in == '\0')
        return nullptr;

    for (; is_separator(*in); ++in);
    for (; *in && !is_separator(*in); ++in);
    return in + !!*in;
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
    const char* ext = get_extension(in);
    if (!ext)
        return false;
    return out.concat(get_extension(in));
}

//------------------------------------------------------------------------------
const char* get_extension(const char* in)
{
    const char* ext = nullptr;
    while (*in)
    {
        switch (*in)
        {
        case '.':
            ext = in;
            break;
        case ' ':
        case '/':
        case '\\':
        case ':':
            ext = nullptr;
            break;
        }

        in++;
    }

    if (ext && ext[1] == '\0')
        ext = nullptr;

    return ext;
}

//------------------------------------------------------------------------------
bool get_name(const char* in, str_base& out)
{
    return out.concat(get_name(in));
}

//------------------------------------------------------------------------------
bool get_name(const wchar_t* in, wstr_base& out)
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
const wchar_t* get_name(const wchar_t* in)
{
    if (const wchar_t* slash = get_last_separator(in))
        return slash + 1;

#if defined(PLATFORM_WINDOWS)
    if (in[0] && in[1] == ':')
        in += 2;
#endif

    return in;
}

//------------------------------------------------------------------------------
bool tilde_expand(const char* in, str_base& out, bool use_appdata_local)
{
    if (!in || in[0] != '~' || (in[1] && !is_separator(in[1])))
    {
        out = in;
        return false;
    }

    if (use_appdata_local)
    {
        wstr<280> wlocal;
        if (SHGetFolderPathW(0, CSIDL_LOCAL_APPDATA, nullptr, 0, wlocal.data()) == S_OK)
        {
            to_utf8(out, wlocal.c_str());
        }
        else
        {
            os::get_env("HOME", out);
            path::append(out, "AppData\\Local");
        }
    }
    else
    {
        os::get_env("HOME", out);
    }

    in++;
    while (path::is_separator(*in))
        ++in;

    path::append(out, in);
    path::normalise(out);
    return true;
}

//------------------------------------------------------------------------------
bool tilde_expand(str_moveable& in_out, bool use_appdata_local)
{
    const char* in = in_out.c_str();
    if (!in || in[0] != '~' || (in[1] && !is_separator(in[1])))
        return false;

    str_moveable expanded;
    if (!tilde_expand(in, expanded, use_appdata_local))
        return false;

    in_out = std::move(expanded);
    return true;
}

//------------------------------------------------------------------------------
bool is_rooted(const char* path)
{
#if defined(PLATFORM_WINDOWS)
    if (path[0] && path[1] == ':')
        path += 2;
#endif

    return is_separator(*path);
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
    if (out.c_str() != lhs)
        out = lhs;
    return append(out, rhs);
}

//------------------------------------------------------------------------------
bool append(str_base& out, const char* rhs)
{
    if (is_rooted(rhs))
        return out.copy(rhs);

    bool add_separator = true;

    int last = int(out.length() - 1);
    if (last >= 0)
    {
        add_separator &= !is_separator(out[last]);

#if defined(PLATFORM_WINDOWS)
        add_separator &= !(isalpha((unsigned char)out[0]) && out[1] == ':' && out[2] == '\0');
#endif
    }
    else
        add_separator = false;

    if (add_separator && !is_separator(rhs[0]))
        out << PATH_SEP;

    return out.concat(rhs);
}

//------------------------------------------------------------------------------
// Strips a run of trailing separators.  Doesn't strip separator after drive
// letter or UNC root, and doesn't strip an initial (root) separator.
void maybe_strip_last_separator(str_base& out)
{
    unsigned int start = 0;

    if (isalpha((unsigned char)out[0]) && out[1] == ':')
        start += 2;
    else if (out[0] == '\\' && out[1] == '\\')
        start += 2;

    if (is_separator(out[start]))
        start++;

    while (out.length() > start && is_separator(out[out.length() - 1]))
        out.truncate(out.length() - 1);
}
void maybe_strip_last_separator(wstr_base& out)
{
    unsigned int start = 0;

    if (is_separator(out[0]) && is_separator(out[1]))
        start = past_unc(out.c_str());
    else
    {
        if (iswalpha(out[0]) && out[1] == ':')
            start += 2;
        if (is_separator(out[start]))
            start++;
    }

    while (out.length() > start && is_separator(out[out.length() - 1]))
        out.truncate(out.length() - 1);
}

//------------------------------------------------------------------------------
// Strips the last path component, and optionally returns it in child.  Returns
// non-zero if out changed, or zero if out didn't change.
bool to_parent(str_base& out, str_base* child)
{
    unsigned int start = 0;
    unsigned int end = out.length();
    unsigned int orig_len = out.length();

    if (is_separator(out[0]) && is_separator(out[1]))
        start = past_unc(out.c_str());
    else
    {
        if (isalpha((unsigned char)out[0]) && out[1] == ':')
            start += 2;
        if (is_separator(out[start]))
            start++;
    }

    while (end > 0 && is_separator(out[end - 1]))
        end--;
    int child_end = end;
    while (end > 0 && !is_separator(out[end - 1]))
        end--;

    if (end < start)
        end = start;

    if (child)
    {
        child->clear();
        child->concat(out.c_str() + end, child_end - end);
    }

    while (end > start && is_separator(out[end - 1]))
        end--;

    out.truncate(end);
    return (out.length() != orig_len);
}

//------------------------------------------------------------------------------
bool is_incomplete_unc(const char* path)
{
    // If it doesn't start with "\\" then it isn't a UNC path.
    if (!is_separator(path[0]) || !is_separator(path[1]))
        return false;
    while (is_separator(*path))
        path++;

    // Server name.
    if (isspace((unsigned char)*path))
        return true;
    while (*path && !is_separator(*path))
        path++;

    // Separator after server name.
    if (!is_separator(*path))
        return true;
    while (is_separator(*path))
        path++;

    // Share name.
    if (isspace((unsigned char)*path))
        return true;
    while (*path && !is_separator(*path))
        path++;

    // Separator after share name.
    if (!is_separator(*path))
        return true;

    // The path is at least "\\x\y\", so it's a complete enough UNC path for
    // file system APIs to succeed.
    return false;
}

//------------------------------------------------------------------------------
bool is_executable_extension(const char* in)
{
    const char* ext = get_extension(in);
    if (!ext)
        return false;

    if (!s_have_pathexts)
    {
        str<> pathext;
        if (!os::get_env("pathext", pathext))
            return false;

        str_tokeniser tokens(pathext.c_str(), ";");
        const char *start;
        int length;

        wstr<> wtoken;
        while (str_token token = tokens.next(start, length))
        {
            str_iter rhs(start, length);
            wtoken.clear();
            to_utf16(wtoken, rhs);

            s_pathexts.emplace(wtoken.c_str(), true);
        }

        s_have_pathexts = true;
    }

    wstr<> wext(ext);
    if (s_pathexts.find(wext.c_str()) != s_pathexts.end())
        return true;

    return false;
}

}; // namespace path
