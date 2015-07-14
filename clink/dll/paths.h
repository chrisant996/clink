// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

void get_config_dir(str_base& buffer);
void get_log_dir(str_base& buffer);
void get_dll_dir(str_base& buffer);
void set_config_dir_override(const char* path);
