// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include <stdio.h>
#include <core/base.h>
#include <core/str.h>

class errfile_reader
{
public:
                    errfile_reader();
                    ~errfile_reader();
    bool            open(const char* name);
    bool            next(str_base& out);
private:
    FILE*           m_file = nullptr;
    int8            m_utf16 = -1;
    wstr_moveable   m_buffer;
};
