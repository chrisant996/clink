#pragma once

#include <string>

namespace wild {

/* Flags */
constexpr int FNMATCH = 0x00; /* Zero flags for fnmatch(3) behavior. */
constexpr int NOESCAPE = 0x01; /* Disable backslash escaping. */
constexpr int PATHNAME = 0x02; /* Slash must be matched by slash. */
constexpr int PERIOD = 0x04; /* Period must be matched by period. */
constexpr int LEADING_DIR = 0x08; /* Ignore /<tail> after Imatch. */
constexpr int CASEFOLD = 0x10; /* Case insensitive search. */
constexpr int WILDSTAR = 0x40; /* Double-asterisks "**" matches slash too. */
/* WILDSTAR implies PATHNAME so that single-asterisks "*" can be used for
 * matching within path components.
 */

/*
 * wild::WILDSTAR is the default value for `flags` when calling
 * wild::match(..).  The C++ API enables the extended "**" syntax by default.
 * This can be disabled or extended by specifying an explicit value for `flags`.
 *
 * For example, to get wildstar extended syntax plus case-insensitive
 * matching the flag values can be OR-d together, e.g.
 *
 *  if (wild::match(str, pattern, wild::WILDSTAR | wild::CASEFOLD)) {
 *      // matched
 *  }
 */
bool match(const char *pattern, const char *string, int flags=WILDSTAR);
bool match(const std::string& pattern, const std::string& string, int flags=WILDSTAR);

} /* wild namespace */
