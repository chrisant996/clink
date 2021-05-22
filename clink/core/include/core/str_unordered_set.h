// Copyright (c) 2020-2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str_hash.h"
#include <unordered_set>

//------------------------------------------------------------------------------
struct match_hasher
{
    size_t operator()(const char* match) const
    {
        return str_hash(match);
    }
    size_t operator()(const wchar_t* match) const
    {
        return wstr_hash(match);
    }
};

//------------------------------------------------------------------------------
struct match_comparator
{
    bool operator()(const char* m1, const char* m2) const
    {
        return strcmp(m1, m2) == 0;
    }
    bool operator()(const wchar_t* m1, const wchar_t* m2) const
    {
        return wcscmp(m1, m2) == 0;
    }
};

//------------------------------------------------------------------------------
typedef std::unordered_set<const char*, match_hasher, match_comparator> str_unordered_set;
typedef std::unordered_set<const wchar_t*, match_hasher, match_comparator> wstr_unordered_set;
