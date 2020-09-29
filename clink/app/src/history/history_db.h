// Copyright (c) 2017 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str_iter.h>

#include <vector>

//------------------------------------------------------------------------------
class history_db
{
public:
    enum expand_result
    {
        // values match Readline's history_expand() return value.
        expand_error            = -1,
        expand_none             = 0,
        expand_ok               = 1,
        expand_print            = 2,
    };

    static const unsigned int   max_line_length = 8192;
    typedef unsigned int        line_id;

    class iter
    {
    public:
                                ~iter();
        line_id                 next(str_iter& out);

    private:
                                iter() = default;
        friend                  history_db;
        uintptr_t               impl = 0;
    };

                                history_db();
                                ~history_db();
    void                        initialise();
    void                        load_rl_history();
    void                        clear();
    bool                        add(const char* line);
    int                         remove(const char* line);
    bool                        remove(line_id id);
    bool                        remove(int rl_history_index, const char* line);
    line_id                     find(const char* line) const;
    template <int S> iter       read_lines(char (&buffer)[S]);
    iter                        read_lines(char* buffer, unsigned int buffer_size);

    static expand_result        expand(const char* line, str_base& out);

private:
    enum : char
    {
        bank_none               = -1,
        bank_master,
        bank_session,
        bank_count,
    };

    friend                      class read_line_iter;
    void                        reap();
    template <typename T> void  for_each_bank(T&& callback);
    template <typename T> void  for_each_bank(T&& callback) const;
    unsigned int                get_active_bank() const;
    void*                       get_bank(unsigned int index) const;
    void*                       m_alive_file;
    void*                       m_bank_handles[bank_count];
    std::vector<line_id>        m_index_map;
    size_t                      m_master_len;
};

//------------------------------------------------------------------------------
template <int S> history_db::iter history_db::read_lines(char (&buffer)[S])
{
    return read_lines(buffer, S);
}
