// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class editor_backend;

// MODE4 : shouldn't be public

//------------------------------------------------------------------------------
class bind_resolver
{
public:
    void            reset();
    bool            is_resolved() const;
    void            set_id(int id);
    editor_backend* get_backend() const;
    int             get_id() const;

private:
    friend class    binder;
    int             get_node_index() const;
    void            set_node_index(int index);
    void            resolve(editor_backend* backend, int id);

private:
    editor_backend* m_backend = nullptr;
    int             m_id = -1;
    int             m_node_index = -1;
};
