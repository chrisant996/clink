// Copyright (c) 2016 Martin Ridgers
// Portions Copyright (c) 2020-2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#ifndef CLINK_USE_LUA_EDITOR_TESTER
#error This is not supported.
#endif

#include "lua_editor_tester.h"
#include "lua_match_generator.h"

#include <core/base.h>
#include <core/str.h>
#include <lib/editor_module.h>
#include <lib/matches.h>
#include <lib/word_classifier.h>
#include <lib/word_classifications.h>
#include <lib/word_collector.h>
#include <lib/cmd_tokenisers.h>
#include <terminal/printer.h>
#include <readline/readline.h>

#include <stdio.h>

//------------------------------------------------------------------------------
class lua_empty_module
    : public editor_module
{
public:
    virtual void    bind_input(binder& binder) override {}
    virtual void    on_begin_line(const context& context) override {}
    virtual void    on_end_line() override {}
    virtual void    on_input(const input& input, result& result, const context& context) override {}
    virtual void    on_matches_changed(const context& context, const line_state& line, const char* needle) override {}
    virtual void    on_terminal_resize(int columns, int rows, const context& context) override {}
    virtual void    on_signal(int sig) override {}
};



//------------------------------------------------------------------------------
class lua_test_module
    : public lua_empty_module
{
public:
    const matches*  get_matches() const;
    const word_classifications* get_classifications() const;

private:
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const context& context) override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    const matches*  m_matches = nullptr;
    const word_classifications* m_classifications = nullptr;
};

//------------------------------------------------------------------------------
const matches* lua_test_module::get_matches() const
{
    return m_matches;
}

//------------------------------------------------------------------------------
const word_classifications* lua_test_module::get_classifications() const
{
    return m_classifications;
}

//------------------------------------------------------------------------------
void lua_test_module::bind_input(binder& binder)
{
    rl_bind_keyseq("\t", rl_named_function("complete"));
}

//------------------------------------------------------------------------------
void lua_test_module::on_begin_line(const context& context)
{
    m_matches = &(context.matches);
    m_classifications = &(context.classifications);
}

//------------------------------------------------------------------------------
void lua_test_module::on_input(const input&, result& result, const context& context)
{
}



//------------------------------------------------------------------------------
lua_editor_tester::lua_editor_tester(lua_State* lua)
{
    m_printer = new printer(m_terminal_out);
    m_printer_context = new printer_context(&m_terminal_out, m_printer);
    m_command_tokeniser = new cmd_command_tokeniser();
    m_word_tokeniser = new cmd_word_tokeniser();
    m_generator = new lua_match_generator(lua);

    line_editor::desc desc(&m_terminal_in, &m_terminal_out, m_printer, nullptr);
    desc.command_tokeniser = m_command_tokeniser;
    desc.word_tokeniser = m_word_tokeniser;

    m_editor = line_editor_create(desc);
    m_editor->set_generator(*m_generator);

    // One-time initialization of Readline.
    static bool s_inited_readline = false;
    if (!s_inited_readline)
    {
        extern void initialise_readline(const char* shell_name, const char* state_dir, const char* bin_dir);
        initialise_readline("clink", nullptr, nullptr);
        s_inited_readline = true;
    }
}

//------------------------------------------------------------------------------
lua_editor_tester::~lua_editor_tester()
{
    line_editor_destroy(m_editor);

    delete m_generator;
    delete m_word_tokeniser;
    delete m_command_tokeniser;
    delete m_printer_context;
    delete m_printer;
}

//------------------------------------------------------------------------------
void lua_editor_tester::set_input(const char* input)
{
    if (input)
    {
        m_input = input;
        m_has_input = true;
    }
}

//------------------------------------------------------------------------------
void lua_editor_tester::set_expected_matches(std::vector<str_moveable>& expected)
{
    m_expected_matches.clear();
    m_expected_matches.swap(expected);
    m_has_matches = true;
}

//------------------------------------------------------------------------------
void lua_editor_tester::set_expected_classifications(const char* expected)
{
    if (expected)
    {
        m_expected_classifications = expected;
        m_has_classifications = true;
    }
}

//------------------------------------------------------------------------------
void lua_editor_tester::set_expected_output(const char* expected)
{
    if (expected)
    {
        m_expected_output = expected;
        m_has_output = true;
    }
}

//------------------------------------------------------------------------------
static const char* sanitize(const char* text)
{
    static unsigned s_i = 0;
    static str<280> s_s[4];

    str_base* s = &s_s[s_i];
    s_i = (s_i + 1) % sizeof_array(s_s);

    s->clear();
    for (const char* p = text; *p; ++p)
    {
        if (CTRL_CHAR(*p) || *p == RUBOUT)
        {
            char tmp[3] = "^?";
            if (*p != RUBOUT)
                tmp[1] = UNCTRL(*p);
            s->concat("\x1b[7m");
            s->concat(tmp, 2);
            s->concat("\x1b[27m");
        }
        else
        {
            s->concat(p, 1);
        }
    }
    return s->c_str();
}

