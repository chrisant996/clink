// Copyright (c) 2020-2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class auto_free_str
{
public:
                        auto_free_str() = default;
                        auto_free_str(const char* s, int32 len) { set(s, len); }
                        auto_free_str(auto_free_str&& other) : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
                        ~auto_free_str();

    auto_free_str&      operator=(const char* s) { set(s); return *this; }
    auto_free_str&      operator=(auto_free_str&& other);
    void                set(const char* s, int32 len = -1);
    const char*         get() const { return m_ptr; }

private:
    char*               m_ptr = nullptr;
};
