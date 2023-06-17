// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
enum hook_type { iat, detour };
typedef void (__stdcall* hookptr_t)();
typedef hookptr_t* hookptrptr_t;
bool find_iat(void* base, const char* dll, const char* func_name, bool find_by_name, hookptrptr_t* import_out, hookptr_t* original_out);
void hook_iat(hookptrptr_t import, hookptr_t hook);

//------------------------------------------------------------------------------
class hook_setter
{
    struct hook_desc
    {
        hook_type               type;
        void*                   replace;
        void*                   base;
        const char*             module;
        const char*             name;
        hookptr_t               hook;
    };

public:
                                hook_setter();
                                ~hook_setter();

    template <typename RET, typename... ARGS>
    bool                        attach(hook_type type, const char* module, const char* name, RET (__stdcall *hook)(ARGS...), hookptrptr_t original);
    template <typename RET, typename... ARGS>
    bool                        attach(hook_type type, const char* module, const char* name, RET (__stdcall *hook)(ARGS...), RET (__stdcall **original)(ARGS...));
    template <typename RET, typename... ARGS>
    bool                        detach(hook_type type, const char* module, const char* name, hookptrptr_t original, RET (__stdcall *hook)(ARGS...));
    template <typename RET, typename... ARGS>
    bool                        detach(hook_type type, const char* module, const char* name, RET (__stdcall **original)(ARGS...), RET (__stdcall *hook)(ARGS...));
    bool                        commit();

private:
    bool                        attach_internal(hook_type type, const char* module, const char* name, hookptr_t hook, hookptrptr_t original);
    bool                        detach_internal(hook_type type, const char* module, const char* name, hookptrptr_t original, hookptr_t hook);

    bool                        attach_detour(const char* module, const char* name, hookptr_t hook, hookptrptr_t original);
    bool                        attach_iat(const char* module, const char* name, hookptr_t hook, hookptrptr_t original);
    bool                        detach_detour(hookptrptr_t original, hookptr_t hook);
    bool                        detach_iat(const char* module, const char* name, hookptrptr_t original, hookptr_t hook);

    bool                        commit_iat(const hook_desc& desc);

private:
    hook_desc                   m_descs[5];
    int32                       m_desc_count = 0;
    bool                        m_pending = false;
};

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::attach(hook_type type, const char* module, const char* name, RET (__stdcall *hook)(ARGS...), hookptrptr_t original)
{
    return attach_internal(type, module, name, hookptr_t(hook), original);
}

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::attach(hook_type type, const char* module, const char* name, RET (__stdcall *hook)(ARGS...), RET (__stdcall **original)(ARGS...))
{
    return attach_internal(type, module, name, hookptr_t(hook), hookptrptr_t(original));
}

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::detach(hook_type type, const char* module, const char* name, hookptrptr_t original, RET (__stdcall *hook)(ARGS...))
{
    return detach_internal(type, module, name, original, hookptr_t(hook));
}

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::detach(hook_type type, const char* module, const char* name, RET (__stdcall **original)(ARGS...), RET (__stdcall *hook)(ARGS...))
{
    return detach_internal(type, module, name, hookptrptr_t(original), hookptr_t(hook));
}
