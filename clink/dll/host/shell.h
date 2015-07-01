/* Copyright (c) 2013 Martin Ridgers
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

class line_editor;

//------------------------------------------------------------------------------
class shell
{
public:
                    shell(line_editor* editor);
    virtual         ~shell() = 0 {}
    virtual bool    validate() = 0;
    virtual bool    initialise() = 0;
    virtual void    shutdown() = 0;
    line_editor*    get_line_editor() const;

private:
    line_editor*    m_line_editor;
};

//------------------------------------------------------------------------------
inline shell::shell(line_editor* editor)
: m_line_editor(editor)
{
}

//------------------------------------------------------------------------------
inline line_editor* shell::get_line_editor() const
{
    return m_line_editor;
}
