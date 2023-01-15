// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include <stdio.h>

#include <compat/config.h>

#include "fnmatch.h"

//------------------------------------------------------------------------------
int fnmatch(const char *pattern, const char *string, int flags)
{
    return wildmatch(pattern, string, flags);
}
