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

#ifndef LINE_EDITOR_H
#define LINE_EDITOR_H

class terminal;

//------------------------------------------------------------------------------
struct environment
{
    terminal*           term;
};

//------------------------------------------------------------------------------
class line_editor
{
public:
                        line_editor(const environment& env);
    virtual             ~line_editor() = 0 {}
    virtual bool        edit_line(const wchar_t* prompt, wchar_t* out, int out_size) = 0;
    virtual const char* get_shell_name() const = 0;
    virtual void        set_shell_name(const char* name) = 0;
    terminal*           get_terminal() const;

private:
    terminal*           m_terminal;

private:
                        line_editor(const line_editor&);    // unimplemented
    void                operator = (const line_editor&);    // unimplemented
};

//------------------------------------------------------------------------------
inline terminal* line_editor::get_terminal() const
{
    return m_terminal;
}

#endif // LINE_EDITOR_H
