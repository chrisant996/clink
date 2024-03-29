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
    uint32          get_timeout() override;
    uint32          get_wait_events(void** events, size_t max) override;
    void            on_wait_event(uint32 index) override;
    void            on_idle() override;

    void            kick();

    static void     signal_delayed_init();
    static void     signal_reclassify();
    static HANDLE   get_idle_event();

private:
    bool            is_enabled();
    bool            has_coroutines();
    void            resume_coroutines();
    lua_state&      m_state;
    uint32          m_iterations = 0;
    bool            m_enabled = true;

    uint32          m_index_recognizer = -1;
    uint32          m_index_task_manager = -1;
    uint32          m_index_force_idle = -1;

    static bool     s_signaled_delayed_init;
    static bool     s_signaled_reclassify;
};
