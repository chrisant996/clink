// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/array.h>

class editor_module;

//------------------------------------------------------------------------------
class binder
{
public:
    // key==0 + id==0xff is special:  it tells is_bound() that control codes and
    // extended keys not explicitly bound will be eaten (i.e. will not cancel
    // the binding group and will not be re-dispatched).
    static constexpr uint8 id_catchall_only_printable = 0xff;

                        binder();
    int32               get_group(const char* name=nullptr);
    int32               create_group(const char* name);
    bool                bind(uint32 group, const char* chord, editor_module& module, uint8 id, bool has_params=false);
    int32               is_bound(uint32 group, const char* seq, int32 len) const;

private:
    static const int32  link_bits = 9;
    static const int32  module_bits = 5;

    struct node
    {
        unsigned short  is_group    : 1;
        unsigned short  next        : link_bits;
        unsigned short  module      : module_bits;
        unsigned short  has_params  : 1;

        unsigned short  child       : link_bits;
        unsigned short  depth       : 4;
        unsigned short  bound       : 1;
        unsigned short              : 2;

        uint8           key;
        uint8           id;
    };

    struct group_node
    {
        unsigned short  is_group    : 1;
        unsigned short  next        : link_bits;
        unsigned short              : 6;
        unsigned short  hash[2];
    };

    typedef fixed_array<editor_module*, (1 << module_bits)> modules;

    friend class        bind_resolver;
    int32               insert_child(int32 parent, uint8 key, bool has_params);
    int32               find_child(int32 parent, uint8 key) const;
    int32               add_child(int32 parent, uint8 key, bool has_params);
    int32               find_tail(int32 head);
    int32               append(int32 head, uint8 key);
    const node&         get_node(uint32 index) const;
    group_node*         get_group_node(uint32 index);
    int32               alloc_nodes(uint32 count=1);
    int32               add_module(editor_module& module);
    editor_module*      get_module(uint32 index) const;
    modules             m_modules;
    node                m_nodes[1 << link_bits];
    uint32              m_next_node;
};
