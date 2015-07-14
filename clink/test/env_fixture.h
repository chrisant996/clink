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
    void            convert_eq_to_null(wchar_t* env_strings);
    void            clear();
    wchar_t*        m_env_strings;
};
