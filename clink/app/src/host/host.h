// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "terminal/terminal.h"

#include <lib/doskey.h>
#include <lib/line_editor.h>
#include <lib/history_db.h>
#include <lib/host_callbacks.h>

#include <list>
#include <memory>

class lua_state;
class str_base;
class host_lua;
class prompt_filter;
class suggester;
class printer_context;

//------------------------------------------------------------------------------
enum class dequeue_flags
{
    none            = 0x00,
    hide_prompt     = 0x01,
    show_line       = 0x02,
    edit_line       = 0x04,
};
DEFINE_ENUM_FLAG_OPERATORS(dequeue_flags);

inline bool check_dequeue_flag(const dequeue_flags check, const dequeue_flags mask)
{
    return (check & mask) != dequeue_flags::none;
}

//------------------------------------------------------------------------------
class host : public host_callbacks
{
    struct queued_line
    {
        queued_line(str_moveable&& line, dequeue_flags flags)
            : m_line(std::move(line)), m_flags(flags) {}
        str_moveable m_line;
        dequeue_flags m_flags;
    };

public:
                    host(const char* name);
    virtual         ~host();
    virtual int32   validate() = 0;
    virtual bool    initialise() = 0;
    virtual void    shutdown() = 0;

    const char*     filter_prompt(const char** rprompt, bool& ok, bool transient=false, bool final=false);
    void            enqueue_lines(std::list<str_moveable>& lines, bool hide_prompt, bool show_line);
    bool            dequeue_line(wstr_base& out, dequeue_flags& flags);
    bool            dequeue_char(wchar_t* out);
    void            cleanup_after_signal();

    // host_callbacks:
    void            filter_prompt() override;
    void            filter_transient_prompt(bool final) override;
    bool            can_suggest(const line_state& line) override;
    bool            suggest(const line_states& lines, matches* matches, int32 generation_id) override;
    bool            filter_matches(char** matches) override;
    bool            call_lua_rl_global_function(const char* func_name, const line_state* line) override;
    const char**    copy_dir_history(int32* total) override;
    void            send_event(const char* event_name) override;
    void            send_oncommand_event(line_state& line, const char* command, bool quoted, recognition recog, const char* file) override;
    void            send_oninputlinechanged_event(const char* line) override;
    bool            has_event_handler(const char* event_name) override;
    void            get_app_context(int32& id, host_context& context) override;

protected:
    std::unique_ptr<printer_context> make_printer_context();
    bool            edit_line(const char* prompt, const char* rprompt, str_base& out, bool edit=true);
    virtual void    initialise_lua(lua_state& lua) = 0;
    virtual void    initialise_editor_desc(line_editor::desc& desc) = 0;

private:
    void            purge_old_files();
    void            update_last_cwd();
    void            pop_queued_line();

private:
    const char*     m_name;
    doskey          m_doskey;
    terminal        m_terminal;
    printer*        m_printer;
    host_lua*       m_lua = nullptr;
    prompt_filter*  m_prompt_filter = nullptr;
    suggester*      m_suggester = nullptr;
    const char*     m_prompt = nullptr;
    const char*     m_rprompt = nullptr;
    str<256>        m_filtered_prompt;
    str<256>        m_filtered_rprompt;
    str_moveable    m_pending_command;
    std::list<queued_line> m_queued_lines;
    uint32          m_char_cursor = 0;
    wstr_moveable   m_last_cwd;
    bool            m_can_transient = false;
    bool            m_skip_provide_line = false;
    bool            m_bypass_dequeue = false;
    dequeue_flags   m_bypass_flags = dequeue_flags::none;
};
