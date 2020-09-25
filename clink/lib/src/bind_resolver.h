// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class binder;
class editor_module;
class str_base;

//------------------------------------------------------------------------------
class bind_resolver
{
public:
    class binding
    {
    public:
        explicit        operator bool () const;
        editor_module*  get_module() const;
        unsigned char   get_id() const;
        void            get_chord(str_base& chord) const;
        void            claim();

    private:
        friend class    bind_resolver;
                        binding() = default;
                        binding(bind_resolver* resolver, int node_index);
        bind_resolver*  m_outer = nullptr;
        unsigned short  m_node_index;
        unsigned char   m_module;
        unsigned char   m_depth;
        unsigned char   m_id;
    };

                        bind_resolver(const binder& binder);
    void                set_group(int group);
    int                 get_group() const;
    bool                step(unsigned char key);
    binding             next();
    void                reset();

    bool                is_bound(const char* seq, int len) const;

private:
    void                claim(binding& binding);
    bool                step_impl(unsigned char key);
    const binder&       m_binder;
    unsigned short      m_node_index = 1;
    unsigned short      m_group = 1;
    bool                m_pending_input = false;
    unsigned char       m_tail = 0;
    unsigned char       m_key_count = 0;
    char                m_keys[16];
};
