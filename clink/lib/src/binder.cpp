// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "binder.h"
#include "bind_resolver.h"

#include <core/base.h>
#include <core/str_hash.h>

#include <algorithm>
#include <assert.h>

//------------------------------------------------------------------------------
template <int32 SIZE> static bool translate_chord(const char* chord, char (&out)[SIZE], int32& len)
{
    // '\M-x'           = alt-x
    // '\C-x' or '^x'   = ctrl-x
    // '\e[t'           = ESC [ t (aka CSI t)
    // 'abc'            = abc

    int32 i = 0;
    for (; i < (SIZE - 1) && *chord; ++i, ++chord)
    {
        if (*chord != '\\' && *chord != '^')
        {
            out[i] = *chord;
            continue;
        }

        if (*chord == '^')
        {
            if (!*++chord)
            {
                out[i] = '^';
                --chord;
            }
            else
                out[i] = *chord & 0x1f;
            continue;
        }

        ++chord;
        switch (*chord)
        {
        case '\0':
            out[i] = '\\';
            --chord;
            continue;

        case 'M':
            if (chord[1] != '-' || !chord[2])
                return false;

            out[i] = '\x1b';

            ++chord; // move to '-'
            if (chord[1] == 'C' && chord[2] == '-') // 'C-' following?
            {
                ++i;
                chord += 3; // move past '-C-'

                if (i >= (SIZE - 1) || !*chord)
                    return false;

                out[i] = *chord & 0x1f;
            }
            continue;

        case 'C':
            if (chord[1] != '-' || !chord[2])
                return false;

            chord += 2;
            out[i] = *chord & 0x1f;
            continue;

        // Some escape sequences for convenience.
        case 'e':   out[i] = '\x1b';    break;
        case 't':   out[i] = '\t';      break;
        case 'n':   out[i] = '\n';      break;
        case 'r':   out[i] = '\r';      break;
        case '0':   out[i] = '\0';      break;
        default:    out[i] = *chord;    break;
        }
    }

    out[i] = '\0';
    len = i;

    // Translate any lone ESC to the bindable ESC sequence so it matches
    // *exactly* ESC being pressed.  Otherwise any input sequence that begins
    // with ESC matches, and the rest of the sequence shows up as text.
    if (out[0] == '\x1b' && out[1] == '\0')
    {
        const char* bindableEsc = get_bindable_esc();
        if (bindableEsc)
            return translate_chord(bindableEsc, out, len);
    }

    return true;
}



//------------------------------------------------------------------------------
binder::binder()
{
    // Initialise the default group.
    m_nodes[0] = { 1 };
    m_nodes[1] = { 0 };
    m_next_node = 2;

    static_assert(sizeof(node) == sizeof(group_node), "Size assumption");
}

//------------------------------------------------------------------------------
int32 binder::get_group(const char* name)
{
    if (name == nullptr || name[0] == '\0')
        return 1;

    uint32 hash = str_hash(name);

    int32 index = get_group_node(0)->next;
    while (index)
    {
        const group_node* node = get_group_node(index);
        if (*(int32*)(node->hash) == hash)
            return index + 1;

        index = node->next;
    }

    return -1;
}

//------------------------------------------------------------------------------
int32 binder::create_group(const char* name)
{
    if (name == nullptr || name[0] == '\0')
        return -1;

    int32 index = alloc_nodes(2);
    if (index < 0)
        return -1;

    // Create a new group node;
    group_node* group = get_group_node(index);
    *(int32*)(group->hash) = str_hash(name);
    group->is_group = 1;

    // Link the new node into the front of the list.
    group_node* master = get_group_node(0);
    group->next = master->next;
    master->next = index;

    // Next node along is the group's root.
    ++index;
    m_nodes[index] = {};
    return index;
}

//------------------------------------------------------------------------------
bool binder::bind(
    uint32 group,
    const char* chord,
    editor_module& module,
    uint8 id,
    bool has_params)
{
    // Validate input
    if (group >= sizeof_array(m_nodes))
        return false;

    // Translate from ASCII representation to actual keys.
    char translated[64];
    int32 len = 0;
    if (!translate_chord(chord, translated, len))
        return false;

    chord = translated;

    // Store the module pointer
    int32 module_index = add_module(module);
    if (module_index < 0)
        return false;

    // Add the chord of keys into the node graph.
    int32 depth = 0;
    int32 head = group;
    for (; len; ++chord, ++depth, --len)
        if (!(head = insert_child(head, *chord, has_params && *chord == '*')))
            return false;

    --chord;

    // If the insert point is already bound we'll duplicate the node at the end
    // of the list. Also check if this is a duplicate of the existing bind.
    node* bindee = m_nodes + head;
    if (bindee->bound)
    {
        int32 check = head;
        while (check > head)
        {
            if (bindee->key == *chord && bindee->module == module_index && bindee->id == id)
                return true;

            check = bindee->next;
            bindee = m_nodes + check;
        }

        head = append(head, *chord);
    }

    if (!head)
        return false;

    bindee = m_nodes + head;
    bindee->module = module_index;
    bindee->bound = 1;
    bindee->depth = depth;
    bindee->id = id;

    return true;
}

