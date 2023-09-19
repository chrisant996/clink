// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>
#include <core/str.h>

//------------------------------------------------------------------------------
class history_timeformatter
{
public:
                    history_timeformatter();
                    ~history_timeformatter();
    void            set_timeformat(const char* timeformat, bool for_popup=false);
    uint32          max_timelen();
    void            format(time_t time, str_base& out);
private:
    void            ensure_timeformat();
    str_moveable    m_timeformat;
    uint32          m_max_timelen = 0;
    bool            m_for_popup = false;
};
