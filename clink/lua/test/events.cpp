// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/os.h>
#include <core/settings.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>
#include <lib/cmd_tokenisers.h>
#include <lib/display_matches.h>
#include <lib/host_callbacks.h>

extern "C" {
#include <lua.h>
}

#define CTRL_A "\x01"           // beginning-of-line
#define CTRL_B "\x02"           // backward-char
#define CTRL_E "\x05"           // end-of-line
#define CTRL_F "\x06"           // forward-char
#define META_B "\033b"          // backward-word
#define META_F "\033f"          // forward-word

//------------------------------------------------------------------------------
namespace {

class test_host : public host_callbacks
{
public:
                    test_host(lua_state& lua, lua_match_generator& generator) : m_lua(lua), m_generator(generator) {}

    void            filter_prompt() { assert(false); }
    void            filter_transient_prompt(bool final) {}
    bool            can_suggest(const line_state& line) { return false; }
    bool            suggest(const line_states& lines, matches* matches, int32 generation_id) { assert(false); return false; }
    bool            filter_matches(char** matches) { return m_generator.filter_matches(matches, rl_completion_type, rl_filename_completion_desired); }
    bool            call_lua_rl_global_function(const char* func_name, const line_state* line) { return m_lua.call_lua_rl_global_function(func_name, line); }
    const char**    copy_dir_history(int32* total) { assert(false); return nullptr; }
    void            send_event(const char* event_name) {}
    void            send_oncommand_event(line_state& line, const char* command, bool quoted, recognition recog, const char* file) {}
    void            send_oninputlinechanged_event(const char* line) {}
    bool            has_event_handler(const char* event_name) { return false; }
    bool            get_command_word(line_state& line, str_base& command_word, bool& quoted, recognition& recog, str_base& file) { return false; }

private:
    lua_state&      m_lua;
    lua_match_generator& m_generator;
};

}

//------------------------------------------------------------------------------
TEST_CASE("Lua events")
{
    fs_fixture fs;

    lua_state lua;
    lua_match_generator lua_generator(lua);
    test_host test_host(lua, lua_generator);

    lua_load_script(lua, app, exec);

    cmd_command_tokeniser command_tokeniser;
    cmd_word_tokeniser word_tokeniser;

    line_editor::desc desc(nullptr, nullptr, nullptr, &test_host);
    desc.command_tokeniser = &command_tokeniser;
    desc.word_tokeniser = &word_tokeniser;
    line_editor_tester tester(desc, nullptr, nullptr);
    tester.get_editor()->set_generator(lua_generator);

    SECTION("onfiltermatches")
    {
        const char* script = "\
            local function onfilter(matches)\
                for i = #matches, 1, -1 do\
                    if matches[i].match == 'bbb' then\
                        table.remove(matches, i)\
                    end\
                end\
                return matches\
            end\
            local function init()\
                clink.onfiltermatches(onfilter)\
                return {}\
            end\
            clink.argmatcher('xyz'):addarg({\
                'aaa',\
                'bbb',\
                'ccc',\
                init,\
            })\
        ";

        REQUIRE_LUA_DO_STRING(lua, script);

        tester.set_input("xyz a" DO_COMPLETE);
        tester.set_expected_output("xyz aaa ");
        tester.run();

        tester.set_input("xyz b" DO_COMPLETE);
        tester.set_expected_output("xyz b");
        tester.run();

        tester.set_input("xyz c" DO_COMPLETE);
        tester.set_expected_output("xyz ccc ");
        tester.run();
    }

    // TODO: ondisplaymatches
}
