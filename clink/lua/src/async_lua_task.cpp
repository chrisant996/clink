// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "async_lua_task.h"

#include <core/str_unordered_set.h>
#include <terminal/printer.h>
#include <terminal/terminal_helpers.h>

#include <readline/readline.h>

#include <map>

//------------------------------------------------------------------------------
class task_manager
{
    friend HANDLE get_task_manager_event();

public:
                            task_manager();
    void                    shutdown();
    std::shared_ptr<async_lua_task> find(const char* key) const;
    bool                    add(const std::shared_ptr<async_lua_task>& task);
    void                    on_idle(lua_state& lua);
    void                    end_line();
    void                    diagnostics();

private:
    bool                    usable() const;

private:
    str_unordered_map<std::shared_ptr<async_lua_task>> m_map;
    volatile bool           m_zombie = false;

    // The Lua ref requires unref on the main thread, and the natural call spot
    // doesn't have access to Lua, so defer unref's until the next idle.
    std::list<std::shared_ptr<callback_ref>> m_unref_callbacks;

    static HANDLE           s_event;
};

//------------------------------------------------------------------------------
HANDLE task_manager::s_event = nullptr;
static task_manager s_manager;

//------------------------------------------------------------------------------
task_manager::task_manager()
{
#ifdef DEBUG
    // Singleton; assert if there's ever more than one.
    static bool s_created = false;
    assert(!s_created);
    s_created = true;
#endif

    s_manager.s_event = CreateEvent(nullptr, false, false, nullptr);
}

//------------------------------------------------------------------------------
std::shared_ptr<async_lua_task> task_manager::find(const char* key) const
{
    if (usable())
    {
        auto const iter = m_map.find(key);
        if (iter != m_map.end())
            return iter->second;
    }

    return nullptr;
}

