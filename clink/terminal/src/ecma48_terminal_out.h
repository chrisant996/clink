// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "terminal_out.h"
#include "ecma48_iter.h"

template <typename T> class array;

//------------------------------------------------------------------------------
class ecma48_terminal_out
    : public terminal_out
{
public:
                        ecma48_terminal_out(terminal_out& inner);
    virtual void        begin() override;
    virtual void        end() override;
    virtual void        write(const char* chars, int length) override;
    virtual void        flush() override;
    virtual int         get_columns() const override;
    virtual int         get_rows() const override;

private:
    void                write_c1(const ecma48_code& code);
    void                write_sgr(const array<int>& params);
    void                write_c0(int c0);
    terminal_out&       m_inner;
    ecma48_state        m_state;
};
