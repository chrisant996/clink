// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class hook_setter
{
public:
    typedef void (*funcptr_t)();

                                hook_setter();
    template <typename T> bool  add_iat(void* module, const char* name, T* hook);
    template <typename T> bool  add_jmp(void* module, const char* name, T* hook);
    bool                        add_trap(void* module, const char* name, bool (*trap)());
    int                         commit();

private:
    enum hook_type
    {
        hook_type_iat_by_name,
        //hook_type_iat_by_addr,
        hook_type_jmp,
        hook_type_trap,
    };

    struct hook_desc
    {
        void*                   module;
        const char*             name;
        funcptr_t               hook;
        hook_type               type;
    };

    hook_desc*                  add_desc(hook_type type, void* module, const char* name, funcptr_t hook);
    bool                        commit_iat(void* self, const hook_desc& desc);
    bool                        commit_jmp(void* self, const hook_desc& desc);
    bool                        commit_trap(void* self, const hook_desc& desc);
    hook_desc                   m_descs[4];
    int                         m_desc_count;
};

//------------------------------------------------------------------------------
template <typename T>
bool hook_setter::add_iat(void* module, const char* name, T* hook)
{
    return (add_desc(hook_type_iat_by_name, module, name, funcptr_t(hook)) != nullptr);
}

//------------------------------------------------------------------------------
template <typename T>
bool hook_setter::add_jmp(void* module, const char* name, T* hook)
{
    return (add_desc(hook_type_jmp, module, name, funcptr_t(hook)) != nullptr);
}
