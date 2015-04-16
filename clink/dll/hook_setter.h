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

#ifndef HOOK_SETTER_H
#define HOOK_SETTER_H

//------------------------------------------------------------------------------
class hook_setter
{
public:
                        hook_setter();
    bool                add_iat(void* module, const char* name, void* hook);
    bool                add_jmp(void* module, const char* name, void* hook);
    bool                add_trap(void* module, const char* name, bool (*trap)());
    int                 commit();

private:
    enum hook_type
    {
        HOOK_TYPE_IAT_BY_NAME,
        //HOOK_TYPE_IAT_BY_ADDR,
        HOOK_TYPE_JMP,
        HOOK_TYPE_TRAP,
    };

    struct hook_desc
    {
        void*           module;
        const char*     name;
        void*           hook;
        hook_type       type;
    };

    hook_desc*          add_desc(hook_type type, void* module, const char* name, void* hook);
    bool                commit_iat(void* self, const hook_desc& desc);
    bool                commit_jmp(void* self, const hook_desc& desc);
    bool                commit_trap(void* self, const hook_desc& desc);
    hook_desc           m_descs[4];
    int                 m_desc_count;
};

#endif // HOOK_SETTER_H
