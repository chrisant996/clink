// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_editor_tester.h"

#include <lib/editor_backend.h>
#include <lib/line_editor.h>
#include <lib/matches.h>

//------------------------------------------------------------------------------
class test_backend
    : public editor_backend
{
public:
    const matches*          get_matches() const { return m_matches; }

    /* editor_backend */
    virtual void            bind(binder_func* func) override {}
    virtual void            begin_line(const char* prompt, const context& context) override {}
    virtual void            end_line() override {}
    virtual void            on_matches_changed(const context& context) override { m_matches = &(context.matches); }
    virtual result          on_input(const char* keys, int id, const context& context) override { return result::next; }

private:
    const matches*          m_matches = nullptr;
};



//------------------------------------------------------------------------------
line_editor_tester::line_editor_tester()
{
    // Create a line editor.
    line_editor::desc desc;
    desc.terminal = &m_test_termbuf;
    desc.buffer = &m_test_termbuf;
    m_editor = line_editor_create(desc);
    REQUIRE(m_editor != nullptr);
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
    REQUIRE(false); // MODE4: Not implemented.
    m_expected_output = expected;
}

//------------------------------------------------------------------------------
void line_editor_tester::run()
{
    bool has_expectations = m_has_matches || (m_expected_output != nullptr);
    REQUIRE(has_expectations);

    REQUIRE(m_input != nullptr);
    m_test_termbuf.set_input(m_input);

    // If we're expecting some matches then add a backend to catch the
    // matches object.
    test_backend match_catch;
    if (m_has_matches)
        m_editor->add_backend(match_catch);

    // First update doesn't read input. We do however want to read at least one
    // character before bailing on the loop.
    REQUIRE(m_editor->update());
    do
    {
        REQUIRE(m_editor->update());
    }
    while (m_test_termbuf.has_input());

    if (m_has_matches)
    {
        const matches* matches = match_catch.get_matches();
        REQUIRE(matches != nullptr);

        unsigned int match_count = matches->get_match_count();
        REQUIRE(m_expected_matches.size() == match_count);

        for (const char* expected : m_expected_matches)
        {
            bool found = false;

            for (unsigned int i = 0; i < match_count; ++i)
                if (strcmp(expected, matches->get_match(i)) == 0)
                    if (found = true)
                        break;

            REQUIRE(found);
        }
    }
}

//------------------------------------------------------------------------------
void line_editor_tester::expected_matches_impl(int dummy, ...)
{
    va_list arg;
    va_start(arg, dummy);

    while (const char* match = va_arg(arg, const char*))
        m_expected_matches.push_back(match);

    va_end(arg);
    m_has_matches = true;
}
