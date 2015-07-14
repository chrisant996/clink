// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

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
