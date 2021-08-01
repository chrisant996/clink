// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

struct repair_iat_node;

//------------------------------------------------------------------------------
enum hook_type { iat, detour };

//------------------------------------------------------------------------------
class hook_setter
{
    typedef void (__stdcall* hookptr_t)();

    struct hook_desc
    {
        hook_type               type;
        void*                   replace;
        void*                   base;
        const char*             name;
        hookptr_t               hook;
    };

public:
                                hook_setter();
                                ~hook_setter();

    template <typename RET,
              typename... ARGS>
    bool                        add(hook_type type, const char* module, const char* name, RET (__stdcall *hook)(ARGS...));
    bool                        commit();

private:
    bool                        add_internal(hook_type type, const char* module, const char* name, hookptr_t hook);
    bool                        add_iat(const char* module, const char* name, hookptr_t hook);
    bool                        add_detour(const char* module, const char* name, hookptr_t hook);
    bool                        commit_iat(void* self, const hook_desc& desc);
    void                        free_repair_list();

private:
    PVOID                       m_self = nullptr;
    repair_iat_node*            m_repair_iat = nullptr;
    hook_desc                   m_descs[5];
    int                         m_desc_count = 0;
    bool                        m_pending = false;
};

//------------------------------------------------------------------------------
template <typename RET, typename... ARGS>
bool hook_setter::add(hook_type type, const char* module, const char* name, RET (__stdcall *hook)(ARGS...))
{
    return add_internal(type, module, name, hookptr_t(hook));
}
