// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_editor_tester.h"
#include "terminal/printer.h"

#include <core/base.h>
#include <core/str.h>
#include <lib/editor_module.h>
#include <lib/matches.h>
#include <lib/hinter.h>
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
    virtual void    on_need_input(int32& bind_group) override {}
    virtual void    on_input(const input& input, result& result, const context& context) override {}
    virtual void    on_matches_changed(const context& context, const line_state& line, const char* needle) override {}
    virtual void    on_terminal_resize(int32 columns, int32 rows, const context& context) override {}
    virtual void    on_signal(int32 sig) override {}
};



//------------------------------------------------------------------------------
class test_module
    : public empty_module
{
public:
    test_module(const char* tab_binding=nullptr) : m_tab_binding(tab_binding) {}
    ~test_module() { delete m_line_state; }
    void set_need_line_state(bool need);
    const matches*  get_matches() const;
    const line_state* get_line_state() const;
    const word_classifications* get_classifications() const;
    const char* get_input_hint() const;

private:
    virtual void    bind_input(binder& binder) override;
    virtual void    on_begin_line(const context& context) override;
    virtual void    on_input(const input& input, result& result, const context& context) override;
    virtual void    on_matches_changed(const context& context, const line_state& line, const char* needle) override;
    const matches*  m_matches = nullptr;
    const line_state* m_line_state = nullptr;
    const word_classifications* m_classifications = nullptr;
    const input_hint* m_input_hint = nullptr;
    bool            m_need_line_state = false;
    const char*     m_tab_binding = nullptr;
};

//------------------------------------------------------------------------------
void test_module::set_need_line_state(bool need)
{
    delete m_line_state;
    m_line_state = nullptr;
    m_need_line_state = need;
}

//------------------------------------------------------------------------------
const matches* test_module::get_matches() const
{
    return m_matches;
}

//------------------------------------------------------------------------------
const line_state* test_module::get_line_state() const
{
    return m_line_state;
}

//------------------------------------------------------------------------------
const word_classifications* test_module::get_classifications() const
{
    return m_classifications;
}

//------------------------------------------------------------------------------
const char* test_module::get_input_hint() const
{
    return (m_input_hint && !m_input_hint->empty()) ? m_input_hint->c_str() : nullptr;
}

//------------------------------------------------------------------------------
void test_module::bind_input(binder& binder)
{
    rl_bind_keyseq("\t", rl_named_function(m_tab_binding ? m_tab_binding : "complete"));
}

//------------------------------------------------------------------------------
void test_module::on_begin_line(const context& context)
{
    m_matches = &(context.matches);
    m_classifications = &(context.classifications);
    m_input_hint = &(context.input_hint);
}

//------------------------------------------------------------------------------
void test_module::on_input(const input&, result& result, const context& context)
{
    assert(&(context.input_hint) == m_input_hint);
}

//------------------------------------------------------------------------------
void test_module::on_matches_changed(const context& context, const line_state& line, const char* needle)
{
    if (m_need_line_state)
    {
        delete m_line_state;
        m_line_state = new line_state(line);
    }
}



//------------------------------------------------------------------------------
bool clipboard_tester::get_clipboard_text(str_base& out)
{
    to_utf8(out, m_text.c_str());
    return !m_text.empty();
}

