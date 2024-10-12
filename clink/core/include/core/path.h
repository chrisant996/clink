// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;
class wstr_base;
class str_moveable;

//------------------------------------------------------------------------------
namespace path
{

void        refresh_pathext();

void        normalise(str_base& in_out, int32 sep=0);
void        normalise(char* in_out, int32 sep=0);
void        normalise_separators(str_base& in_out, int32 sep=0);
void        normalise_separators(char* in_out, int32 sep=0);
bool        is_separator(int32 c);
const char* next_element(const char* in);
bool        get_base_name(const char* in, str_base& out);
bool        get_directory(const char* in, str_base& out);
bool        get_directory(str_base& in_out);
bool        get_drive(const char* in, str_base& out);
bool        get_drive(str_base& in_out);
bool        get_extension(const char* in, str_base& out);
const char* get_extension(const char* in);
bool        get_name(const char* in, str_base& out);
bool        get_name(const wchar_t* in, wstr_base& out);
const char* get_name(const char* in);
const wchar_t* get_name(const wchar_t* in);
bool        tilde_expand(const char* in, str_base& out, bool use_appdata_local=false);
bool        tilde_expand(str_moveable& in_out, bool use_appdata_local=false);
bool        is_rooted(const char* path);
bool        is_root(const char* path);
bool        is_device(const char* path);
bool        is_unix_hidden(const char* in, bool Ignore_trailing_separators=false);
bool        join(const char* lhs, const char* rhs, str_base& out);
bool        append(str_base& out, const char* rhs);
void        maybe_strip_last_separator(str_base& out);
void        maybe_strip_last_separator(wstr_base& out);
bool        to_parent(str_base& out, str_base* child);
bool        is_unc(const char* path, const char** past_unc=nullptr);
bool        is_unc(const wchar_t* path, const wchar_t** past_unc=nullptr);
bool        is_incomplete_unc(const char* path);
bool        is_executable_extension(const char* in);

template<typename TYPE> static void skip_sep(const TYPE*& path)
{
    while (path::is_separator(*path))
        ++path;
}

template<typename TYPE> static uint32 past_ssqs(const TYPE* path)
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
    return uint32(p - path);
}

}; // namespace path
