// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

//------------------------------------------------------------------------------
namespace path
{

void        abs_path(const char* in, str_base& out, const char* root);
void        clean(str_base& in_out, int sep=0);
void        clean(char* in_out, int sep=0);
bool        is_separator(int c);
const char* next_element(const char* in);
bool        get_base_name(const char* in, str_base& out);
bool        get_directory(const char* in, str_base& out);
bool        get_directory(str_base& in_out);
bool        get_drive(const char* in, str_base& out);
bool        get_drive(str_base& in_out);
bool        get_extension(const char* in, str_base& out);
bool        get_name(const char* in, str_base& out);
const char* get_name(const char* in);
bool        is_rooted(const char* path);
bool        is_root(const char* path);
bool        join(const char* lhs, const char* rhs, str_base& out);
bool        append(str_base& out, const char* rhs);

}; // namespace path
