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

#ifndef LUA_SCRIPT_LOADER_H
#define LUA_SCRIPT_LOADER_H

#include <shared/util.h>

#ifdef _DEBUG
    //------------------------------------------------------------------------------
    inline void lua_load_script_impl(
        struct lua_State* state,
        const char* embed_path,
        const char* name)
    {
        char buffer[512];
        str_cpy(buffer, embed_path, sizeof_array(buffer));

        char* slash = strrchr(buffer, '\\');
        if (slash == nullptr)
            slash = strrchr(buffer, '/');

        if (slash != nullptr)
        {
            *(slash + 1) = '\0';
            str_cat(buffer, name, sizeof_array(buffer));
            if (luaL_dofile(state, buffer) == 0)
                return;
        }

        puts(embed_path);
        printf("CLINK DEBUG: Failed to load '%s'\n", buffer);
    }

    //------------------------------------------------------------------------------
    #define lua_load_script(state, module, name)                    \
        {                                                           \
           extern const char* module##_embed_path;                  \
           extern const char* module##_##name##_lua_script_src;     \
            lua_load_script_impl(state,                             \
                module##_embed_path,                                \
                module##_##name##_lua_script_src                    \
            );\
        }
#else
    //------------------------------------------------------------------------------
    #define lua_load_script(state, module, name)                    \
        {                                                           \
            extern const char* module##_script_##name##_lua;        \
            luaL_dostring(m_state, module##_script_##name##_lua);   \
        }
#endif // _DEBUG

#endif // LUA_SCRIPT_LOADER_H
