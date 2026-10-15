// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "char_reader.h"

//------------------------------------------------------------------------------
class char_reader_impl : public editor_module
{
public:
    bool            activate(result& result, char_reader_callback callback);
    bool            activate_number(result& result, read_number_callback callback,
                                    read_number_change_callback on_change, int32 max_number);
    bool            is_active() const;
    bool            is_codepoint_active() const;
    bool            is_number_active() const;

private:
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const context& context) override;
    virtual void    on_end_line() override;
    virtual void    on_need_input(int32& bind_group) override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    virtual void    on_matches_changed(const context& context, const line_state& line, const char* needle) override;
    virtual void    on_terminal_resize(int32 columns, int32 rows, const context& context) override;
    virtual void    on_signal(int32 sig) override;

    void            finish(bool success);
    void            on_number_input(const input& input, result& result);
    void            clear_bytes();

    enum class mode { none, codepoint, number };
    mode            m_mode = mode::none;
    char_reader_callback m_callback;
    read_number_callback m_number_callback;
    read_number_change_callback m_number_change;
    int32           m_bind_group = -1;
    int32           m_number_bind_group = -1;
    int32           m_prev_bind_group = -1;
    int32           m_number = -1;
    int32           m_max_number = 999999;
    uint32          m_value = 0;
    uint32          m_min_value = 0;
    uint8           m_expected = 0;
    uint8           m_length = 0;
    char            m_bytes[4] = {};
};

//------------------------------------------------------------------------------
bool is_read_codepoint_active();
bool is_read_number_active();
bool is_char_reader_active();
