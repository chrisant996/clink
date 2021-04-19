// Copyright (c) 2020-2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <map>

//------------------------------------------------------------------------------
struct cmp_str_case
{
    bool operator()(const char* a, const char* b) const
    {
        return strcmp(a, b) < 0;
    }
};

//------------------------------------------------------------------------------
struct cmp_str_caseless
{
    bool operator()(const char* a, const char* b) const
    {
        return stricmp(a, b) < 0;
    }
};

//------------------------------------------------------------------------------
// This C++11 syntax w/VS2019 causes clink_test_x64.exe to crash during startup.
//template<typename V> using str_map_case = std::map<const char*, V, cmp_str_case>;
//template<typename V> using str_map_caseless = std::map<const char*, V, cmp_str_caseless>;

//------------------------------------------------------------------------------
// Alternative form using C++03.
template<typename V> struct str_map_case
{
    typedef std::map<const char*, V, cmp_str_case> type;
};
template<typename V> struct str_map_caseless
{
    typedef std::map<const char*, V, cmp_str_caseless> type;
};
