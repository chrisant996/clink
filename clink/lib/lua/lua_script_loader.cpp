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

#include <shared/util.h>

#ifdef _DEBUG
//------------------------------------------------------------------------------
void lua_load_script_impl(lua_State* state, const char* path, const char* name)
{
    char buffer[512];
    str_cpy(buffer, path, sizeof_array(buffer));

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

    printf("CLINK DEBUG: Failed to load '%s'\n", buffer);
}
#endif // _DEBUG
