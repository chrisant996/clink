// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <memory>

//------------------------------------------------------------------------------
class shared_event
{
    typedef shared_event T;
    typedef std::shared_ptr<T> ST;
public:
    static ST       make()              { return ST(new T, delfunc); }
    void*           get_event()         { return m_event; }
private:
                    shared_event()      { m_event = CreateEvent(nullptr, false, false, nullptr); }
                    ~shared_event()     { CloseHandle(m_event); }
    static void     delfunc(T* del)     { delete del; }
    void*           m_event;
};

//------------------------------------------------------------------------------
class input_idle
{
public:
    virtual             ~input_idle() = default;
    virtual void        reset() = 0;
    virtual bool        is_enabled() = 0;
    virtual unsigned    get_timeout() = 0;
    virtual std::shared_ptr<shared_event> get_waitevent() = 0;
    virtual void        on_idle() = 0;
};
