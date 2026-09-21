#pragma once

#define VERSION_MAJOR           0
#define VERSION_MINOR           1
#define VERSION_PATCH           0

#define COPYRIGHT_STR           "Copyright (c) 2026 Christopher Antos"

#ifdef RC_INVOKED
#   define _MSC_VER
#endif

#ifdef _MSC_VER
#   define AS_STR(x)            AS_STR_IMPL(x)
#   define AS_STR_IMPL(x)       #x
#   define AS_LSTR(x)           AS_LSTR_IMPL(x)
#   define AS_LSTR_IMPL(x)      L#x
#endif

#ifdef AS_STR
#   undef VERSION_STR
#   define VERSION_STR          AS_STR(VERSION_MAJOR) ## "." ##\
                                AS_STR(VERSION_MINOR) ## "." ##\
                                AS_STR(VERSION_PATCH)
#endif

#ifdef AS_LSTR
#   undef VERSION_LSTR
#   define VERSION_LSTR         AS_LSTR(VERSION_MAJOR) ## L"." ##\
                                AS_LSTR(VERSION_MINOR) ## L"." ##\
                                AS_LSTR(VERSION_PATCH)
#endif
