// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#define ANSI_X_COMPILE

//------------------------------------------------------------------------------

#define char_t          char
#define ANSI_FNAME(x)   x
#define ansi_str_len(x) strlen(x)

#include "ansi.x"

#undef ansi_str_len
#undef char_t
#undef ANSI_FNAME

//------------------------------------------------------------------------------

#define char_t          wchar_t
#define ANSI_FNAME(x)   x##_w
#define ansi_str_len(x) wcslen(x)

#include "ansi.x"

#undef ansi_str_len
#undef char_t
#undef ANSI_FNAME

#undef ANSI_X_COMPILE
