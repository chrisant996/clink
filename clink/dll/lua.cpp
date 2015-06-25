/* Copyright (c) 2012 Martin Ridgers
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

//------------------------------------------------------------------------------
char** lua_match_display_filter(char** matches, int match_count)
{
#if MODE4
    // A small note about the contents of 'matches' - the first match isn't
    // really a match, it's the word being completed. Readline ignores it when
    // displaying the matches. So matches[1...n] are useful.

    char** new_matches;
    int top;
    int i;

    top = lua_gettop(g_lua);
    new_matches = NULL;

    // Check there's a display filter set.
    lua_getglobal(g_lua, "clink");

    lua_pushliteral(g_lua, "match_display_filter");
    lua_rawget(g_lua, -2);

    i = lua_isnil(g_lua, -1);

    if (i != 0)
    {
        goto done;
    }

    // Convert matches to a Lua table.
    lua_createtable(g_lua, match_count, 0);
    for (i = 1; i < match_count; ++i)
    {
        lua_pushstring(g_lua, matches[i]);
        lua_rawseti(g_lua, -2, i);
    }

    // Call the filter.
    if (lua_pcall(g_lua, 1, 1, 0) != 0)
    {
        puts(lua_tostring(g_lua, -1));
        goto done;
    }

    // Convert table returned by the Lua filter function to C.
    new_matches = (char**)calloc(match_count + 1, sizeof(*new_matches));
    for (i = 0; i < match_count; ++i)
    {
        const char* match;

        lua_rawgeti(g_lua, -1, i);
        if (lua_isnil(g_lua, -1))
        {
            match = "nil";
        }
        else
        {
            match = lua_tostring(g_lua, -1);
        }

        new_matches[i] = (char*)malloc(strlen(match) + 1);
        strcpy(new_matches[i], match);

        lua_pop(g_lua, 1);
    }

done:
    top = lua_gettop(g_lua) - top;
    lua_pop(g_lua, top);
    return new_matches;
#endif
}

//------------------------------------------------------------------------------
void lua_filter_prompt(char* buffer, int buffer_size)
{
#if MODE4
    const char* prompt;

    // Call Lua to filter prompt
    lua_getglobal(g_lua, "clink");
    lua_pushliteral(g_lua, "filter_prompt");
    lua_rawget(g_lua, -2);

    lua_pushstring(g_lua, buffer);
    if (lua_pcall(g_lua, 1, 1, 0) != 0)
    {
        puts(lua_tostring(g_lua, -1));
        lua_pop(g_lua, 2);
        return;
    }

    // Collect the filtered prompt.
    prompt = lua_tostring(g_lua, -1);
    buffer[0] = '\0';
    str_cat(buffer, prompt, buffer_size);

    lua_pop(g_lua, 2);
#endif
}
