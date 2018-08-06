// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#if defined(RC_INVOKED)
#   define AS_STR(x)        AS_STR_IMPL(x)
#   define AS_STR_IMPL(x)   #x
#endif

#define CLINK_VERSION_MAJOR 1
#define CLINK_VERSION_MINOR 0
#define CLINK_VERSION_PATCH 0
#define CLINK_VERSION_STR   AS_STR(CLINK_VERSION_MAJOR) "."\
                            AS_STR(CLINK_VERSION_MINOR) "."\
                            AS_STR(CLINK_VERSION_PATCH)\
                            "a1"\
                            "." AS_STR(CLINK_COMMIT)
