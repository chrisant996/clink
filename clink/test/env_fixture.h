// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class env_fixture
{
public:
                    env_fixture(const char** env);
                    ~env_fixture();

protected:
    void            clear();
    wchar_t*        m_env_strings;
};
