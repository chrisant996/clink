// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "terminal/input_idle.h"

class lua_state;

//------------------------------------------------------------------------------
class lua_input_idle
    : public input_idle
{
public:
                    lua_input_idle(lua_state& state);
                    ~lua_input_idle();
    void            reset() override;
    bool            is_enabled() override;
    unsigned        get_timeout() override;
    void*           get_waitevent() override;
    void            on_idle() override;

private:
    bool            has_coroutines();
    void            resume_coroutines();
    lua_state&      m_state;
    void*           m_event = 0;
    unsigned        m_iterations = 0;
    bool            m_enabled = true;
};
