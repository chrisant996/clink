// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "host.h"

#include <core/singleton.h>
#include <lib/doskey.h>
#include <lib/cmd_tokenisers.h>
#include <lib/word_collector.h>
#include <lua/prompt.h>

class lua_state;

//------------------------------------------------------------------------------
#if defined(__MINGW32__) || defined(__MINGW64__)
#define __CONSOLE_READCONSOLE_CONTROL VOID
#else
#define __CONSOLE_READCONSOLE_CONTROL CONSOLE_READCONSOLE_CONTROL
#endif

//------------------------------------------------------------------------------
class host_cmd
    : public host
    , public singleton<host_cmd>
{
public:
                        host_cmd();
    virtual int32       validate() override;
    virtual bool        initialise() override;
    virtual void        shutdown() override;

private:
    static BOOL WINAPI  read_console(HANDLE input, void* buffer, DWORD buffer_count, LPDWORD read_in, __CONSOLE_READCONSOLE_CONTROL* control);
    static BOOL WINAPI  write_console(HANDLE handle, const void* chars, DWORD to_write, LPDWORD written, LPVOID);
    static BOOL WINAPI  set_env_var(const wchar_t* name, const wchar_t* value);
    static DWORD WINAPI get_env_var(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize);
    static BOOL WINAPI  set_console_title(LPCWSTR lpConsoleTitle);
    bool                initialise_system();
    virtual void        initialise_lua(lua_state& lua) override;
    virtual void        initialise_editor_desc(line_editor::desc& desc) override;
    void                make_aliases(str_base& clink, str_base& history);
    void                add_aliases(bool force);
    void                edit_line(wchar_t* chars, int32 max_chars, bool edit=true);
    bool                capture_prompt(const wchar_t* chars, int32 char_count);
    bool                is_interactive() const;
    tagged_prompt       m_prompt;
    doskey              m_doskey;
    cmd_command_tokeniser m_command_tokeniser;
    cmd_word_tokeniser  m_word_tokeniser;
};
