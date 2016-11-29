// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_editor_tester.h"

#include <core/base.h>
#include <lib/editor_module.h>
#include <lib/matches.h>

#include <stdio.h>

//------------------------------------------------------------------------------
class empty_module
    : public editor_module
{
public:
    virtual void    bind_input(binder& binder) override {}
    virtual void    on_begin_line(const char* prompt, const context& context) override {}
    virtual void    on_end_line() override {}
    virtual void    on_matches_changed(const context& context) override {}
    virtual void    on_input(const input& input, result& result, const context& context) override {}
    virtual void    on_terminal_resize(int columns, int rows, const context& context) override {}
};



//------------------------------------------------------------------------------
class test_module
    : public empty_module
{
public:
    const matches*  get_matches() const;

private:
    virtual void    bind_input(binder& binder) override;
    virtual void    on_matches_changed(const context& context) override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    const matches*  m_matches = nullptr;
};

//------------------------------------------------------------------------------
const matches* test_module::get_matches() const
{
    return m_matches;
}

//------------------------------------------------------------------------------
void test_module::bind_input(binder& binder)
{
    int default_group = binder.get_group();
    binder.bind(default_group, "\b", 0); // TODO use macro instead of magic value
}

//------------------------------------------------------------------------------
void test_module::on_matches_changed(const context& context)
{
    m_matches = &(context.matches);
}

//------------------------------------------------------------------------------
void test_module::on_input(const input&, result& result, const context& context)
{
    if (context.matches.get_match_count() == 1)
        result.accept_match(0);
    else
        result.append_match_lcd();
}



//------------------------------------------------------------------------------
line_editor_tester::line_editor_tester()
{
    create_line_editor();
}

//------------------------------------------------------------------------------
line_editor_tester::line_editor_tester(const line_editor::desc& desc)
{
    create_line_editor(&desc);
}

//------------------------------------------------------------------------------
void line_editor_tester::create_line_editor(const line_editor::desc* desc)
{
    // Create a line editor.
    line_editor::desc inner_desc;
    if (desc != nullptr)
        inner_desc = *desc;
    inner_desc.input = &m_terminal_in;
    inner_desc.output = &m_terminal_out;

    m_editor = line_editor_create(inner_desc);
    REQUIRE(m_editor != nullptr);
}

//------------------------------------------------------------------------------
line_editor_tester::~line_editor_tester()
{
    line_editor_destroy(m_editor);
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
void line_editor_tester::run()
{
    bool has_expectations = m_has_matches || (m_expected_output != nullptr);
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

    if (m_has_matches)
    {
        const matches* matches = match_catch.get_matches();
        REQUIRE(matches != nullptr);

        unsigned int match_count = matches->get_match_count();
        REQUIRE(m_expected_matches.size() == match_count, [&] () {
            puts("expected;");
            for (const char* match : m_expected_matches)
                printf("  %s\n", match);

            puts("got;");
            for (int i = 0, n = matches->get_match_count(); i < n; ++i)
                printf("  %s\n", matches->get_match(i));
        });

        for (const char* expected : m_expected_matches)
        {
            bool match_found = false;

            for (unsigned int i = 0; i < match_count; ++i)
                if (match_found = (strcmp(expected, matches->get_match(i)) == 0))
                    break;

            REQUIRE(match_found);
        }
    }

    // Check the output is as expected.
    if (m_expected_output != nullptr)
    {
        char line[256];
        REQUIRE(m_editor->get_line(line, sizeof_array(line)));
        REQUIRE(strcmp(m_expected_output, line) == 0);
    }

    m_input = nullptr;
    m_expected_output = nullptr;
    m_expected_matches.clear();
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
