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

//------------------------------------------------------------------------------
template <typename TYPE, int COUNT>
class str_base
{
public:
                        str_base();
    TYPE*               data();
    unsigned int        length() const;
    const TYPE*         c_str() const;
    void                clear();
    bool                copy(const TYPE* src);
    bool                concat(const TYPE* src);
    bool                format(const TYPE* format, ...);

private:
    TYPE                m_data[COUNT];
};

template <int COUNT=128> class  str : public str_base<char,    COUNT> {};
template <int COUNT=128> class wstr : public str_base<wchar_t, COUNT> {};

//------------------------------------------------------------------------------
template <typename TYPE, int COUNT>
str_base<TYPE, COUNT>::str_base()
{
    clear();
}

//------------------------------------------------------------------------------
template <typename TYPE, int COUNT>
TYPE* str_base<TYPE, COUNT>::data()
{
    return m_data;
}

//------------------------------------------------------------------------------
template <typename TYPE, int COUNT>
unsigned int str_base<TYPE, COUNT>::length() const
{
    return (unsigned int)(strlen(m_data));
}

//------------------------------------------------------------------------------
template <typename TYPE, int COUNT>
const TYPE* str_base<TYPE, COUNT>::c_str() const
{
    return m_data;
}

//------------------------------------------------------------------------------
template <typename TYPE, int COUNT>
void str_base<TYPE, COUNT>::clear()
{
    m_data[0] = '\0';
}

//------------------------------------------------------------------------------
template <typename TYPE, int COUNT>
bool str_base<TYPE, COUNT>::copy(const TYPE* src)
{
    clear();
    return concat(src);
}

//------------------------------------------------------------------------------
template <typename TYPE, int COUNT>
bool str_base<TYPE, COUNT>::concat(const TYPE* src)
{
    int m = COUNT - int(size()) - 1;
    if (m > 0)
        strncat(m_data, src, m);

    return (strlen(src) <= m);
}

//------------------------------------------------------------------------------
template <typename TYPE, int COUNT>
bool str_base<TYPE, COUNT>::format(const TYPE* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(m_data, COUNT, format, args);
    m_data[COUNT - 1] = '\0';
    va_end(args);
    return (ret >= 0);
}
