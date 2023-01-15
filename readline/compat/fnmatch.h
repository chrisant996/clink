// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <wildmatch.h>

#define FNM_MATCH 0 /* Match. */
#define FNM_NOMATCH 1 /* Match failed. */

#define FNM_NOESCAPE 0x01 /* Disable backslash escaping. */
#define FNM_PATHNAME 0x02 /* Slash must be matched by slash. */
#define FNM_PERIOD 0x04 /* Period must be matched by period. */
#define FNM_LEADING_DIR 0x08 /* Ignore /<tail> after Imatch. */
#define FNM_CASEFOLD 0x10 /* Case insensitive search. */

#define FNM_IGNORECASE FNM_CASEFOLD
#define FNM_FILE_NAME FNM_PATHNAME

int fnmatch(const char *string, const char *pattern, int flags);
