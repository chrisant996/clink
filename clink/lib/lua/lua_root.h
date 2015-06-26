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

#include "clink_lua_api.h"
#include "lua_match_generator.h"

struct lua_State;

//------------------------------------------------------------------------------
class lua_root
    : public lua_match_generator
{
public:
                     lua_root();
                     ~lua_root();
    void             initialise();
    bool             do_string(const char* string);
    bool             do_file(const char* path);
    lua_State*       get_state() const;

private:
    void             shutdown();
    lua_State*       m_state;
    clink_lua_api    m_clink;
};

//------------------------------------------------------------------------------
inline lua_State* lua_root::get_state() const
{
    return m_state;
}
