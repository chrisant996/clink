// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

//------------------------------------------------------------------------------
class fs_fixture
{
public:
                    fs_fixture(const char** fs=nullptr);
                    ~fs_fixture();
    const char*     get_root() const;

private:
    void            clean(const char* path);
    str<>           m_root;
    const char**    m_fs;
};
