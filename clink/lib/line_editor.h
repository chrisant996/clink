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

#ifndef BACKEND_H
#define BACKEND_H

#ifdef __cplusplus

//------------------------------------------------------------------------------
class line_editor
{
public:
                        line_editor() {}
    virtual bool        edit_line(const wchar_t* prompt, wchar_t* out, int out_size) = 0;
    virtual const char* get_shell_name() const = 0;
    virtual void        set_shell_name(const char* name) = 0;

protected:
    virtual             ~line_editor() = 0 {}

private:
                        line_editor(const line_editor&);    // unimplemented
    void                operator = (const line_editor&);    // unimplemented
};

#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

typedef void* line_editor_t;

int         edit_line(line_editor_t* instance, const wchar_t* prompt, wchar_t* out, int out_size);
const char* get_shell_name(line_editor_t* instance);
void        set_shell_name(line_editor_t* instance, const char* name);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // BACKEND_H

// vim: expandtab
