// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <functional>

//------------------------------------------------------------------------------
class auto_closure
{
    using Func = std::function<void()>;

    struct auto_closure_node
    {
        auto_closure_node(Func func) : m_func(func), m_next(nullptr) {}
        //auto_closure_node(Func&& func) : m_func(std::move(func)), m_next(nullptr) {}

        Func m_func;
        auto_closure_node* m_next;

    private:
        auto_closure_node() = delete;
        auto_closure_node(auto_closure_node const& other) = delete;
        auto_closure_node(auto_closure_node&& other) = delete;
        auto_closure_node& operator=(auto_closure_node const& other) = delete;
        auto_closure_node& operator=(auto_closure_node&& other) = delete;
    };

public:
    auto_closure() : m_head(nullptr) {}
    auto_closure(Func func) : m_head(nullptr) { add(func); }
    //auto_closure(Func&& func) : m_head(nullptr) { add(func); }
    ~auto_closure() { clear(true/*call*/); }

    void clear() { clear(false/*call*/); }

    void add(Func func) { link(new auto_closure_node(func)); }
    //void add(Func&& func) { link(new auto_closure_node(func)); }

private:
    void clear(bool call)
    {
        while (m_head)
            pop(call);
    }

    void pop(bool call)
    {
        auto_closure_node* const p = m_head;
        m_head = p->m_next;
        if (call)
            p->m_func();
        delete p;
    }

    void link(auto_closure_node* p)
    {
        p->m_next = m_head;
        m_head = p;
    }

private:
    auto_closure(auto_closure const& other) = delete;
    auto_closure(auto_closure&& other) = delete;
    auto_closure& operator=(auto_closure const& other) = delete;
    auto_closure& operator=(auto_closure&& other) = delete;

private:
    auto_closure_node* m_head;
};
