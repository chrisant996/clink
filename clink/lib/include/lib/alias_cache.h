// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>
#include <core/str_map.h>
#include <core/auto_free_str.h>
#include <core/linear_allocator.h>

//------------------------------------------------------------------------------
class alias_cache
{
public:
    alias_cache() : m_names(4096) {}
    void clear();
    bool get_alias(const char* name, str_base& out);
private:
    str_map_caseless<auto_free_str>::type m_map;
    linear_allocator m_names;
};
