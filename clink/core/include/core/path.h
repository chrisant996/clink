// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;
class wstr_base;

//------------------------------------------------------------------------------
namespace path
{

void        refresh_pathext();

void        normalise(str_base& in_out, int sep=0);
void        normalise(char* in_out, int sep=0);
bool        is_separator(int c);
const char* next_element(const char* in);
bool        get_base_name(const char* in, str_base& out);
bool        get_directory(const char* in, str_base& out);
bool        get_directory(str_base& in_out);
bool        get_drive(const char* in, str_base& out);
bool        get_drive(str_base& in_out);
bool        get_extension(const char* in, str_base& out);
const char* get_extension(const char* in);
bool        get_name(const char* in, str_base& out);
const char* get_name(const char* in);
bool        is_rooted(const char* path);
bool        is_root(const char* path);
bool        join(const char* lhs, const char* rhs, str_base& out);
bool        append(str_base& out, const char* rhs);
void        maybe_strip_last_separator(str_base& out);
void        maybe_strip_last_separator(wstr_base& out);
bool        to_parent(str_base& out, str_base* child);
bool        is_incomplete_unc(const char* path);
bool        is_executable_extension(const char* in);

}; // namespace path
