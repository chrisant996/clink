// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class hook_setter
{
public:
    typedef void (__stdcall *funcptr_t)();

                                hook_setter();
    template <typename RET,
             typename... ARGS>
    bool                        add_iat(void* module, const char* name, RET (__stdcall *hook)(ARGS...));
    template <typename RET,
             typename... ARGS>
    bool                        add_jmp(void* module, const char* name, RET (__stdcall *hook)(ARGS...));
    int                         commit();

private:
    enum hook_type
    {
        hook_type_iat_by_name,
        //hook_type_iat_by_addr,
        hook_type_jmp,
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
    hook_desc                   m_descs[4];
    int                         m_desc_count;
};

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::add_iat(void* module, const char* name, RET (__stdcall *hook)(ARGS...))
{
    return (add_desc(hook_type_iat_by_name, module, name, funcptr_t(hook)) != nullptr);
}

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::add_jmp(void* module, const char* name, RET (__stdcall *hook)(ARGS...))
{
    return (add_desc(hook_type_jmp, module, name, funcptr_t(hook)) != nullptr);
}
