// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "line_buffer.h"

//------------------------------------------------------------------------------
class rl_buffer
    : public line_buffer
{
    struct command
    {
        unsigned int        offset;
        unsigned int        length;
    };

public:
                            rl_buffer(const char* command_delims = nullptr,
                                      const char* word_delims = " \t",
                                      const char* quote_pair = "\"");

    virtual void            reset() override;
    virtual void            begin_line() override;
    virtual void            end_line() override;
    virtual const char*     get_buffer() const override;
    virtual unsigned int    get_length() const override;
    virtual unsigned int    get_cursor() const override;
    virtual unsigned int    set_cursor(unsigned int pos) override;
    virtual bool            insert(const char* text) override;
    virtual bool            remove(unsigned int from, unsigned int to) override;
    virtual void            draw() override;
    virtual void            redraw() override;
    virtual void            begin_undo_group() override;
    virtual void            end_undo_group() override;
    virtual void            collect_words(std::vector<word>& words, bool stop_at_cursor) const;

private:
    void                    find_command_bounds(std::vector<command>& commands, bool stop_at_cursor) const;
    char                    get_closing_quote() const;

private:
    bool                    m_need_draw;

    const char* const       m_command_delims;
    const char* const       m_word_delims;
    const char* const       m_quote_pair;
};

//------------------------------------------------------------------------------
inline char rl_buffer::get_closing_quote() const
{
    return m_quote_pair[1] ? m_quote_pair[1] : m_quote_pair[0];
}
