// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

struct repair_iat_node;

//------------------------------------------------------------------------------
class hook_setter
{
    typedef void (__stdcall* hookptr_t)();

    struct hook_iat_desc
    {
        void*                   base;
        const char*             name;
        hookptr_t               hook;
    };

public:
                                hook_setter();
                                ~hook_setter();

    template <typename RET,
              typename... ARGS>
    bool                        add_iat(const char* module, const char* name, RET (__stdcall *hook)(ARGS...));
    template <typename RET,
              typename... ARGS>
    bool                        add_jmp(const char* module, const char* name, RET (__stdcall *hook)(ARGS...));
    bool                        commit();

private:
    bool                        add_desc(const char* module, const char* name, hookptr_t hook);
    bool                        add_detour(const char* module, const char* name, hookptr_t hook);
    bool                        commit_iat(void* self, const hook_iat_desc& desc);
    void                        free_repair_list();

private:
    PVOID                       m_self = nullptr;
    repair_iat_node*            m_repair_iat = nullptr;
    hook_iat_desc               m_descs[4];
    int                         m_desc_count = 0;
    bool                        m_pending = false;
};

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::add_iat(const char* module, const char* name, RET (__stdcall *hook)(ARGS...))
{
    return add_desc(module, name, hookptr_t(hook));
}

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::add_jmp(const char* module, const char* name, RET (__stdcall *hook)(ARGS...))
{
    return add_detour(module, name, hookptr_t(hook));
}