//------------------------------------------------------------------------------
// KLUDGE: -1 means redispatch.  And goes awry if the binding group stack is
// deeper than 2.  Or if it's not actually a stack, etc.  It may be beneficial
// to manage the binding group stack internally, rather than letting callers
// manage it externally however they like.
int32 binder::is_bound(uint32 group, const char* seq, int32 len) const
{
    if (!len)
        return false;

    uint32 node_index = group;
    const bool ctrl_or_ext = (*seq == '\x1b' || uint8(*seq) < ' ' || *seq == '\x7f');

    while (len--)
    {
        int32 next = find_child(node_index, *(seq++));
        if (!next)
        {
            while (node_index)
            {
                const binder::node& node = get_node(node_index);

                // Move iteration along to the next node.
                int32 was_index = node_index;
                node_index = node.next;

                // Check to see if where we're currently at a node in the tree
                // that is a valid bind (at the point of call).
                if (node.bound && (!node.key || node.key == *seq))
                {
                    if (node.key || !ctrl_or_ext)
                        return true;
                    if (node.id == id_catchall_only_printable)
                        return -1;
                    // So that win_terminal_in can reject keys that are not
                    // bound at all.
                    return false;
                }
            }
            return false;
        }

        node_index = next;
        if (!len)
            return (get_node(next).child == 0);
    }

    return false;
}

//------------------------------------------------------------------------------
int32 binder::insert_child(int32 parent, uint8 key, bool has_params)
{
    if (int32 child = find_child(parent, key))
    {
        assert(get_node(child).has_params == has_params);
        return child;
    }

    return add_child(parent, key, has_params);
}

//------------------------------------------------------------------------------
int32 binder::find_child(int32 parent, uint8 key) const
{
    const node* node = m_nodes + parent;

    if (node->has_params && key >= '0' && key <= '9')
        return parent;

    int32 index = node->child;
    for (; index > parent; index = node->next)
    {
        node = m_nodes + index;
        if (node->key == key)
            return index;
        if (node->has_params && key >= '0' && key <= '9')
            return index;
    }

    return 0;
}

//------------------------------------------------------------------------------
int32 binder::add_child(int32 parent, uint8 key, bool has_params)
{
    int32 child = alloc_nodes();
    if (child < 0)
        return 0;

    node addee = {};
    addee.key = key;
    addee.has_params = has_params;

    int32 current_child = m_nodes[parent].child;
    if (current_child < parent)
    {
        addee.next = parent;
        m_nodes[parent].child = child;
    }
    else
    {
        int32 tail = find_tail(current_child);
        addee.next = parent;
        m_nodes[tail].next = child;
    }

    m_nodes[child] = addee;
    return child;
}

//------------------------------------------------------------------------------
int32 binder::find_tail(int32 head)
{
    while (m_nodes[head].next > head)
        head = m_nodes[head].next;

    return head;
}

//------------------------------------------------------------------------------
int32 binder::append(int32 head, uint8 key)
{
    int32 index = alloc_nodes();
    if (index < 0)
        return 0;

    int32 tail = find_tail(head);

    node addee = {};
    addee.key = key;
    addee.next = m_nodes[tail].next;
    m_nodes[tail].next = index;

    m_nodes[index] = addee;
    return index;
}

//------------------------------------------------------------------------------
const binder::node& binder::get_node(uint32 index) const
{
    if (index < sizeof_array(m_nodes))
        return m_nodes[index];

    static const node zero = {};
    return zero;
}

//------------------------------------------------------------------------------
binder::group_node* binder::get_group_node(uint32 index)
{
    if (index < sizeof_array(m_nodes))
        return (group_node*)(m_nodes + index);

    return nullptr;
}

//------------------------------------------------------------------------------
int32 binder::alloc_nodes(uint32 count)
{
    int32 next = m_next_node + count;
    if (next > sizeof_array(m_nodes))
        return -1;

    m_next_node = next;
    return m_next_node - count;
}

//------------------------------------------------------------------------------
int32 binder::add_module(editor_module& module)
{
    for (int32 i = 0, n = m_modules.size(); i < n; ++i)
        if (*(m_modules[i]) == &module)
            return i;

    if (editor_module** slot = m_modules.push_back())
    {
        *slot = &module;
        return int32(slot - m_modules.front());
    }

    return -1;
}

//------------------------------------------------------------------------------
editor_module* binder::get_module(uint32 index) const
{
    auto b = m_modules[index];
    return b ? *b : nullptr;
}
