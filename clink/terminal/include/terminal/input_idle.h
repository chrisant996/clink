// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class input_idle
{
public:
    virtual             ~input_idle() = default;
    virtual void        reset() = 0;
    virtual uint32      get_timeout() = 0;
    virtual uint32      get_wait_events(void** events, size_t max) = 0;
    virtual void        on_wait_event(uint32 index) = 0;
    virtual void        on_idle() = 0;
};
