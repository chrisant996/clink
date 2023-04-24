// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "input_params.h"

class binder;
class editor_module;
class str_base;

//------------------------------------------------------------------------------
class bind_resolver
{
public:
    class binding;

    class bind_params : public input_params
    {
        friend class    bind_resolver;
        friend class    bind_resolver::binding;
    };

    class binding
    {
    public:
        explicit        operator bool () const;
        editor_module*  get_module() const;
        unsigned char   get_id() const;
        void            get_chord(str_base& chord) const;
        const input_params& get_params() const;
        void            claim();

    private:
        friend class    bind_resolver;
                        binding() = default;
                        binding(bind_resolver* resolver, int node_index, const bind_params& params);
        bind_resolver*  m_outer = nullptr;
        unsigned short  m_node_index;
        unsigned char   m_module;
        unsigned char   m_len;
        unsigned char   m_id;
        bind_params     m_params;
    };

                        bind_resolver(const binder& binder);
    void                set_group(int group);
    int                 get_group() const;
    bool                step(unsigned char key);
    binding             next();
    void                reset();

    int                 is_bound(const char* seq, int len) const;
    bool                more_than(unsigned int len) const;

private:
    void                claim(binding& binding);
    bool                step_impl(unsigned char key);
    const binder&       m_binder;
    unsigned short      m_node_index = 1;
    unsigned short      m_group = 1;
    bool                m_pending_input = false;
    unsigned char       m_tail = 0;
    bind_params         m_params;
    unsigned short      m_param_accumulator = 0;
    unsigned char       m_param_len = 0;
    bool                m_pending_param = false;
    unsigned char       m_key_count = 0;
    char                m_keys[16];
};
