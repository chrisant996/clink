// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "lua_bindable.h"

#include <core/str.h>

#include <memory>
#include <thread>

class lua_state;

//------------------------------------------------------------------------------
struct callback_ref
{
    callback_ref(int32 ref) : m_ref(ref) {}
    int32 m_ref;
};

//------------------------------------------------------------------------------
class async_yield_lua
    : public lua_bindable<async_yield_lua>
{
public:
                            async_yield_lua(const char* name, uint32 timeout=0);
                            ~async_yield_lua();

    int32                   get_name(lua_State* state);
    int32                   get_expiration(lua_State* state);
    int32                   ready(lua_State* state);

    bool                    is_expired() const;
    void                    set_ready() { m_ready = true; }
    void                    clear_ready() { m_ready = false; }

private:
    str_moveable            m_name;
    double                  m_expiration = 0.0;
    bool                    m_ready = false;

    friend class lua_bindable<async_yield_lua>;
    static const char* const c_name;
    static const method c_methods[];
};

//------------------------------------------------------------------------------
class async_lua_task : public std::enable_shared_from_this<async_lua_task>
{
    friend class task_manager;

public:
                            async_lua_task(const char* key, const char* src, bool run_until_complete=false);
    virtual                 ~async_lua_task();

    const char*             key() const { return m_key.c_str(); }
    HANDLE                  get_wait_handle() const { return m_event; }
    bool                    is_complete() const { return m_is_complete; }
    bool                    is_canceled() const { return m_is_canceled; }

    void                    set_asyncyield(async_yield_lua* asyncyield);
    void                    set_callback(const std::shared_ptr<callback_ref>& callback);
    void                    run_callback(lua_state& lua);
    void                    disable_callback();
    std::shared_ptr<callback_ref> take_callback();
    void                    cancel();

protected:
    virtual void            do_work() = 0;

    void                    wake_asyncyield() const;

private:
    void                    start();
    void                    detach();
    bool                    is_run_until_complete() const { return m_run_until_complete; }
    static void             proc(std::shared_ptr<async_lua_task> task);

private:
    HANDLE                  m_event;
    std::unique_ptr<std::thread> m_thread;
    str_moveable            m_key;
    str_moveable            m_src;
    async_yield_lua*        m_asyncyield = nullptr;
    std::shared_ptr<callback_ref> m_callback_ref;
    const bool              m_run_until_complete = false;
    bool                    m_run_callback = false;
    bool                    m_is_complete = false;
    volatile bool           m_is_canceled = false;
};

//------------------------------------------------------------------------------
std::shared_ptr<async_lua_task> find_async_lua_task(const char* key);
bool add_async_lua_task(std::shared_ptr<async_lua_task>& task);
