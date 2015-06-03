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

#ifndef PROMPT_H
#define PROMPT_H

//------------------------------------------------------------------------------
class prompt
{
public:
                    prompt();
                    prompt(prompt&& rhs);
                    prompt(const prompt& rhs) = delete;
                    ~prompt();
    prompt&         operator = (prompt&& rhs);
    prompt&         operator = (const prompt& rhs) = delete;
    void            clear();
    const wchar_t*  get() const;
    void            set(const wchar_t* chars, int char_count=0);
    bool            is_set() const;

protected:
    wchar_t*        m_data;
};

//------------------------------------------------------------------------------
class tagged_prompt
    : public prompt
{
public:
    void            set(const wchar_t* chars, int char_count=0);
    void            tag(const wchar_t* value);

private:
    int             is_tagged(const wchar_t* chars, int char_count=0);
};

//------------------------------------------------------------------------------
class prompt_utils
{
public:
    static prompt   extract_from_console();
};

#endif // PROMPT_H
