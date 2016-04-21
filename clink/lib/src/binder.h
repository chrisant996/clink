// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/array.h>

class bind_resolver;
class editor_backend;

//------------------------------------------------------------------------------
class binder
{
public:
                        binder();
    void                set_default_backend(editor_backend& backend);
    bool                bind(const char* chord, editor_backend& backend, unsigned char id);
    void                update_resolver(unsigned char key, bind_resolver& resolver);

private:
    enum
    {
        node_use_none,
        node_use_bound,
        node_use_parent,
        // node.usage.bits
    };

    struct node
    {
        unsigned int    key         : 7;
        unsigned int    sibling     : 7;
        unsigned int    usage       : 2;
        unsigned int    id_or_child : 8;
        unsigned int    backend     : 4;
        unsigned int                : 4;
    };

    static const unsigned int sentinal = 0x7f;
    typedef fixed_array<editor_backend*, 16> backends; // node.backend.bits - 1

    node*               find_child(node* parent, unsigned char key);
    node*               insert_child(node* parent, unsigned char key);
    node*               add_child(node* parent, unsigned char key);
    int                 add_backend(editor_backend& backend);
    node*               get_root();
    node*               get_node(unsigned int index);
    int                 alloc_node();
    editor_backend*     get_backend(unsigned int index) const;
    backends            m_backends;
    node                m_root;
    node                m_nodes[127]; // node.sibling.bits - 1
    int                 m_default_backend; // MODE4 : no place here?
    char                m_next_node;
};
