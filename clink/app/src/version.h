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
#define CLINK_VERSION_MINOR     7
#define CLINK_VERSION_PATCH     6

#define ORIGINAL_COPYRIGHT_STR  "Copyright (c) 2012-2018 Martin Ridgers"
#define CLINK_COPYRIGHT_STR     "Copyright (c) 2012-2018 Martin Ridgers, Portions Copyright (c) 2020-2024 Christopher Antos"
#define PORTIONS_COPYRIGHT_STR  "Portions Copyright (c) 2020-2024 Christopher Antos"

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
#   undef CLINK_VERSION_STR_WITH_BRANCH
#   ifdef CLINK_BRANCH
#       define CLINK_VERSION_STR_WITH_BRANCH \
                                AS_STR(CLINK_VERSION_MAJOR) ## "." ##\
                                AS_STR(CLINK_VERSION_MINOR) ## "." ##\
                                AS_STR(CLINK_VERSION_PATCH) ## "." ##\
                                AS_STR(CLINK_COMMIT) ## "." ##\
                                AS_STR(CLINK_BRANCH)
#   else
#       define CLINK_VERSION_STR_WITH_BRANCH CLINK_VERSION_STR
#   endif
#endif

#define ENCODE_CLINK_VERSION(major, minor, patch) (major * 10000000 + minor * 10000 + patch)
#define CLINK_VERSION_ENCODED   ENCODE_CLINK_VERSION(CLINK_VERSION_MAJOR, CLINK_VERSION_MINOR, CLINK_VERSION_PATCH)

