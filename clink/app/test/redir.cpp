// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <lib/word_collector.h>
#include <lib/cmd_tokenisers.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_state.h>

//------------------------------------------------------------------------------
class word_collector_tester
{
public:
                                word_collector_tester(collector_tokeniser* command_tokeniser,
                                                      collector_tokeniser* word_tokeniser,
                                                      const char* quote_pair=nullptr);
    void                        set_input(const char* input) { m_input = input; }
    template <class ...T> void  set_expected_words(uint32 command_offset, T... t); // T must be const char*
    void                        run();
private:
    void                        expected_words_impl(uint32 command_offset, ...);
    word_collector              m_collector;
    const char*                 m_input = nullptr;
    std::vector<const char*>    m_expected_words;
    uint32                      m_expected_command_offset;
    bool                        m_has_words = false;
};

//------------------------------------------------------------------------------
word_collector_tester::word_collector_tester(collector_tokeniser* command_tokeniser,
                                             collector_tokeniser* word_tokeniser,
                                             const char* quote_pair)
: m_collector(command_tokeniser, word_tokeniser, quote_pair)
{
}

//------------------------------------------------------------------------------
template <class ...T>
void word_collector_tester::set_expected_words(uint32 command_offset, T... t)
{
    expected_words_impl(command_offset, t..., nullptr);
}

//------------------------------------------------------------------------------
void word_collector_tester::run()
{
    REQUIRE(m_input);
    REQUIRE(m_has_words);

    // Collect words.
    std::vector<word> all_words;
    collect_words_mode mode = collect_words_mode::stop_at_cursor;
    const uint32 len = uint32(strlen(m_input));
    const uint32 command_offset = m_collector.collect_words(m_input, len, len, all_words, mode);

    commands commands;
    commands.set(m_input, len, len, all_words);

    const std::vector<word>& words = commands.get_linestate(m_input, len).get_words();

    auto report = [&] ()
    {
        printf("          input; %s#\n", m_input);
        puts("");
        printf("expected offset; %d\n", m_expected_command_offset);
        printf("            got; %d\n", command_offset);
        puts("");
        printf("expected words;\n");
        for (const char* word : m_expected_words)
            printf("  '%s'\n", word);
        printf("got;\n");
        for (const word& word : words)
        {
            if (word.length == 0 && word.offset == len)
                continue;
            printf("  '%.*s' (offset %d, length %d%s%s%s)\n",
                   word.length, m_input + word.offset,
                   word.offset, word.length,
                   word.command_word ? ", command word" : "",
                   word.is_redir_arg ? ", redir arg" : "",
                   word.quoted ? ", quoted" : "");
        }
    };

    REQUIRE(command_offset == m_expected_command_offset, report);
    REQUIRE(words.size() == m_expected_words.size() + 1, report);

    for (int32 i = 0; i < int32(m_expected_words.size()); i++)
    {
        REQUIRE(words[i].offset <= len);
        REQUIRE(words[i].offset + words[i].length <= len);

        str<> tmp;
        tmp.concat(m_input + words[i].offset, words[i].length);

        const char* expected = m_expected_words[i];
        const char* got = tmp.c_str();

        const bool expected_redir_arg = (*expected == '!');
        if (expected_redir_arg)
            expected++;

        REQUIRE(strcmp(expected, got) == 0, report);
        REQUIRE(words[i].is_redir_arg == expected_redir_arg);
    }

    const word& last_word = words.back();
    REQUIRE(last_word.length == 0);
}

//------------------------------------------------------------------------------
void word_collector_tester::expected_words_impl(uint32 command_offset, ...)
{
    m_expected_words.clear();
    m_expected_command_offset = command_offset;

    va_list arg;
    va_start(arg, command_offset);

    while (const char* word = va_arg(arg, const char*))
        m_expected_words.push_back(word);

    va_end(arg);
    m_has_words = true;
}



