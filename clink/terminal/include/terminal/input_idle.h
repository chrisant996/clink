// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class input_idle
{
public:
    virtual             ~input_idle() = default;
    virtual void        reset() = 0;
    virtual unsigned    get_timeout() = 0;
    virtual void*       get_waitevent() = 0;
    virtual void        on_idle() = 0;
    virtual void        on_task_manager() = 0;
};
