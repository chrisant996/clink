// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "terminal_in.h"

class key_tester;

//------------------------------------------------------------------------------
class win_terminal_in
    : public terminal_in
{
public:
    virtual void    begin() override;
    virtual void    end() override;
    virtual void    select() override;
    virtual int     read() override;
    virtual key_tester* set_key_tester(key_tester* keys) override;

private:
    void            read_console();
    void            process_input(const KEY_EVENT_RECORD& key_event);
    void            push(unsigned int value);
    void            push(const char* seq);
    unsigned char   pop();
    key_tester*     m_keys;
    void*           m_stdin = nullptr;
    unsigned int    m_dimensions = 0;
    unsigned long   m_prev_mode = 0;
    unsigned char   m_buffer_head = 0;
    unsigned char   m_buffer_count = 0;
    unsigned char   m_buffer[16]; // must be power of two.
};
