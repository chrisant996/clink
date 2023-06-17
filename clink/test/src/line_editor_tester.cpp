// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_editor_tester.h"
#include "terminal/printer.h"

#include <core/base.h>
#include <core/str.h>
#include <lib/editor_module.h>
#include <lib/matches.h>
#include <lib/word_classifier.h>
#include <lib/word_classifications.h>
#include <lib/word_collector.h>
#include <lib/cmd_tokenisers.h>
#include <readline/readline.h>

#include <stdio.h>

//------------------------------------------------------------------------------
class empty_module
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
class test_module
    : public empty_module
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
const matches* test_module::get_matches() const
{
    return m_matches;
}

//------------------------------------------------------------------------------
const word_classifications* test_module::get_classifications() const
{
    return m_classifications;
}

//------------------------------------------------------------------------------
void test_module::bind_input(binder& binder)
{
    rl_bind_keyseq("\t", rl_named_function("complete"));
}

//------------------------------------------------------------------------------
void test_module::on_begin_line(const context& context)
{
    m_matches = &(context.matches);
    m_classifications = &(context.classifications);
}

//------------------------------------------------------------------------------
void test_module::on_input(const input&, result& result, const context& context)
{
}



//------------------------------------------------------------------------------
line_editor_tester::line_editor_tester()
{
    create_line_editor();
}

//------------------------------------------------------------------------------
line_editor_tester::line_editor_tester(const line_editor::desc& _desc, const char* command_delims, const char* word_delims)
{
    line_editor::desc desc(_desc);

    if (command_delims)
        m_command_tokeniser = new simple_word_tokeniser(command_delims);
    else
        m_command_tokeniser = new cmd_command_tokeniser();
    if (word_delims)
        m_word_tokeniser = new simple_word_tokeniser(word_delims);
    else
        m_word_tokeniser = new cmd_word_tokeniser();

    desc.command_tokeniser = m_command_tokeniser;
    desc.word_tokeniser = m_word_tokeniser;

    create_line_editor(&desc);
}

//------------------------------------------------------------------------------
void line_editor_tester::create_line_editor(const line_editor::desc* desc)
{
    // Create a line editor.
    line_editor::desc inner_desc(nullptr, nullptr, nullptr, nullptr);
    if (desc != nullptr)
        inner_desc = *desc;

    m_printer = new printer(m_terminal_out);

    m_printer_context = new printer_context(&m_terminal_out, m_printer);

    inner_desc.input = &m_terminal_in;
    inner_desc.output = &m_terminal_out;
    inner_desc.printer = m_printer;

    m_editor = line_editor_create(inner_desc);
    REQUIRE(m_editor != nullptr);
}

//------------------------------------------------------------------------------
line_editor_tester::~line_editor_tester()
{
    delete m_printer_context;
    line_editor_destroy(m_editor);
    delete m_printer;
}

//------------------------------------------------------------------------------
line_editor* line_editor_tester::get_editor() const
{
    return m_editor;
}

//------------------------------------------------------------------------------
void line_editor_tester::set_input(const char* input)
{
    m_input = input;
}

//------------------------------------------------------------------------------
void line_editor_tester::set_expected_output(const char* expected)
{
    m_expected_output = expected;
}

//------------------------------------------------------------------------------
static const char* sanitize(const char* text)
{
    static str<280> s_s;
    s_s.clear();
    for (const char* p = text; *p; ++p)
    {
        if (CTRL_CHAR(*p) || *p == RUBOUT)
        {
            char tmp[3] = "^?";
            if (*p != RUBOUT)
                tmp[1] = UNCTRL(*p);
            s_s.concat("\x1b[7m");
            s_s.concat(tmp, 2);
            s_s.concat("\x1b[27m");
        }
        else
        {
            s_s.concat(p, 1);
        }
    }
    return s_s.c_str();
}

