// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "ecma48_iter.h"
#include "terminal_out.h"

class screen_buffer;

//------------------------------------------------------------------------------
class ecma48_terminal_out
    : public terminal_out
{
public:
                        ecma48_terminal_out(screen_buffer& screen);
    virtual void        begin() override;
    virtual void        end() override;
    virtual void        write(const char* chars, int length) override;
    virtual void        flush() override;
    virtual int         get_columns() const override;
    virtual int         get_rows() const override;

private:
    void                write_c1(const ecma48_code& code);
    void                write_c0(int c0);
    void                set_attributes(const ecma48_code::csi_base& csi);
    ecma48_state        m_state;
    screen_buffer&      m_screen;
};
