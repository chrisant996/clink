// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "clink_commit.h"

#if defined(RC_INVOKED)
#   define AS_STR(x)            AS_STR_IMPL(x)
#   define AS_STR_IMPL(x)       #x
#   define AS_LSTR(x)           AS_LSTR_IMPL(x)
#   define AS_LSTR_IMPL(x)      L#x
#endif

#define CLINK_VERSION_MAJOR     1
#define CLINK_VERSION_MINOR     4
#define CLINK_VERSION_PATCH     20

#ifdef _MSC_VER
#   undef CLINK_VERSION_STR
#   define CLINK_VERSION_STR    AS_STR(CLINK_VERSION_MAJOR) ## "." ##\
                                AS_STR(CLINK_VERSION_MINOR) ## "." ##\
                                AS_STR(CLINK_VERSION_PATCH) ## "." ##\
                                AS_STR(CLINK_COMMIT)
#   undef CLINK_VERSION_LSTR
#   define CLINK_VERSION_LSTR   AS_LSTR(CLINK_VERSION_MAJOR) ## L"." ##\
                                AS_LSTR(CLINK_VERSION_MINOR) ## L"." ##\
                                AS_LSTR(CLINK_VERSION_PATCH) ## L"." ##\
                                AS_LSTR(CLINK_COMMIT)
#endif


