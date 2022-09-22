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
template<typename TYPE> static void skip_sep(const TYPE*& path)
{
    while (path::is_separator(*path))
        ++path;
}

//------------------------------------------------------------------------------
template<typename TYPE> static void skip_sep(TYPE*& path)
{
    while (path::is_separator(*path))
        ++path;
}

//------------------------------------------------------------------------------
template<typename TYPE> static void skip_nonsep(const TYPE*& path)
{
    while (*path && !path::is_separator(*path))
        ++path;
}

//------------------------------------------------------------------------------
template<typename TYPE> static unsigned int past_ssqs(const TYPE* path)
{
    const TYPE* p = path;
    if (!path::is_separator(*(p++)))
        return 0;
    if (!path::is_separator(*(p++)))
        return 0;
    if (*(p++) != '?')
        return 0;
    if (!path::is_separator(*(p++)))
        return 0;
    skip_sep(p);
    return static_cast<unsigned int>(p - path);
}

//------------------------------------------------------------------------------
template<typename TYPE> static const TYPE* past_drive(const TYPE* path)
{
    if (!path::is_unc(path, &path))
    {
        path += past_ssqs(path);
        if (iswalpha(static_cast<unsigned int>(path[0])) && path[1] == ':')
            path += 2;
    }
    return path;
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
    const char* p = path;

#if defined(PLATFORM_WINDOWS)
    // Windows and its drive prefixes and UNC paths.
    p = past_drive(p);
#endif

    // Don't strip '/' if it's the first char.
    if ((p == path || p[-1] == ':') && path::is_separator(*p))
        ++p;

    const char* slash = get_last_separator(p);
    if (!slash)
        return int(p - path);

    // Trim consecutive slashes unless they're leading ones.
    const char* first_slash = slash;
    while (first_slash >= p)
    {
        if (!path::is_separator(*first_slash))
            break;

        --first_slash;
    }
    ++first_slash;

    if (first_slash != p)
        slash = first_slash;

    return int(slash - path);
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
#if defined(__MINGW32__) || defined(__MINGW64__)
# define RESTRICT
#else
# define RESTRICT __restrict
#endif
void normalise(char* in_out, int sep)
{
    if (!sep)
        sep = PATH_SEP[0];

    bool dots = true;
    bool eat_extra_dots = false;
    bool is_unc = path::is_unc(in_out);

#if defined(PLATFORM_WINDOWS)
    if (is_separator(in_out[0]) && is_separator(in_out[1]) && in_out[2] == '.' && is_separator(in_out[3]))
        dots = false; // Device namespace does not normalize . or .. in paths.
    else
    {
        const char* past = past_drive(in_out);
        eat_extra_dots = (past > in_out && is_separator(in_out[0]) && is_separator(in_out[1]) && in_out[2] == '?');
        if (past >= in_out + 1 && is_separator(in_out[0]) && is_separator(in_out[1]))
            in_out += 2; // Preserve the 2 leading separators.
        char* RESTRICT write = in_out;
        while (in_out < past)
        {
            char c = *in_out;
            *write = c;
            if (is_separator(c))
                skip_sep(in_out);
            else
                ++in_out;
            ++write;
        }
    }
#endif

    // BUGBUG:  Two reasonable perspectives:
    //
    // 1.  This should normalize . and .. even in \\?\ paths.
    // 2.  This should not normalize . or .. in \\?\ paths.
    //
    // Maybe there should be a parameter, so that the caller can choose.

    unsigned int piece_count = 0;

    char* RESTRICT write = in_out;
    int unc_offset = 0;
    if (is_separator(*write))
    {
        *write++ = char(sep);
        piece_count = INT_MAX;
        if (is_unc)
            ++unc_offset;
    }

    const char* const RESTRICT start = write - unc_offset;
    const char* RESTRICT read = write;
    for (; const char* RESTRICT next = next_element(read); read = next)
    {
        skip_sep(read);

        if (read[0] == '.' && dots)
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

                    --piece_count;
                    continue;
                }
                else if (eat_extra_dots)
                {
                    continue;
                }
            }
        }
        else
            ++piece_count;

        if (is_unc && write == start)
            *write++ = char(sep);

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

    skip_sep(in);
    skip_nonsep(in);
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
    // Advance past \\?\ if present.
    in += past_ssqs(in);

    // If not 'X:' then there's no drive.
    if ((in[1] != ':') || (unsigned(tolower(in[0]) - 'a') > ('z' - 'a')))
        return false;

    // Return the drive.
    const char c = in[0];
    out.clear();
    return out.format("%c:", c);