//------------------------------------------------------------------------------
void line_editor_tester::run(bool expectationless)
{
    bool has_expectations = expectationless;
    has_expectations |= m_has_matches || m_has_classifications || (m_expected_output != nullptr);
    REQUIRE(has_expectations);

    REQUIRE(m_input != nullptr);
    m_terminal_in.set_input(m_input);

    // If we're expecting some matches then add a module to catch the
    // matches object.
    test_module match_catch;
    m_editor->add_module(match_catch);

    // First update doesn't read input. We do however want to read at least one
    // character before bailing on the loop.
    REQUIRE(m_editor->update());
    do
    {
        REQUIRE(m_editor->update());
    }
    while (m_terminal_in.has_input());

    m_editor->update_matches();

    if (m_has_matches)
    {
        const matches* matches = match_catch.get_matches();
        REQUIRE(matches != nullptr);

        unsigned int match_count = matches->get_match_count();
        REQUIRE(m_expected_matches.size() == match_count, [&] () {
            printf(" input; %s#\n", sanitize(m_input));

            str<> line;
            get_line(line);
            printf("output; %s#\n", sanitize(line.c_str()));

            puts("\nexpected;");
            for (const char* match : m_expected_matches)
                printf("  %s\n", sanitize(match));

            puts("\ngot;");
            for (matches_iter iter = matches->get_iter(); iter.next();)
                printf("  %s\n", sanitize(iter.get_match()));
        });

        for (const char* expected : m_expected_matches)
        {
            bool match_found = false;

            for (matches_iter iter = matches->get_iter(); iter.next();)
                if (match_found = (strcmp(expected, iter.get_match()) == 0))
                    break;

            REQUIRE(match_found, [&] () {
                printf("match '%s' not found\n", sanitize(expected));

                puts("\ngot;");
                for (matches_iter iter = matches->get_iter(); iter.next();)
                    printf("  %s\n", sanitize(iter.get_match()));
            });
        }
    }

    if (m_has_classifications)
    {
        const word_classifications* classifications = match_catch.get_classifications();
        REQUIRE(classifications, [&]() {
            printf(" input; %s\n", sanitize(m_input));

            puts("expected classifications but got none");
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

        REQUIRE(m_expected_classifications.equals(c.c_str()), [&] () {
            printf(" input; %s#\n", sanitize(m_input));

            puts("\nexpected classifications;");
            printf("  %s\n", m_expected_classifications.c_str());

            puts("\ngot;");
            printf("  %s\n", c.c_str());
        });
    }

    // Check the output is as expected.
    if (m_expected_output != nullptr)
    {
        str<> line;
        REQUIRE(get_line(line));
        REQUIRE(strcmp(m_expected_output, line.c_str()) == 0, [&] () {
            printf("       input; %s#\n", sanitize(m_input));
            printf("out expected; %s#\n", sanitize(m_expected_output));
            printf("     out got; %s#\n", sanitize(line.c_str()));
        });
    }

    m_input = nullptr;
    m_expected_output = nullptr;
    m_expected_matches.clear();
    m_expected_classifications.clear();

    reset_lines();
}

//------------------------------------------------------------------------------
void line_editor_tester::expected_matches_impl(int dummy, ...)
{
    m_expected_matches.clear();

    va_list arg;
    va_start(arg, dummy);

    while (const char* match = va_arg(arg, const char*))
        m_expected_matches.push_back(match);

    va_end(arg);
    m_has_matches = true;
}

//------------------------------------------------------------------------------
void line_editor_tester::set_expected_matches_list(const char* const* expected)
{
    m_expected_matches.clear();

    while (*expected)
        m_expected_matches.push_back(*(expected++));

    m_has_matches = true;
}

//------------------------------------------------------------------------------
bool line_editor_tester::get_line(str_base& line)
{
    if (!m_editor->get_line(line))
        return false;

    if (line.empty())
        line = rl_line_buffer;
    return true;
}

//------------------------------------------------------------------------------
void line_editor_tester::reset_lines()
{
    str<> t;
    do
    {
        m_editor->get_line(t);
    }
    while (!t.empty());
}

//------------------------------------------------------------------------------
void line_editor_tester::set_expected_classifications(const char* classifications)
{
    m_expected_classifications = classifications;
    m_has_classifications = true;
}
