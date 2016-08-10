// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/array.h>

class editor_backend;

//------------------------------------------------------------------------------
class binder
{
public:
                        binder();
    int                 get_group(const char* name=nullptr);
    int                 create_group(const char* name);
    bool                bind(unsigned int group, const char* chord, editor_backend& backend, unsigned char id);

private:
    static const int    link_bits = 9;
    static const int    backend_bits = 6;

    struct node
    {
        unsigned short  is_group    : 1; 
        unsigned short  next        : link_bits;
        unsigned short  backend     : backend_bits;

        unsigned short  child       : link_bits;
        unsigned short  depth       : 4;
        unsigned short  bound       : 1;
        unsigned short              : 2;

        unsigned char   key;
        unsigned char   id;
    };

    struct group_node
    {
        unsigned short  is_group    : 1;
        unsigned short  next        : link_bits;
        unsigned short              : 6;
        unsigned short  hash[2];
    };

    typedef fixed_array<editor_backend*, (1 << backend_bits)> backends;

    friend class        bind_resolver;
    int                 insert_child(int parent, unsigned char key);
    int                 find_child(int parent, unsigned char key) const;
    int                 add_child(int parent, unsigned char key);
    int                 find_tail(int head);
    int                 append(int head, unsigned char key);
    const node&         get_node(unsigned int index) const;
    group_node*         get_group_node(unsigned int index);
    int                 alloc_nodes(unsigned int count=1);
    int                 add_backend(editor_backend& backend);
    editor_backend*     get_backend(unsigned int index) const;
    backends            m_backends;
    node                m_nodes[1 << link_bits];
    unsigned int        m_next_node;
};
