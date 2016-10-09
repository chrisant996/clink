// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "host.h"
#include "prompt.h"
#include "doskey.h"

#include <core/singleton.h>

class lua_state;

//------------------------------------------------------------------------------
class host_cmd
    : public host
    , public singleton<host_cmd>
{
public:
                        host_cmd();
    virtual bool        validate() override;
    virtual bool        initialise() override;
    virtual void        shutdown() override;

private:
    static BOOL WINAPI  read_console(HANDLE input, wchar_t* buffer, DWORD buffer_count, LPDWORD read_in, CONSOLE_READCONSOLE_CONTROL* control);
    static BOOL WINAPI  write_console(HANDLE handle, const wchar_t* chars, DWORD to_write, LPDWORD written, LPVOID);
    static BOOL WINAPI  set_env_var(const wchar_t* name, const wchar_t* value);
    static bool         hook_trap();
    virtual void        initialise_lua(lua_state& lua) override;
    virtual void        initialise_editor_desc(line_editor::desc& desc) override;
    void                edit_line(const wchar_t* prompt, wchar_t* chars, int max_chars);
    bool                capture_prompt(const wchar_t* chars, int char_count);
    bool                is_interactive() const;
    tagged_prompt       m_prompt;
    doskey              m_doskey;
};
