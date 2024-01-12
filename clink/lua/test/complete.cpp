// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <lib/host_callbacks.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>
#include <rl/rl_commands.h>
#include <terminal/terminal.h>
#include <terminal/printer.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
extern int _rl_last_v_pos;
}

//------------------------------------------------------------------------------
class test_host : public host_callbacks
{
public:
                    test_host(lua_state& lua) : m_lua(lua) {}

    void            filter_prompt() { assert(false); }
    void            filter_transient_prompt(bool final) {}
    bool            can_suggest(const line_state& line) { return false; }
    bool            suggest(const line_states& lines, matches* matches, int32 generation_id) { assert(false); return false; }
    bool            filter_matches(char** matches) { return false; }
    bool            call_lua_rl_global_function(const char* func_name, const line_state* line) { return m_lua.call_lua_rl_global_function(func_name, line); }
    const char**    copy_dir_history(int32* total) { assert(false); return nullptr; }
    void            send_event(const char* event_name) {}
    void            send_oncommand_event(line_state& line, const char* command, bool quoted, recognition recog, const char* file) {}
    void            send_oninputlinechanged_event(const char* line) {}
    bool            has_event_handler(const char* event_name) { return false; }

private:
    lua_state&      m_lua;
};

//------------------------------------------------------------------------------
TEST_CASE("Rl matches")
{
    static const char* dir_fs[] = {
        "11111111",
        "11112222",
        "11223344",
        nullptr,
    };

    fs_fixture fs(dir_fs);

    lua_state lua;
    lua_match_generator lua_generator(lua);
    test_host test_host(lua);

    lua_load_script(lua, app, commands);

    terminal term = terminal_create();
    printer printer(*term.out);

    line_editor::desc desc(nullptr, term.out, &printer, &test_host);
    line_editor_tester tester(desc, "&|", nullptr);
    tester.get_editor()->set_generator(lua_generator);

    SECTION("normal completion")
    {
        tester.set_input("x 1\t");
        tester.set_expected_matches("11111111", "11112222", "11223344");
        tester.run();
    }

    SECTION("numbers completion")
    {
        MAKE_CLEANUP([](){ rl_unbind_key_in_map(0x0e, emacs_meta_keymap); });
        rl_bind_keyseq_in_map("\\C-N", clink_complete_numbers, emacs_meta_keymap); // Alt-Ctrl-N "\e\C-N" "\x1b\x0e"

        // Must have at least two matching matches, or clink_complete_numbers
        // will just insert the single matching match and not actually update
        // the list of matches.
        const char* script = "\
            function console.screengrab() \
                return { '12345', '13579', '99999', } \
            end \
        ";

        REQUIRE_LUA_DO_STRING(lua, script);

        // This is testing both clink-complete-numbers and rl.setmatches().
        tester.set_input("x 1\x1b\x0e");
        tester.set_expected_matches("12345", "13579");
        tester.run();
    }
}
