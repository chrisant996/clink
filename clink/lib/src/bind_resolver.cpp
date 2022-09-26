// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <assert.h>
#include "bind_resolver.h"
#include "binder.h"

#include <core/base.h>
#include <core/str.h>

#include <new>

//------------------------------------------------------------------------------
bool bind_resolver::bind_params::get(unsigned int param, unsigned int& value) const
{
    if (param >= m_num)
    {
        value = 0;
        return false;
    }

    value = m_params[param];
    return true;
}

//------------------------------------------------------------------------------
unsigned int bind_resolver::bind_params::count() const
{
    return m_num;
}

//------------------------------------------------------------------------------
bool bind_resolver::bind_params::add(unsigned short value, unsigned char len)
{
    if (m_num >= sizeof_array(m_params))
        return false;

    m_params[m_num++] = value;
    m_len += len;

    // Offset the '*' key, so that depth + len is always the key sequence
    // length, even when the param is empty (no digits).
    m_len--;
    return true;
}

//------------------------------------------------------------------------------
void bind_resolver::bind_params::clear()
{
    m_num = 0;
    m_len = 0;
}

//------------------------------------------------------------------------------
bind_resolver::binding::binding(bind_resolver* resolver, int node_index, const bind_params& params)
: m_outer(resolver)
, m_node_index(node_index)
, m_params(params)
{
    const binder& binder = m_outer->m_binder;
    const auto& node = binder.get_node(m_node_index);

    m_module = node.module;
    m_len = max<unsigned char>(1, node.depth) + params.m_len;
    m_id = node.id;
    assert(m_len > 0);
}

//------------------------------------------------------------------------------
bind_resolver::binding::operator bool () const
{
    return (m_outer != nullptr);
}

//------------------------------------------------------------------------------
editor_module* bind_resolver::binding::get_module() const
{
    if (m_outer == nullptr)
        return nullptr;

    const binder& binder = m_outer->m_binder;
    return binder.get_module(m_module);
}

//------------------------------------------------------------------------------
unsigned char bind_resolver::binding::get_id() const
{
    if (m_outer == nullptr)
        return 0xff;

    return m_id;
}

//------------------------------------------------------------------------------
void bind_resolver::binding::get_chord(str_base& chord) const
{
    if (m_outer == nullptr)
        return;

    chord.clear();
    chord.concat_no_truncate(m_outer->m_keys + m_outer->m_tail, m_len);
}

//------------------------------------------------------------------------------
const bind_resolver::bind_params& bind_resolver::binding::get_params() const
{
    return m_params;
}

//------------------------------------------------------------------------------
void bind_resolver::binding::claim()
{
    if (m_outer != nullptr)
        m_outer->claim(*this);
}



//------------------------------------------------------------------------------
bind_resolver::bind_resolver(const binder& binder)
: m_binder(binder)
{
}

//------------------------------------------------------------------------------
void bind_resolver::set_group(int group)
{
    if (unsigned(group) - 1 >= sizeof_array(m_binder.m_nodes) - 1)
        return;

    if (m_group == group || !m_binder.get_node(group - 1).is_group)
        return;

    m_group = group;
    m_node_index = group;
    m_pending_input = true;

    m_params.clear();
    m_param_accumulator = 0;
    m_param_len = 0;
    m_pending_param = false;
}

//------------------------------------------------------------------------------
int bind_resolver::get_group() const
{
    return m_group;
}

//------------------------------------------------------------------------------
void bind_resolver::reset()
{
    int group = m_group;

    new (this) bind_resolver(m_binder);

    m_group = group;
    m_node_index = m_group;

    m_params.clear();
    m_param_accumulator = 0;
    m_param_len = 0;
    m_pending_param = false;
}

//------------------------------------------------------------------------------
bool bind_resolver::step(unsigned char key)
{
    if (m_key_count >= sizeof_array(m_keys))
    {
        reset();
        return false;
    }

    m_keys[m_key_count] = key;
    ++m_key_count;

    return step_impl(key);
}

//------------------------------------------------------------------------------
bool bind_resolver::step_impl(unsigned char key)
{
    int next = m_binder.find_child(m_node_index, key);
    if (!next)
        return true;

    auto node = m_binder.get_node(next);
    if (node.has_params)
    {
        assert(key >= '0' && key <= '9');
        m_param_accumulator *= 10;
        m_param_accumulator += key - '0';
        m_param_len++;
        m_pending_param = true;
    }
    else if (m_pending_param)
    {
        m_params.add(m_param_accumulator, m_param_len);
        m_param_accumulator = 0;
        m_param_len = 0;
        m_pending_param = false;
    }

    m_node_index = next;
    return (node.child == 0);
}

//------------------------------------------------------------------------------
bind_resolver::binding bind_resolver::next()
{
    // Push remaining input through the tree.
    if (m_pending_input)
    {
        m_pending_input = false;

        unsigned int keys_remaining = m_key_count - m_tail;
        if (!keys_remaining || keys_remaining >= sizeof_array(m_keys))
        {
            reset();
            return binding();
        }

        for (int i = m_tail, n = m_key_count; i < n; ++i)
            if (step_impl(m_keys[i]))
                break;
    }

    // Go along this depth's nodes looking for valid binds.
    while (m_node_index)
    {
        const binder::node& node = m_binder.get_node(m_node_index);

        // Move iteration along to the next node.
        int node_index = m_node_index;
        m_node_index = node.next;

        // Check to see if where we're currently at a node in the tree that is
        // a valid bind (at the point of call).
        int key_index = m_tail + node.depth + m_params.m_len - 1;
        if (node.bound && (!node.key || node.key == m_keys[key_index]))
            return binding(this, node_index, m_params);
    }

    // We can't get any further traversing the tree with the input provided.
    reset();
    return binding();
}

//------------------------------------------------------------------------------
int bind_resolver::is_bound(const char* seq, int len) const
{
    return m_binder.is_bound(m_group, seq, len);
}

//------------------------------------------------------------------------------
bool bind_resolver::more_than(unsigned int len) const
{
    return m_key_count > m_tail + len;
}

//------------------------------------------------------------------------------
void bind_resolver::claim(binding& binding)
{
    if (binding)
    {
        m_tail += binding.m_len;
        m_node_index = m_group;
        m_pending_input = true;

        binding.m_outer = nullptr;
    }
}
