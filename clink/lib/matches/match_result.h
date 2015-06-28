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

#include <vector>

//------------------------------------------------------------------------------
class match_result
{
public:
                        match_result();
                        ~match_result();
                        match_result(match_result&& rhs);
    void                operator = (match_result&& rhs);
    void                reserve(unsigned int count);
    unsigned int        get_match_count() const;
    const char*         get_match(unsigned int index) const;
    void                add_match(const char* match);

private:
    void                swap(match_result& rhs);
                        match_result(const match_result&);  // unimplemented
    void                operator = (const match_result&);   // unimplemented
    std::vector<char*>  m_matches;
};

//------------------------------------------------------------------------------
inline unsigned int match_result::get_match_count() const
{
    return (unsigned int)m_matches.size();
}

//------------------------------------------------------------------------------
inline const char* match_result::get_match(unsigned int index) const
{
    return (index < get_match_count()) ? m_matches[index] : nullptr;
}
