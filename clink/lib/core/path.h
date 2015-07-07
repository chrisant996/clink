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

class str_base;

//------------------------------------------------------------------------------
class path
{
public:
    static void     clean(str_base& in_out, int sep='\\');
    static bool     get_base_name(const char* in, str_base& out);
    static bool     get_directory(const char* in, str_base& out);
    static bool     get_directory(str_base& in_out);
    static bool     get_drive(const char* in, str_base& out);
    static bool     get_drive(str_base& in_out);
    static bool     get_extension(const char* in, str_base& out);
    static bool     get_name(const char* in, str_base& out);
    static bool     join(const char* lhs, const char* rhs, str_base& out);
    static bool     append(str_base& out, const char* rhs);

private:
                    path() = delete;
                    ~path() = delete;
                    path(const char&) = delete;
    void            operator = (const path&) = delete;
};
