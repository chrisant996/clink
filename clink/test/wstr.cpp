// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include <core/str.h>

#define str         wstr
#define STR(x)      L##x
#define NAME_SUFFIX " (wchar_t)"
#include "str.cpp"
#undef str