//------------------------------------------------------------------------------
TEST_CASE("Redir parsing")
{
    cmd_command_tokeniser command_tokeniser;
    cmd_word_tokeniser word_tokeniser;

    word_collector_tester tester(&command_tokeniser, &word_tokeniser);
    std::vector<word> words;

    SECTION("Commands")
    {
        SECTION("Plain")
        {
            tester.set_input("argcmd ");
            tester.set_expected_words(0, "argcmd");
            tester.run();
        }

        SECTION("Ampersand")
        {
            tester.set_input("nullcmd & argcmd ");
            tester.set_expected_words(10, "argcmd");
            tester.run();
        }

        SECTION("Pipe")
        {
            tester.set_input("nullcmd | argcmd ");
            tester.set_expected_words(10, "argcmd");
            tester.run();
        }

        SECTION("Ampersand 2")
        {
            tester.set_input("nullcmd && argcmd ");
            tester.set_expected_words(11, "argcmd");
            tester.run();
        }

        SECTION("Pipe 2")
        {
            tester.set_input("nullcmd || argcmd ");
            tester.set_expected_words(11, "argcmd");
            tester.run();
        }

        SECTION("Ampersand adjacent")
        {
            tester.set_input("nullcmd&&argcmd ");
            tester.set_expected_words(9, "argcmd");
            tester.run();
        }

        SECTION("Pipe adjacent")
        {
            tester.set_input("nullcmd||argcmd ");
            tester.set_expected_words(9, "argcmd");
            tester.run();
        }

        SECTION("Mixed")
        {
            tester.set_input("nullcmd |&|&| argcmd ");
            tester.set_expected_words(14, "argcmd");
            tester.run();
        }

        SECTION("Leading spaces")
        {
            tester.set_input("nullcmd &  argcmd ");
            tester.set_expected_words(10, "argcmd");
            tester.run();
        }
    }

    SECTION("Redir")
    {
        SECTION("Simple")
        {
            tester.set_input("argcmd > x y ");
            tester.set_expected_words(0, "argcmd", "!x", "y");
            tester.run();
        }

        SECTION("Simple no space")
        {
            tester.set_input("argcmd>x y ");
            tester.set_expected_words(0, "argcmd", "!x", "y");
            tester.run();
        }

        SECTION("Simple digit")
        {
            tester.set_input("argcmd 2> x y ");
            tester.set_expected_words(0, "argcmd", "!x", "y");
            tester.run();
        }

        SECTION("Simple digit no space")
        {
            tester.set_input("argcmd 2>x y ");
            tester.set_expected_words(0, "argcmd", "!x", "y");
            tester.run();
        }

        SECTION("Simple combine")
        {
            tester.set_input("argcmd 2>&1 x ");
            tester.set_expected_words(0, "argcmd", "x");
            tester.run();
        }

        SECTION("Multiple commands")
        {
            tester.set_input("nullcmd & argcmd 2>&1 x ");
            tester.set_expected_words(10, "argcmd", "x");
            tester.run();
        }

        SECTION("Multiple redirs")
        {
            tester.set_input("argcmd 2>&1 < x y ");
            tester.set_expected_words(0, "argcmd", "!x", "y");
            tester.run();
        }

        SECTION("Multiple redir args")
        {
            tester.set_input("argcmd < a > b y ");
            tester.set_expected_words(0, "argcmd", "!a", "!b", "y");
            tester.run();
        }

        SECTION("Redir end")
        {
            tester.set_input(">a argcmd >");
            tester.set_expected_words(0, "!a", "argcmd");
            tester.run();
        }
    }

    SECTION("Negative")
    {
        SECTION("Split redir symbol")
        {
            tester.set_input("argcmd 2> &1 x ");
            tester.set_expected_words(11, "1", "x");
            tester.run();
        }

        SECTION("Adjacent digit")
        {
            tester.set_input("argcmd2> x ");
            tester.set_expected_words(0, "argcmd2", "!x");
            tester.run();
        }

        SECTION("Missing digit")
        {
            tester.set_input("argcmd 2>&x ");
            tester.set_expected_words(0, "argcmd", "x");
            tester.run();
        }

        SECTION("Digit space")
        {
            tester.set_input("argcmd 2 x ");
            tester.set_expected_words(0, "argcmd", "2", "x");
            tester.run();
        }

        SECTION("Digit text")
        {
            tester.set_input("argcmd 2y x ");
            tester.set_expected_words(0, "argcmd", "2y", "x");
            tester.run();
        }

        SECTION("Digit punct")
        {
            tester.set_input("argcmd 2,5 ");
            tester.set_expected_words(0, "argcmd", "2", "5");
            tester.run();
        }

        SECTION("Stray pipe")
        {
            tester.set_input("argcmd 2>&| x ");
            tester.set_expected_words(12, "x");
            tester.run();
        }
    }
}
