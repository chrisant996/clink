// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "host.h"
#include "prompt.h"

#include <core/singleton.h>
#include <lib/doskey.h>
#include <lib/word_collector.h>

class lua_state;

//------------------------------------------------------------------------------
class host_cmd
    : public host
    , public singleton<host_cmd>
{
    class command_tokeniser : public collector_tokeniser
    {
    public:
        void start(const str_iter& iter, const char* quote_pair) override;
        str_token next(unsigned int& offset, unsigned int& length) override;
    private:
        const char* m_start;
        str_tokeniser m_tokeniser;
        bool m_first;
    };

    class word_tokeniser : public collector_tokeniser
    {
    public:
        void start(const str_iter& iter, const char* quote_pair) override;
        str_token next(unsigned int& offset, unsigned int& length) override;
    private:
        const char* m_start;
        str_tokeniser m_tokeniser;
    };

public:
                        host_cmd();
    virtual int         validate() override;
    virtual bool        initialise() override;
    virtual void        shutdown() override;

private:
    static BOOL WINAPI  read_console(HANDLE input, wchar_t* buffer, DWORD buffer_count, LPDWORD read_in, CONSOLE_READCONSOLE_CONTROL* control);
    static BOOL WINAPI  write_console(HANDLE handle, const wchar_t* chars, DWORD to_write, LPDWORD written, LPVOID);
    static BOOL WINAPI  set_env_var(const wchar_t* name, const wchar_t* value);
#if 0
    static DWORD WINAPI format_message(DWORD flags, LPCVOID source, DWORD messageId, DWORD languageId, wchar_t* buffer, DWORD size, va_list* arguments);
#endif
    bool                initialise_system();
    virtual void        initialise_lua(lua_state& lua) override;
    virtual void        initialise_editor_desc(line_editor::desc& desc) override;
    void                make_aliases(str_base& clink, str_base& history);
    void                add_aliases(bool force);
    void                edit_line(wchar_t* chars, int max_chars);
    bool                capture_prompt(const wchar_t* chars, int char_count);
    bool                is_interactive() const;
    tagged_prompt       m_prompt;
    doskey              m_doskey;
    doskey_alias        m_doskey_alias;
    command_tokeniser   m_command_tokeniser;
    word_tokeniser      m_word_tokeniser;
};
