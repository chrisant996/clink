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

#ifndef LUA_MATCH_GENERATOR_H
#define LUA_MATCH_GENERATOR_H

#include "matches/match_generator.h"

//------------------------------------------------------------------------------
class lua_match_generator
    : public match_generator
{
public:
                            lua_match_generator();
    virtual                 ~lua_match_generator();
    virtual match_result    generate(const char* line, int start, int end) override;

private:
    void                    initialise(struct lua_State* state);
    void                    shutdown();
    bool                    load_script(const char* script);
    void                    load_scripts(const char* path);
    struct lua_State*       m_state;

    friend class lua_root;
};

#endif // LUA_MATCH_GENERATOR_H
