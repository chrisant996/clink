// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "alias_cache.h"

#include <core/os.h>

//------------------------------------------------------------------------------
void alias_cache::clear()
{
    m_map.clear();
    m_names.clear();
}

//------------------------------------------------------------------------------
bool alias_cache::get_alias(const char* name, str_base& out)
{
    const auto& iter = m_map.find(name);
    if (iter != m_map.end())
    {
        const char* value = iter->second.get();
        if (!value || !*value)
            return false;
        out = value;
        return true;
    }

    const bool exists = os::get_alias(name, out);

    const char* cache_name = m_names.store(name);
    auto_free_str cache_value(out.c_str(), out.length());
    m_map.emplace(cache_name, std::move(cache_value));

    return exists;
}
