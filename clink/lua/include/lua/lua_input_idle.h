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
    unsigned        get_timeout() override;
    void*           get_waitevent() override;
    void            on_idle() override;
    void            on_task_manager() override;

    void            kick();

    static void     signal_delayed_init();
    static void     signal_reclassify();

private:
    bool            is_enabled();
    bool            has_coroutines();
    void            resume_coroutines();
    lua_state&      m_state;
    unsigned        m_iterations = 0;
    bool            m_enabled = true;

    static bool     s_signaled_delayed_init;
    static bool     s_signaled_reclassify;
};