//------------------------------------------------------------------------------
bool lua_editor_tester::run(str_base& message)
{
    message.clear();

#define REQUIRE(expr, msg) if (!(expr)) { message = msg; return false; }
#define REQUIREEX(expr, code) if (!(expr)) { code; return false; }

    const bool has_expectations = m_has_matches || m_has_classifications || m_has_output;
    REQUIRE(has_expectations, "missing expectations");

    REQUIRE(m_has_input, "missing input");
    m_terminal_in.set_input(m_input.c_str());

    // If we're expecting some matches then add a module to catch the
    // matches object.
    lua_test_module match_catch;
    m_editor->add_module(match_catch);

    // First update doesn't read input. We do however want to read at least one
    // character before bailing on the loop.
    REQUIRE(m_editor->update(), "internal input failure");
    do
    {
        REQUIRE(m_editor->update(), "internal input failure");
    }
    while (m_terminal_in.has_input());

    m_editor->update_matches();

    str<> line;
    const bool got_line = get_line(line);

    str<> tmp;

    if (m_has_matches)
    {
        const matches* matches = match_catch.get_matches();
        REQUIREEX(matches != nullptr,
        {
            message << " input = " << sanitize(m_input.c_str()) << "\n";

            if (m_has_output)
                message << "output = " << sanitize(line.c_str()) << "\n";

            message << "matches expected but none available\n";
        });

        unsigned int match_count = matches->get_match_count();
        REQUIREEX(m_expected_matches.size() == match_count,
        {
            message << " input = " << sanitize(m_input.c_str()) << "\n";

            if (m_has_output)
                message << "output = " << sanitize(line.c_str()) << "\n";

            tmp.format("expected matches = (%u)\n", m_expected_matches.size());
            message << tmp.c_str();
            for (const str_moveable& match : m_expected_matches)
                message << "  " << sanitize(match.c_str()) << "\n";

            tmp.format("actual matches = (%u)\n", matches->get_match_count());
            message << tmp.c_str();
            for (matches_iter iter = matches->get_iter(); iter.next();)
                message << "  " << sanitize(iter.get_match()) << "\n";
        });

        for (const str_moveable& expected : m_expected_matches)
        {
            bool match_found = false;

            for (matches_iter iter = matches->get_iter(); iter.next();)
                if (match_found = expected.equals(iter.get_match()))
                    break;

            REQUIREEX(match_found,
            {
                message << " input = " << sanitize(m_input.c_str()) << "\n";

                if (m_has_output)
                    message << "output = " << sanitize(line.c_str()) << "\n";

                tmp.format("match '%s' not found\n", sanitize(expected.c_str()));
                message << tmp.c_str();

                tmp.format("actual matches = (%u)\n", matches->get_match_count());
                message << tmp.c_str();
                for (matches_iter iter = matches->get_iter(); iter.next();)
                    message << "  " << sanitize(iter.get_match()) << "\n";
            });
        }
    }

    if (m_has_classifications)
    {
        const word_classifications* classifications = match_catch.get_classifications();
        REQUIREEX(classifications,
        {
            message << " input = " << sanitize(m_input.c_str()) << "\n";

            if (m_has_output)
                message << "output = " << sanitize(line.c_str()) << "\n";

            message << "classifications expected but none available\n";
        });

        str<> c;
        for (unsigned int i = 0; i < classifications->size(); ++i)
        {
            static const char c_lookup[] =
            {
                'o',    // word_class::other
                'u',    // word_class::unrecognized
                'x',    // word_class::executable
                'c',    // word_class::command
                'd',    // word_class::doskey
                'a',    // word_class::arg
                'f',    // word_class::flag
                'n',    // word_class::none
            };
            static_assert(sizeof_array(c_lookup) == int(word_class::max), "c_lookup size does not match word_class::max");

            const word_class_info& wc = *(*classifications)[i];
            if (unsigned(wc.word_class) < sizeof_array(c_lookup))
                c.concat(&c_lookup[unsigned(wc.word_class)], 1);
        }

        REQUIREEX(m_expected_classifications.equals(c.c_str()),
        {
            message << " input = " << sanitize(m_input.c_str()) << "\n";

            if (m_has_output)
                message << "output = " << sanitize(line.c_str()) << "\n";

            message << "expected classifications = " << m_expected_classifications.c_str() << "\n";
            message << "actual classifications   = " << c.c_str() << "\n";
        });
    }

    // Check the output is as expected.
    if (m_has_output)
    {
        REQUIRE(got_line, "internal output failure");
        REQUIREEX(m_expected_output.equals(line.c_str()),
        {
            message << " input = " << sanitize(m_input.c_str()) << "\n";
            message << "expected output = " << sanitize(m_expected_output.c_str()) << "\n";
            message << "actual output   = " << sanitize(line.c_str()) << "\n";
        });
    }

    m_input.clear();
    m_expected_matches.clear();
    m_expected_classifications.clear();
    m_expected_output.clear();

    m_has_input = false;
    m_has_matches = false;
    m_has_classifications = false;
    m_has_output = false;

    reset_lines();

#undef REQUIREEX
#undef REQUIRE

    return true;
}

//------------------------------------------------------------------------------
bool lua_editor_tester::get_line(str_base& line)
{
    if (!m_editor->get_line(line))
        return false;

    if (line.empty())
        line = rl_line_buffer;
    return true;
}

//------------------------------------------------------------------------------
void lua_editor_tester::reset_lines()
{
    str<> t;
    do
    {
        m_editor->get_line(t);
    }
    while (!t.empty());
}
