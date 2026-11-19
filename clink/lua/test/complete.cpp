// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <lib/host_callbacks.h>
#include <lib/line_state.h>
#include <lib/matches.h>
#include <lib/rl_integration.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>
#include <rl/rl_commands.h>
#include <terminal/terminal.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
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
    bool            get_command_word(line_state& line, str_base& command_word, bool& quoted, recognition& recog, str_base& file) { return false; }

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

    line_editor::desc desc(&test_host);
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
        MAKE_CLEANUP([](){ clink_bind("\\M-\\C-n", nullptr, emacs_table); });
        clink_bind("\\M-\\C-n", "clink-complete-numbers", emacs_table); // Alt-Ctrl-N "\e\C-N" "\x1b\x0e"

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

//------------------------------------------------------------------------------
TEST_CASE("Named command completion")
{
    static const char* files[] = { "11111111", "11112222", nullptr };
    fs_fixture fs(files);

    lua_state lua;
    lua_match_generator lua_generator(lua);
    class async_test_host : public test_host
    {
    public:
        async_test_host(lua_state& lua) : test_host(lua) {}
        bool can_suggest(const line_state&) override { return true; }
        bool suggest(const line_states& lines, matches* matches, int32 generation_id) override
        {
            if (!matches)
                pending = make_match_builder_toolkit(generation_id, lines.back());
            return true;
        }
        std::shared_ptr<match_builder_toolkit> pending;
    } host(lua);
    line_editor::desc desc(&host);
    line_editor_tester tester(desc, "&|", nullptr);
    tester.get_editor()->set_generator(lua_generator);

    tester.feed_input("x 1");
    REQUIRE(host.pending);
    auto pending = std::move(host.pending);
    tester.feed_input("\x1b" "xbackward-kill-wo");
    REQUIRE(!host.pending);
    pending->get_builder()->add_match("backward-kill-wo-file", match_type::file);
    REQUIRE(!notify_matches_ready(pending, pending->get_generation_id()));
    tester.feed_input("\t");
    REQUIRE(is_reading_named_command());
    REQUIRE(g_tib->is_read_parameter());
    REQUIRE(strcmp(g_tib->get_text().c_str(), "backward-kill-word") == 0);

    SECTION("accept")
    {
        tester.feed_input("\r");
        REQUIRE(!is_reading_named_command());
        REQUIRE(!g_tib->is_read_parameter());
        REQUIRE(strcmp(g_tib->get_text().c_str(), "x ") == 0);
    }

    SECTION("Ctrl-C cancels")
    {
        tester.feed_input("\x03");
        REQUIRE(strcmp(g_tib->get_last_command_name(), "readstr-ding-cancel") == 0);
        REQUIRE(!is_reading_named_command());
        REQUIRE(!g_tib->is_read_parameter());
        REQUIRE(strcmp(g_tib->get_text().c_str(), "x 1") == 0);
    }

    SECTION("Ctrl-G cancels and resumes normal completion")
    {
        tester.feed_input("\x07");
        REQUIRE(strcmp(g_tib->get_last_command_name(), "readstr-ding-cancel") == 0);
        REQUIRE(!is_reading_named_command());
        REQUIRE(!g_tib->is_read_parameter());
        REQUIRE(strcmp(g_tib->get_text().c_str(), "x 1") == 0);
        tester.feed_input("\t");
        REQUIRE(strcmp(g_tib->get_text().c_str(), "x 1111") == 0);
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Named command completion preserves the prompt")
{
    lua_state lua;
    class prompt_test_host : public test_host
    {
    public:
        prompt_test_host(lua_state& lua) : test_host(lua) {}
        void filter_transient_prompt(bool) override
        {
            str<> prompt("new> ");
            str<> right("right");
            init_prompt(prompt, right);
            ++refilters;
        }
        int32 refilters = 0;
    } host(lua);
    line_editor::desc desc(&host);
    line_editor_tester tester(desc, "&|", nullptr);
    tester.feed_input("x");
    str<> prompt("old> ");
    str<> right;
    init_prompt(prompt, right);
    tester.feed_input("\x1b" "xglob-\t");

    class recording_terminal_out : public test_terminal_out
    {
    public:
        void write(const char* text, int32 length) override { output.concat(text, length); }
        str<> output;
    } out;
    g_terminal->redirect(&out);
    MAKE_CLEANUP([](){ g_terminal->redirect(nullptr); });

    tester.feed_input("\t");
    REQUIRE(host.refilters > 0);
    REQUIRE(strstr(out.output.c_str(), "glob-complete-word") != nullptr);
    REQUIRE(strchr(out.output.c_str(), '!') != nullptr);
    REQUIRE(strstr(out.output.c_str(), "new> ") != nullptr);
    REQUIRE(strstr(out.output.c_str(), "right") == nullptr);
    REQUIRE(g_tib->is_read_parameter());

    out.output.clear();
    g_tib->force_redisplay();
    g_tib->display();
    REQUIRE(strchr(out.output.c_str(), '!') != nullptr);

    SECTION("accept")
    {
        tib::cstring parameter;
        REQUIRE(g_tib->accept_read_parameter(parameter));
        REQUIRE(parameter == "glob-");
    }

    SECTION("cancel")
    {
        g_tib->cancel_read_parameter();
    }
    out.output.clear();
    g_tib->display();
    REQUIRE(strstr(out.output.c_str(), "new> ") != nullptr);
    REQUIRE(strstr(out.output.c_str(), "right") != nullptr);
    REQUIRE(strchr(out.output.c_str(), '!') == nullptr);
    REQUIRE(strcmp(g_tib->get_text().c_str(), "x") == 0);
}
