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

#ifndef DLL_HOOKS_H
#define DLL_HOOKS_H

//------------------------------------------------------------------------------
typedef enum {
    SEARCH_IAT_BY_ADDR    = 0,
    SEARCH_IAT_BY_NAME    = 1,
} search_iat_type_e;

typedef enum {
    HOOK_TYPE_IAT_BY_ADDR = SEARCH_IAT_BY_ADDR,
    HOOK_TYPE_IAT_BY_NAME = SEARCH_IAT_BY_NAME,
    HOOK_TYPE_JMP,
} hook_type_e;

typedef struct {
    hook_type_e     type;
    void*           base;           // unused by jmp-type
    const char*     dll;            // null makes iat-types search all
    void*           name_or_addr;   // name only for jmp-type
    void*           hook;
} hook_decl_t;

//------------------------------------------------------------------------------
int apply_hooks(const hook_decl_t* hooks, int hook_count);
int set_hook_trap(const char* dll, const char* func_name, int (*trap)());

#endif // DLL_HOOKS_H

// vim: expandtab