//------------------------------------------------------------------------------
bool task_manager::add(const std::shared_ptr<async_lua_task>& task)
{
    if (usable())
    {
        assert(s_event);
        assert(!find(task->key()));
        m_map.emplace(task->key(), task);
        task->start();
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
void task_manager::on_idle(lua_state& lua)
{
    auto iter = m_map.begin();
    while (iter != m_map.end())
    {
        if (!iter->second->is_complete())
        {
            ++iter;
        }
        else
        {
            auto next(iter);
            ++next;
            iter->second->run_callback(lua);
            m_unref_callbacks.push_back(iter->second->take_callback());
            m_map.erase(iter);
            iter = next;
        }
    }

    lua_State* state = lua.get_state();
    for (auto callback : m_unref_callbacks)
        luaL_unref(state, LUA_REGISTRYINDEX, callback->m_ref);
    m_unref_callbacks.clear();
}

//------------------------------------------------------------------------------
void task_manager::end_line()
{
    bool unref = false;

    auto iter = m_map.begin();
    while (iter != m_map.end())
    {
        if (iter->second->is_run_until_complete())
        {
            ++iter;
        }
        else
        {
            auto next(iter);
            ++next;
            iter->second->detach();
            m_unref_callbacks.push_back(iter->second->take_callback());
            unref = true;
            m_map.erase(iter);
            iter = next;
        }
    }

    if (unref)
        SetEvent(s_event);
}

//------------------------------------------------------------------------------
void task_manager::diagnostics()
{
    if (m_map.empty() || !rl_explicit_arg)
        return;

    static char bold[] = "\x1b[1m";
    static char norm[] = "\x1b[m";
    static char cyan[] = "\x1b[36m";
    static char green[] = "\x1b[32m";
    static char dark[] = "\x1b[90m";
    static char lf[] = "\n";

    str<> s;
    str<> states;

    s.clear();
    s.format("%sasync tasks:%s\n", bold, norm);
    g_printer->print(s.c_str(), s.length());

    for (auto iter : m_map)
    {
        std::shared_ptr<callback_ref> callback(iter.second->m_callback_ref);
        const int ref = callback ? callback->m_ref : LUA_REFNIL;
        const bool pending = callback && iter.second->m_run_callback;
        s.clear();
        states.clear();
        if (iter.second->is_canceled())
            states << "  " << cyan << "canceled" << norm;
        if (iter.second->is_complete())
            states << "  " << green << "completed" << norm;
        if (ref == LUA_REFNIL)
            s.format("  %p:%s  %snil%s  %s\n", ref, states.c_str(), dark, norm, iter.second->m_src.c_str());
        else
            s.format("  %p:%s  %s%d%s  %s\n", iter.second.get(), states.c_str(), pending ? bold : dark, ref, norm, iter.second->m_src.c_str());
        g_printer->print(s.c_str(), s.length());
    }
}

//------------------------------------------------------------------------------
bool task_manager::usable() const
{
    return !m_zombie && s_event;
}

//------------------------------------------------------------------------------
void task_manager::shutdown()
{
    if (m_zombie)
        return;

    m_zombie = true;

    for (auto &iter : m_map)
    {
        iter.second->detach();
        // Shutting down, so don't need to worry about Lua leaks.
        (void)iter.second->take_callback();
    }
    m_map.clear();
}



//------------------------------------------------------------------------------
async_lua_task::async_lua_task(const char* key, const char* src, bool run_until_complete)
: m_key(key)
, m_src(src)
, m_run_until_complete(run_until_complete)
{
    m_event = CreateEvent(nullptr, true, false, nullptr);
}

//------------------------------------------------------------------------------
async_lua_task::~async_lua_task()
{
    assert(!m_callback_ref);
    CloseHandle(m_event);
}

//------------------------------------------------------------------------------
void async_lua_task::set_callback(const std::shared_ptr<callback_ref>& callback)
{
    m_callback_ref = callback;
    m_run_callback = true;
}

//------------------------------------------------------------------------------
void async_lua_task::run_callback(lua_state& lua)
{
    if (!m_run_callback)
        return;

    if (m_callback_ref)
    {
        lua_State* state = lua.get_state();
        lua_rawgeti(state, LUA_REGISTRYINDEX, m_callback_ref->m_ref);
        lua.pcall(0, 0);
    }

    m_run_callback = false;
}

//------------------------------------------------------------------------------
void async_lua_task::disable_callback()
{
    m_run_callback = false;
}

//------------------------------------------------------------------------------
std::shared_ptr<callback_ref> async_lua_task::take_callback()
{
    std::shared_ptr<callback_ref> callback;
    m_callback_ref.swap(callback);
    return callback;
}

//------------------------------------------------------------------------------
void async_lua_task::cancel()
{
    m_run_callback = false;
    if (!m_is_complete)
        m_is_canceled = true;
}

//------------------------------------------------------------------------------
void async_lua_task::start()
{
    m_thread = std::make_unique<std::thread>(&proc, this);
}

//------------------------------------------------------------------------------
void async_lua_task::detach()
{
    cancel();
    if (m_thread)
    {
        m_thread->detach();
        m_thread.reset();
    }
}

//------------------------------------------------------------------------------
void async_lua_task::proc(async_lua_task* task)
{
    task->do_work();
    task->m_is_complete = true;
    SetEvent(task->m_event);
    SetEvent(get_task_manager_event());
    task->detach();
}



//------------------------------------------------------------------------------
std::shared_ptr<async_lua_task> find_async_lua_task(const char* key)
{
    return s_manager.find(key);
}

//------------------------------------------------------------------------------
bool add_async_lua_task(std::shared_ptr<async_lua_task>& task)
{
    return s_manager.add(task);
}

//------------------------------------------------------------------------------
HANDLE get_task_manager_event()
{
    assert(task_manager::s_event);
    return task_manager::s_event;
}

//------------------------------------------------------------------------------
void task_manager_on_idle(lua_state& lua)
{
    return s_manager.on_idle(lua);
}

//------------------------------------------------------------------------------
extern "C" void end_task_manager()
{
    return s_manager.end_line();
}

//------------------------------------------------------------------------------
void shutdown_task_manager()
{
    return s_manager.shutdown();
}

//------------------------------------------------------------------------------
void task_manager_diagnostics()
{
    return s_manager.diagnostics();
}
