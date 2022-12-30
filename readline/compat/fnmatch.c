// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include <stdio.h>

#include <compat/config.h>

#include "fnmatch.h"

//------------------------------------------------------------------------------
int fnmatch(const char *string, const char* pattern, int flags)
{
    return wildmatch(pattern, string, flags);
}
