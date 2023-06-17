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
intercept_result intercept_directory(const char* line, str_base* out=nullptr, bool only_cd_chdir=false);