//------------------------------------------------------------------------------
bool clipboard_tester::set_clipboard_text(const char* text, int32 length)
{
    int32 cch = 0;
    if (length)
    {
        int32 cch = MultiByteToWideChar(CP_UTF8, 0, text, length, nullptr, 0);
        if (!cch)
            return false;
    }

    m_text.clear();
    to_utf16(m_text, str_iter(text, length));
    return true;
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
void line_editor_tester::set_tab_binding(const char* tab_binding)
{
    m_tab_binding = tab_binding;
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
    has_expectations |= m_has_matches || m_has_words || m_has_classifications || m_has_faces || m_has_hint || m_expected_output;
    REQUIRE(has_expectations);

    REQUIRE(m_input != nullptr);
    m_terminal_in.set_input(m_input);

    // If we're expecting some matches then add a module to catch the
    // matches object.
    test_module match_catch(m_tab_binding);
    if (m_has_words)
        match_catch.set_need_line_state(true);
    m_editor->add_module(match_catch);

    // First update doesn't read input. We do however want to read at least one
    // character before bailing on the loop.
    REQUIRE(m_editor->update());
    do
    {
        REQUIRE(m_editor->update());
    }
    while (m_terminal_in.available(0));

    m_editor->update_matches();

    if (m_has_matches)
    {
        const matches* matches = match_catch.get_matches();
        REQUIRE(matches != nullptr);

        uint32 match_count = matches->get_match_count();
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

    if (m_has_words)
    {
        const line_state* line_state = match_catch.get_line_state();
        REQUIRE(line_state != nullptr);

        std::vector<str_moveable> words;
        for (uint32 i = 0; i < line_state->get_word_count(); ++i)
        {
            str_moveable s;
            str_iter iter(line_state->get_word(i));
            s.concat(iter.get_pointer(), iter.length());
            words.emplace_back(std::move(s));
        }

        REQUIRE(m_expected_words.size() == words.size(), [&] () {
            printf("input; %s#\n", sanitize(m_input));

            puts("\nexpected words;");
            for (const char* word : m_expected_words)
                printf("  '%s'\n", sanitize(word));

            puts("\ngot;");
            for (const auto& word : words)
                printf("  '%s'\n", sanitize(word.c_str()));
        });

        for (uint32 i = 0; i < words.size(); ++i)
        {
            REQUIRE(strcmp(words[i].c_str(), m_expected_words[i]) == 0, [&] () {
                printf("word %u (%s) does not match\n", i, sanitize(words[i].c_str()));

                printf("\ninput; %s#\n", sanitize(m_input));

                puts("\nexpected words;");
                for (const char* word : m_expected_words)
                    printf("  '%s'\n", sanitize(word));

                puts("\ngot;");
                for (const auto& word : words)
                    printf("  '%s'\n", sanitize(word.c_str()));
            });
        }
    }

    if (m_has_classifications)
    {
        const word_classifications* classifications = match_catch.get_classifications();
        REQUIRE(classifications, [&]() {
            printf(" input; %s#\n", sanitize(m_input));

            puts("expected classifications but got none");
        });

        str<> c;
        for (uint32 i = 0; i < classifications->size(); ++i)
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
            static_assert(sizeof_array(c_lookup) == int32(word_class::max), "c_lookup size does not match word_class::max");

            const word_class_info& wc = *(*classifications)[i];
            if (m_mark_argmatchers && wc.argmatcher)
                c.concat("m", 1);
            if (unsigned(wc.word_class) < sizeof_array(c_lookup))
                c.concat(&c_lookup[unsigned(wc.word_class)], 1);
            else
                c.concat(" ", 1);
        }

        while (c.length() && c.c_str()[c.length() - 1] == ' ')
            c.truncate(c.length() - 1);

        REQUIRE(m_expected_classifications.equals(c.c_str()), [&] () {
            printf(" input; %s#\n", sanitize(m_input));

            puts("\nexpected classifications;");
            printf("  %s\n", m_expected_classifications.c_str());

            puts("\ngot;");
            printf("  %s\n", c.c_str());
        });
    }

    if (m_has_faces)
    {
        const word_classifications* classifications = match_catch.get_classifications();
        REQUIRE(classifications, [&]() {
            printf(" input; %s#\n", sanitize(m_input));

            puts("expected classifications but got none");
        });

        str<> c;
        for (uint32 i = 0; i < classifications->length(); ++i)
        {
            char face = classifications->get_face(i);
            c.concat(&face, 1);
        }

        REQUIRE(m_expected_faces.equals(c.c_str()), [&] () {
            printf(" input; %s#\n", sanitize(m_input));

            puts("\nexpected faces;");
            printf("  %s\n", m_expected_faces.c_str());

            puts("\ngot;");
            printf("  %s\n", c.c_str());
        });
    }

    if (m_has_hint)
    {
        const char* hint = match_catch.get_input_hint();
        if (m_expected_hint && !hint)
        {
            REQUIRE(hint, [&]() {
                printf(" input; %s#\n", sanitize(m_input));

                puts("expected input hint but got none");
            });
        }
        else if (!m_expected_hint && hint)
        {
            REQUIRE(!hint, [&]() {
                printf(" input; %s#\n", sanitize(m_input));

                printf("expected no input hint but got \"%s\"\n", hint);
            });
        }
        else if (m_expected_hint && hint)
        {
            REQUIRE(strcmp(m_expected_hint, hint) == 0,[&] () {
                printf(" input; %s#\n", sanitize(m_input));

                puts("\nexpected input hint;");
                printf("  \"%s\"\n", m_expected_hint);

                puts("\ngot;");
                printf("  \"%s\"\n", hint);
            });
        }
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
    m_expected_words.clear();
    m_expected_classifications.clear();
    m_expected_faces.clear();
    m_expected_hint = nullptr;

    m_has_matches = false;
    m_has_words = false;
    m_has_classifications = false;
    m_has_faces = false;
    m_has_hint = false;

    reset_lines();
}

//------------------------------------------------------------------------------
void line_editor_tester::expected_matches_impl(int32 dummy, ...)
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
void line_editor_tester::expected_words_impl(int32 dummy, ...)
{
    m_expected_words.clear();

    va_list arg;
    va_start(arg, dummy);

    while (const char* match = va_arg(arg, const char*))
        m_expected_words.push_back(match);

    va_end(arg);
    m_has_words = true;
}

//------------------------------------------------------------------------------
void line_editor_tester::set_expected_words_list(const char* const* expected)
{
    m_expected_words.clear();

    while (*expected)
        m_expected_words.push_back(*(expected++));

    m_has_words = true;
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
void line_editor_tester::set_expected_classifications(const char* classifications, bool mark_argmatchers)
{
    m_expected_classifications = classifications;
    m_mark_argmatchers = mark_argmatchers;
    m_has_classifications = true;
}

//------------------------------------------------------------------------------
void line_editor_tester::set_expected_faces(const char* faces)
{
    m_expected_faces = faces;
    m_has_faces = true;
}

//------------------------------------------------------------------------------
void line_editor_tester::set_expected_hint(const char* hint)
{
    m_expected_hint = hint;
    m_has_hint = true;
}
