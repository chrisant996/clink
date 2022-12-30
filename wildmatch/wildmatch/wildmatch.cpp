#include "wildmatch.hpp"
#include "wildmatch.c" // avoids a link-time dependency

namespace wild {

bool match(const char *pattern, const char *string, int flags)
{
    return wildmatch(pattern, string, flags) == WM_MATCH;
}

bool match(const std::string& pattern, const std::string& string, int flags)
{
    return wildmatch(pattern.c_str(), string.c_str(), flags) == WM_MATCH;
}

}
