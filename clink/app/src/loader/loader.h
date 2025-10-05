// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

void get_profile_path(const char* in, str_base& out);
void load_settings_and_inputrc(str_base* settings_file=nullptr);
