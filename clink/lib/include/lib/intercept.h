// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

//------------------------------------------------------------------------------
enum class intercept_result : uint8
{
    none,
    prev_dir,
    chdir,
};

//------------------------------------------------------------------------------
enum class intercept_mode : uint8
{
    normal,
    only_cd_chdir,
    no_remote,                  // Avoids network IO for remote paths.
};

//------------------------------------------------------------------------------
intercept_result intercept_directory(const char* line, str_base* out=nullptr, intercept_mode mode=intercept_mode::normal);
void make_cd_command(const char* dir, str_base& out);
