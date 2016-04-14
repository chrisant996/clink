// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "bind_resolver.h"

#include <new>

//------------------------------------------------------------------------------
void bind_resolver::reset()
{
    new (this) bind_resolver();
}

//------------------------------------------------------------------------------
bool bind_resolver::is_resolved() const
{
    return m_backend != nullptr;
}

//------------------------------------------------------------------------------
void bind_resolver::set_id(int id)
{
    m_id = id;
}

//------------------------------------------------------------------------------
editor_backend* bind_resolver::get_backend() const
{
    return m_backend;
}

//------------------------------------------------------------------------------
int bind_resolver::get_id() const
{
    return m_id;
}

//------------------------------------------------------------------------------
int bind_resolver::get_node_index() const
{
    return m_node_index;
}

//------------------------------------------------------------------------------
void bind_resolver::set_node_index(int index)
{
    m_node_index = index;
}

//------------------------------------------------------------------------------
void bind_resolver::resolve(editor_backend* backend, int id)
{
    reset();

    if (backend == nullptr)
        return;

    m_backend = backend;
    m_id = id;
}
