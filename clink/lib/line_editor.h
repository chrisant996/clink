/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "core/str.h"

class match_generator;
class match_printer;
class terminal;

//------------------------------------------------------------------------------
class line_editor
{
public:
    struct desc
    {
        const char*         shell_name;
        terminal*           term;
        match_generator*    match_generator;
        match_printer*      match_printer;
    };

                            line_editor(const desc& desc);
    virtual                 ~line_editor() = 0 {}
    bool                    edit_line(const wchar_t* prompt, wchar_t* out, int out_count);
    terminal*               get_terminal() const;
    match_printer*          get_match_printer() const;
    match_generator*        get_match_generator() const;
    const char*             get_shell_name() const;

private:
    virtual bool            edit_line_impl(const wchar_t* prompt, wchar_t* out, int out_count) = 0;
    terminal*               m_terminal;
    match_printer*          m_match_printer;
    match_generator*        m_match_generator;
    str<32>                 m_shell_name;

private:
                            line_editor(const line_editor&);    // unimplemented
    void                    operator = (const line_editor&);    // unimplemented
};

//------------------------------------------------------------------------------
inline terminal* line_editor::get_terminal() const
{
    return m_terminal;
}

//------------------------------------------------------------------------------
inline match_printer* line_editor::get_match_printer() const
{
    return m_match_printer;
}

//------------------------------------------------------------------------------
inline match_generator* line_editor::get_match_generator() const
{
    return m_match_generator;
}

//------------------------------------------------------------------------------
inline const char* line_editor::get_shell_name() const
{
    return m_shell_name.c_str();
}
