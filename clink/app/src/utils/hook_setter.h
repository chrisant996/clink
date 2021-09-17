// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
typedef void (__stdcall* hookptr_t)();
typedef hookptr_t* hookptrptr_t;

//------------------------------------------------------------------------------
class hook_setter
{
    struct hook_desc
    {
        void*                   replace;
    };

public:
                                hook_setter();
                                ~hook_setter();

    template <typename RET, typename... ARGS>
    bool                        attach(const char* module, const char* name, RET (__stdcall *hook)(ARGS...), hookptrptr_t original);
    template <typename RET, typename... ARGS>
    bool                        attach(const char* module, const char* name, RET (__stdcall *hook)(ARGS...), RET (__stdcall **original)(ARGS...));
    template <typename RET, typename... ARGS>
    bool                        detach(hookptrptr_t original, RET (__stdcall *hook)(ARGS...));
    template <typename RET, typename... ARGS>
    bool                        detach(RET (__stdcall **original)(ARGS...), RET (__stdcall *hook)(ARGS...));
    bool                        commit();

private:
    bool                        attach_internal(const char* module, const char* name, hookptr_t hook, hookptrptr_t original);
    bool                        detach_internal(hookptrptr_t original, hookptr_t hook);

    bool                        attach_detour(const char* module, const char* name, hookptr_t hook, hookptrptr_t original);
    bool                        detach_detour(hookptrptr_t original, hookptr_t hook);

private:
    hook_desc                   m_descs[5];
    int                         m_desc_count = 0;
    bool                        m_pending = false;
};

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::attach(const char* module, const char* name, RET (__stdcall *hook)(ARGS...), hookptrptr_t original)
{
    return attach_internal(module, name, hookptr_t(hook), original);
}

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::attach(const char* module, const char* name, RET (__stdcall *hook)(ARGS...), RET (__stdcall **original)(ARGS...))
{
    return attach_internal(module, name, hookptr_t(hook), hookptrptr_t(original));
}

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::detach(hookptrptr_t original, RET (__stdcall *hook)(ARGS...))
{
    return detach_internal(original, hookptr_t(hook));
}

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::detach(RET (__stdcall **original)(ARGS...), RET (__stdcall *hook)(ARGS...))
{
    return detach_internal(hookptrptr_t(original), hookptr_t(hook));
}
