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

#include "pch.h"
#include "line_editor.h"

extern "C" {

//------------------------------------------------------------------------------
int edit_line(line_editor_t* instance, const wchar_t* prompt, wchar_t* out, int out_size)
{
    line_editor* self = (line_editor*)instance;
    return (self->edit_line(prompt, out, out_size) == true);
}

//------------------------------------------------------------------------------
const char* get_shell_name(line_editor_t* instance)
{
    line_editor* self = (line_editor*)instance;
    return self->get_shell_name();
}

//------------------------------------------------------------------------------
void set_shell_name(line_editor_t* instance, const char* name)
{
    line_editor* self = (line_editor*)instance;
    self->set_shell_name(name);
}

} // extern "C"