#else
    return false;
#endif
}

//------------------------------------------------------------------------------
bool get_drive(str_base& in_out)
{
    return get_drive(in_out.c_str(), in_out);
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
template<typename T>
const T* get_name(const T* in)
{
#if defined(PLATFORM_WINDOWS)
    // Skip UNC root, \\?\ prefix, and/or drive letter.  This is important so
    // that they are not misinterpreted as a file name.
    in = past_drive(in);
#endif

    if (const T* slash = get_last_separator(in))
        return slash + 1;

    return in;
}

//------------------------------------------------------------------------------
const char* get_name(const char* in)
{
    return get_name<char>(in);
}

//------------------------------------------------------------------------------
const wchar_t* get_name(const wchar_t* in)
{
    return get_name<wchar_t>(in);
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
    skip_sep(in);

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
static bool find_root(const char* path, const char*& child)
{
#if defined(PLATFORM_WINDOWS)
    // Windows' drives prefixes.
    // "X:" or UNC root?
    path = past_drive(path);
#endif

    child = path;

    return !*path || is_separator(*path);
}

//------------------------------------------------------------------------------
// is_rooted means it is a root plus at least a path separator.
//
// QUIRK:  \\foo and \\foo\ and \\?\UNC\foo and \\?\UNC\foo\ are reported as
// rooted, because saying they're not would create path parsing problems.
bool is_rooted(const char* path)
{
    const char* child;
    return find_root(path, child) && is_separator(*child);
}

//------------------------------------------------------------------------------
// is_root means cannot subdivide the string, i.e. cannot move up the hierarchy.
bool is_root(const char* path)
{
    if (!find_root(path, path))
        return false;

    // "[/ or \]+" ?
    skip_sep(path);

    return (*path == '\0');
}

//------------------------------------------------------------------------------
bool is_device(const char* _path)
{
    if (is_separator(_path[0]) && is_separator(_path[1]) && _path[2] == '.')
        return (!_path[3] || is_separator(_path[3]));

    str<> path(_path);
    str<> child;
    while (path::to_parent(path, &child))
    {
        char* truncate = strpbrk(child.data(), ":.");
        if (truncate)
            child.truncate(static_cast<unsigned int>(truncate - child.c_str()));

        const char* name = child.c_str();
        while (*name == ' ')
            name++;

        static const char* const c_devices[] = { "nul", "aux", "con" };
        static const char* const c_devicesDigit[] = { "com", "lpt" };
        for (const char* device : c_devices)
        {
            if (!_stricmp(name, device))
                return true;
        }
        for (const char* device : c_devicesDigit)
        {
            if (!_strnicmp(name, device, 3) && name[3] >= '0' && name[3] <= '9' && !name[4])
                return true;
        }
    }

    return false;
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
        const char* in = out.c_str();
        if (!is_unc(in, &in))
        {
            in += past_ssqs(in);
            add_separator &= !(isalpha((unsigned char)in[0]) && in[1] == ':' && in[2] == '\0');
        }
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
    unsigned int start = static_cast<unsigned int>(past_drive(out.c_str()) - out.c_str());

    if (is_separator(out[start]))
        start++;

    while (out.length() > start && is_separator(out[out.length() - 1]))
        out.truncate(out.length() - 1);
}
void maybe_strip_last_separator(wstr_base& out)
{
    unsigned int start = static_cast<unsigned int>(past_drive(out.c_str()) - out.c_str());

    if (is_separator(out[start]))
        ++start;

    while (out.length() > start && is_separator(out[out.length() - 1]))
        out.truncate(out.length() - 1);
}

//------------------------------------------------------------------------------
// Strips the last path component, and optionally returns it in child.  Returns
// non-zero if out changed, or zero if out didn't change.
bool to_parent(str_base& out, str_base* child)
{
    unsigned int orig_len = out.length();

    // Find end of drive or UNC root plus separator(s).
    unsigned int start = static_cast<unsigned int>(past_drive(out.c_str()) - out.c_str());
    if (start && out[start - 1] == ':')
    {
        while (is_separator(out[start]))
            ++start;
    }

    // Trim separators at the end.
    unsigned int end = out.length();
    while (end > 0 && is_separator(out[end - 1]))
        end--;

    // Trim the last path component.
    int child_end = end;
    while (end > 0 && !is_separator(out[end - 1]))
        end--;
    if (end < start)
        end = start;

    // Return the last path component.
    if (child)
    {
        child->clear();
        child->concat(out.c_str() + end, child_end - end);
    }

    // Trim trailing separators.
    while (end > start && is_separator(out[end - 1]))
        end--;

    out.truncate(end);
    return (out.length() != orig_len);
}

//------------------------------------------------------------------------------
// Optional out parameter past_unc points just after the UNC server\share part
// (and does not include a separator past that).
template<typename T>
bool is_unc(const T* path, const T** past_unc)
{
    if (!is_separator(path[0]) || !is_separator(path[1]))
        return false;

    const T* const in = path;
    skip_sep(path);
    unsigned int leading = static_cast<unsigned int>(path - in);

    // Check for device namespace.
    if (path[0] == '.' && (!path[1] || is_separator(path[1])))
        return false;

    // Check for \\?\UNC\ namespace.
    if (leading == 2 && path[0] == '?' && is_separator(path[1]))
    {
        ++path;
        skip_sep(path);

        if (*path != 'U' && *path != 'u')
            return false;
        ++path;
        if (*path != 'N' && *path != 'n')
            return false;
        ++path;
        if (*path != 'C' && *path != 'c')
            return false;
        ++path;

        if (!is_separator(*path))
            return false;
        skip_sep(path);
    }

    if (past_unc)
    {
        // Skip server name.
        skip_nonsep(path);
        while (*path && !is_separator(*path))
            ++path;

        // Skip separator.
        skip_sep(path);

        // Skip share name.
        skip_nonsep(path);

        *past_unc = path;
    }
    return true;
}

//------------------------------------------------------------------------------
bool is_unc(const char* path, const char** past_unc)
{
    return is_unc<char>(path, past_unc);
}

//------------------------------------------------------------------------------
bool is_unc(const wchar_t* path, const wchar_t** past_unc)
{
    return is_unc<wchar_t>(path, past_unc);
}

//------------------------------------------------------------------------------
bool is_incomplete_unc(const char* path)
{
    // If it doesn't start with "\\" then it isn't a UNC path.
    if (!is_separator(path[0]) || !is_separator(path[1]))
        return false;

    // Maybe \\?\UNC\.
    if (path[2] == '?')
    {
        if (!is_separator(path[3]))
            return false;
        path += 4;
        skip_sep(path);
        if (_strnicmp(path, "UNC", 3) != 0 || !is_separator(path[3]))
            return false;
        path += 4;
    }

    skip_sep(path);

    // Server name.
    if (isspace((unsigned char)*path))
        return true;
    skip_nonsep(path);

    // Separator after server name.
    if (!is_separator(*path))
        return true;
    skip_sep(path);

    // Share name.
    if (isspace((unsigned char)*path))
        return true;
    skip_nonsep(path);

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
