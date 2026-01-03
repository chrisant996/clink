// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>
#include <core/str.h>
#include <core/singleton.h>
#include <terminal/terminal.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_out.h>
#include <vector>

//------------------------------------------------------------------------------
class test_terminal_in
    : public terminal_in, public singleton<test_terminal_in>
{
public:
                    test_terminal_in(bool cursor_visibility=true) {}
                    ~test_terminal_in() = default;
    virtual int32   begin(bool can_hide_cursor=true) override;
    virtual int32   end(bool can_show_cursor=true) override;
    virtual void    override_handle() override {}
    virtual bool    available(uint32 timeout) override;
    virtual void    select(input_idle* callback=nullptr, uint32 timeout=INFINITE) override;
    virtual int32   read() override;
    virtual int32   peek() override;
    virtual bool    send_terminal_request(const char* request, const char* pattern, str_base& out) override;
    virtual key_tester* set_key_tester(key_tester* keys) override;

    void            set_input(const char* input, int32 length=-1);
    void            push_input(const char* input, int32 length=-1);

private:
    int32           m_began = 0;
    key_tester*     m_keys = nullptr;   // NOTE:  Neither called or respected.
    std::vector<uint8> m_queue;
    size_t          m_head = 0;
};

//------------------------------------------------------------------------------
class test_terminal_out
    : public terminal_out
{
public:
    virtual void    open() override {}
    virtual void    begin() override {}
    virtual void    end() override {}
    virtual void    close() override {}
    virtual void    write(const char* chars, int32 length) override {}
    virtual void    flush() override {}
    virtual int32   get_columns() const override { return 80; }
    virtual int32   get_rows() const override { return max<int32>(25, uint32(m_lines.size())); }
    virtual int32   get_top() const override { return 0; }
    virtual bool    get_cursor(int16& x, int16& y) const override { assert(false); x = 0; y = 0; return false; }
    virtual bool    get_line_text(int32 line, str_base& out) const;
    virtual int32   is_line_default_color(int32 line) const { return true; }
    virtual int32   line_has_color(int32 line, const BYTE* attrs, int32 num_attrs, BYTE mask=0xff) const { return false; }
    virtual int32   find_line(int32 starting_line, int32 distance, const char* text, find_line_mode mode, const BYTE* attrs=nullptr, int32 num_attrs=0, BYTE mask=0xff) const { return 0; }
    virtual void    set_attributes(const attributes attr) {}

    void            set_line_text(int32 line, const char* text);

private:
    std::vector<str_moveable> m_lines;
};
