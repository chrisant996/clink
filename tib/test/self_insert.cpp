// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "test.h"
#include "test_util.h"
#include "tib.h"

class self_insert_tester : public tib::editor_context
{
    typedef tib::editor_context base;

public:
    int32_t             dispatch(const tib::cstring& sequence, int32_t key, const tib::binding_target* binding, const tib::binding_params* params) noexcept override;
    uint32_t            get_dispatch_count() const noexcept { return m_dispatch_count; }

private:
    uint32_t            m_dispatch_count = 0;
};

int32_t self_insert_tester::dispatch(const tib::cstring& sequence, int32_t key, const tib::binding_target* binding, const tib::binding_params* params) noexcept
{
    ++m_dispatch_count;
    return base::dispatch(sequence, key, binding, params);
}

static void dispatch_macro(const char* text, std::shared_ptr<self_insert_tester>& input, tib::binding_resolver& resolver)
{
    tib::cstring macro(text);
    macro.append("\r");
    REQUIRE(tib::term_push_macro_text(macro.c_str(), macro.length()));

    for (;;)
    {
        const int32_t c = tib::term_in();
        REQUIRE(c >= 0);
        if (c == '\r')
            break;

        auto resolved = resolver.step(uint8_t(c));
        REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
        REQUIRE(resolved.dispatch());
    }
}

static std::shared_ptr<tib::key_table_list> make_quoted_insert_key_table(bool numeric_argument=false)
{
    auto tables = tib::make_default_key_table(numeric_argument);
    REQUIRE(!tables->empty());
    REQUIRE(tables->back()->add("\021", tib::binding_target_func("quoted-insert")));
    return tables;
}

static std::shared_ptr<tib::key_table_list> make_universal_argument_key_table(bool quoted_insert=false)
{
    auto tables = tib::make_default_key_table(true/*numeric_argument*/);
    REQUIRE(!tables->empty());
    REQUIRE(tables->back()->add("\025", tib::binding_target_func("universal-argument")));
    if (quoted_insert)
        REQUIRE(tables->back()->add("\021", tib::binding_target_func("quoted-insert")));
    return tables;
}

static void invoke_quoted_insert(tib::binding_resolver& resolver)
{
    auto resolved = resolver.step('\021');
    REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
    REQUIRE(resolved.binding_target->is_func_name("quoted-insert"));
    REQUIRE(resolved.dispatch());
}

TEST_CASE("Quoted insert")
{
    auto input = std::make_shared<self_insert_tester>();
    input->initialize();
    input->set_bindings(make_quoted_insert_key_table());

    tib::binding_resolver resolver;
    resolver.add_target(input);

    SECTION("Inserts any valid input byte literally")
    {
        const uint8_t bytes[] = { 0x01, '\r', 0x1b, 0x7f, 0x80, 0xf9 };
        for (const uint8_t c : bytes)
        {
            auto byte_input = std::make_shared<self_insert_tester>();
            byte_input->initialize();
            byte_input->set_bindings(make_quoted_insert_key_table());

            tib::binding_resolver byte_resolver;
            byte_resolver.add_target(byte_input);
            invoke_quoted_insert(byte_resolver);

            auto resolved = byte_resolver.step(c);
            REQUIRE(resolved.outcome == tib::dispatch_outcome::quoted_insert);
            REQUIRE(resolved.sequence.length() == 1);
            REQUIRE(uint8_t(resolved.sequence.c_str()[0]) == c);
            REQUIRE(resolved.dispatch());

            REQUIRE(byte_input->get_text().length() == 1);
            REQUIRE(uint8_t(byte_input->get_text().c_str()[0]) == c);
            REQUIRE(!strcmp(byte_input->get_last_command(), "self-insert"));
        }
    }

    SECTION("Applies to exactly one byte")
    {
        invoke_quoted_insert(resolver);

        auto resolved = resolver.step('\r');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::quoted_insert);
        REQUIRE(resolved.dispatch());
        REQUIRE(!input->done());
        REQUIRE(input->get_text() == "\r");

        resolved = resolver.step('\r');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target->is_func_name("accept-line"));
        REQUIRE(resolved.dispatch());
        REQUIRE(input->done());
    }

    SECTION("Does not invoke an abort binding")
    {
        auto tables = make_quoted_insert_key_table();
        REQUIRE(tables->back()->add("\007", tib::binding_target_func("abort")));
        input->set_bindings(std::move(tables));

        invoke_quoted_insert(resolver);
        auto resolved = resolver.step('\007');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::quoted_insert);
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_text() == tib::cstring("\007", 1));

        resolved = resolver.step('\007');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target->is_func_name("abort"));
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_text() == tib::cstring("\007", 1));
    }

    SECTION("Positive numeric argument repeats the next byte")
    {
        input->set_numeric_argument(5);
        invoke_quoted_insert(resolver);
        REQUIRE(!input->has_numeric_argument());

        auto resolved = resolver.step('\r');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::quoted_insert);
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_text() == "\r\r\r\r\r");

        resolved = resolver.step('\r');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target->is_func_name("accept-line"));
    }

    SECTION("Negative numeric argument quotes the next N bytes")
    {
        input->set_numeric_argument(-3);
        invoke_quoted_insert(resolver);
        REQUIRE(!input->has_numeric_argument());

        const uint8_t bytes[] = { '\r', 0x1b, 0x01 };
        for (const uint8_t c : bytes)
        {
            auto resolved = resolver.step(c);
            REQUIRE(resolved.outcome == tib::dispatch_outcome::quoted_insert);
            REQUIRE(resolved.dispatch());
        }
        REQUIRE(input->get_text() == "\r\x1b\x01");

        auto resolved = resolver.step('\r');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target->is_func_name("accept-line"));
    }

    SECTION("Proper negative digit argument quotes exactly N bytes")
    {
        tib::editor_quirks quirks;
        quirks.bash_digit_argument = false;
        input->set_quirks(quirks);
        input->set_bindings(make_quoted_insert_key_table(true/*numeric_argument*/));

        auto resolved = resolver.step('\x1b');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::more);
        resolved = resolver.step('-');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target->is_func_name("digit-argument"));
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_numeric_argument() == -1);

        resolved = resolver.step('\x1b');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::more);
        resolved = resolver.step('2');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target->is_func_name("digit-argument"));
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_numeric_argument() == -2);

        invoke_quoted_insert(resolver);
        for (int32_t n = 0; n < 2; ++n)
        {
            resolved = resolver.step('\x7f');
            REQUIRE(resolved.outcome == tib::dispatch_outcome::quoted_insert);
            REQUIRE(resolved.dispatch());
        }
        REQUIRE(input->get_text() == "\x7f\x7f");

        resolved = resolver.step('\x7f');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target->is_func_name("del-char-left"));
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_text() == "\x7f");
    }

    SECTION("Bash digit argument includes the implicit one")
    {
        tib::editor_quirks quirks;
        quirks.bash_digit_argument = true;
        input->set_quirks(quirks);
        input->set_bindings(make_quoted_insert_key_table(true/*numeric_argument*/));

        auto resolved = resolver.step('\x1b');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::more);
        resolved = resolver.step('-');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.dispatch());

        resolved = resolver.step('\x1b');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::more);
        resolved = resolver.step('2');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_numeric_argument() == -12);
    }

    SECTION("Discards a pending binding prefix")
    {
        auto resolved = resolver.step('\x1b');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::more);

        invoke_quoted_insert(resolver);
        resolved = resolver.step('x');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::quoted_insert);
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_text() == "x");
    }

    SECTION("Clearing targets cancels the pending mode")
    {
        invoke_quoted_insert(resolver);
        resolver.clear_targets();
        resolver.add_target(input);

        auto resolved = resolver.step('\r');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target->is_func_name("accept-line"));
    }

    SECTION("Returns quoted input to the initiating target")
    {
        auto other = std::make_shared<self_insert_tester>();
        other->initialize();

        auto other_table = std::make_shared<tib::key_table>(false);
        REQUIRE(other_table->add("x", tib::binding_target_func("accept-line")));
        auto other_tables = std::make_shared<tib::key_table_list>();
        other_tables->emplace_back(std::move(other_table));
        other->set_bindings(std::move(other_tables));
        resolver.add_target(other);

        invoke_quoted_insert(resolver);
        auto resolved = resolver.step('x');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::quoted_insert);
        REQUIRE(resolved.dispatcher_target.lock() == input);
        REQUIRE(input->get_text().empty());
        REQUIRE(other->get_text().empty());
        REQUIRE(!other->done());
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_text() == "x");
        REQUIRE(other->get_text().empty());
        REQUIRE(!other->done());
    }
}

TEST_CASE("Digit argument uses a modal key table")
{
    auto input = std::make_shared<self_insert_tester>();
    input->initialize();
    input->set_bindings(tib::make_default_key_table(true/*numeric_argument*/));

    tib::binding_resolver resolver;
    resolver.add_target(input);

    auto resolved = resolver.step('\x1b');
    REQUIRE(resolved.outcome == tib::dispatch_outcome::more);
    resolved = resolver.step('0');
    REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
    REQUIRE(resolved.binding_target->is_func_name("digit-argument"));
    REQUIRE(resolved.dispatch());
    REQUIRE(input->get_numeric_argument() == 0);

    resolved = resolver.step('1');
    REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
    REQUIRE(resolved.binding_target->is_func_name("digit-argument"));
    REQUIRE(resolved.dispatch());
    REQUIRE(input->get_numeric_argument() == 1);

    resolved = resolver.step('\x1b');
    REQUIRE(resolved.outcome == tib::dispatch_outcome::more);
    resolved = resolver.step('2');
    REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
    REQUIRE(resolved.binding_target->is_func_name("digit-argument"));
    REQUIRE(resolved.dispatch());
    REQUIRE(input->get_numeric_argument() == 12);

    resolved = resolver.step('x');
    REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
    REQUIRE(resolved.dispatch());
    REQUIRE(input->get_text() == "xxxxxxxxxxxx");
    REQUIRE(!input->has_numeric_argument());

    resolved = resolver.step('3');
    REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
    REQUIRE(resolved.dispatch());
    REQUIRE(input->get_text() == "xxxxxxxxxxxx3");
}

TEST_CASE("Universal argument uses a modal key table")
{
    auto input = std::make_shared<self_insert_tester>();
    input->initialize();
    input->set_bindings(make_universal_argument_key_table());

    tib::binding_resolver resolver;
    resolver.add_target(input);

    auto invoke_universal_argument = [&]()
    {
        auto resolved = resolver.step('\025');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target->is_func_name("universal-argument"));
        REQUIRE(resolved.dispatch());
    };

    auto invoke_digit_argument = [&](char digit)
    {
        auto resolved = resolver.step('\x1b');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::more);
        resolved = resolver.step(digit);
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target->is_func_name("digit-argument"));
        REQUIRE(resolved.dispatch());
    };

    SECTION("Supplies four and repeated invocations multiply by four")
    {
        invoke_universal_argument();
        REQUIRE(input->get_numeric_argument() == 4);
        invoke_universal_argument();
        REQUIRE(input->get_numeric_argument() == 16);

        auto resolved = resolver.step('x');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_text() == "xxxxxxxxxxxxxxxx");
        REQUIRE(!input->has_numeric_argument());
    }

    SECTION("Raw digits replace the implicit value")
    {
        invoke_universal_argument();
        for (const char c : "12")
        {
            if (!c)
                break;
            auto resolved = resolver.step(c);
            REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
            REQUIRE(resolved.binding_target->is_func_name("digit-argument"));
            REQUIRE(resolved.dispatch());
        }
        REQUIRE(input->get_numeric_argument() == 12);

        invoke_universal_argument();
        REQUIRE(input->get_numeric_argument() == 12);

        auto resolved = resolver.step('x');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_text() == "xxxxxxxxxxxx");
    }

    SECTION("First invocation preserves an existing digit argument")
    {
        input->set_bindings(make_universal_argument_key_table());
        invoke_digit_argument('5');
        REQUIRE(input->get_numeric_argument() == 5);

        invoke_universal_argument();
        REQUIRE(input->get_numeric_argument() == 5);
        invoke_universal_argument();
        REQUIRE(input->get_numeric_argument() == 20);
        invoke_universal_argument();
        REQUIRE(input->get_numeric_argument() == 80);
    }

    SECTION("First invocation after raw digits preserves their value")
    {
        invoke_universal_argument();

        auto resolved = resolver.step('5');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target->is_func_name("digit-argument"));
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_numeric_argument() == 5);

        invoke_universal_argument();
        REQUIRE(input->get_numeric_argument() == 5);
        invoke_universal_argument();
        REQUIRE(input->get_numeric_argument() == 20);
        invoke_universal_argument();
        REQUIRE(input->get_numeric_argument() == 80);
    }

    SECTION("Accepts a leading minus before raw digits")
    {
        input->set_bindings(make_universal_argument_key_table(true/*quoted_insert*/));
        invoke_universal_argument();

        for (const char c : "-3")
        {
            if (!c)
                break;
            auto resolved = resolver.step(c);
            REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
            REQUIRE(resolved.binding_target->is_func_name("digit-argument"));
            REQUIRE(resolved.dispatch());
        }
        REQUIRE(input->get_numeric_argument() == -3);

        invoke_quoted_insert(resolver);
        for (const char c : "abc")
        {
            if (!c)
                break;
            auto resolved = resolver.step(c);
            REQUIRE(resolved.outcome == tib::dispatch_outcome::quoted_insert);
            REQUIRE(resolved.dispatch());
        }
        REQUIRE(input->get_text() == "abc");
        REQUIRE(!input->has_numeric_argument());
    }
}

TEST_CASE("Uppercase Alt input resolves in the same key table")
{
    auto input = std::make_shared<self_insert_tester>();
    input->initialize();
    input->set_bindings(tib::make_default_key_table());

    tib::binding_resolver resolver;
    resolver.add_target(input);

    SECTION("Resolves defined lowercase alternatives directly")
    {
        struct test_case
        {
            char key;
            const char* command;
        };
        static const test_case cases[] =
        {
            { 'C', "capitalize" },
            { 'L', "lower-case" },
            { 'T', "transpose-words" },
            { 'U', "upper-case" },
        };

        for (const auto& test : cases)
        {
            REQUIRE(resolver.step('\x1b').more());
            const auto resolved = resolver.step(test.key);
            REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
            REQUIRE(resolved.key == test.key + ('a' - 'A'));
            REQUIRE(resolved.sequence.length() == 2);
            REQUIRE(resolved.sequence.c_str()[0] == '\x1b');
            REQUIRE(resolved.sequence.c_str()[1] == resolved.key);
            REQUIRE(resolved.binding_target->is_func_name(test.command));
        }
    }

    SECTION("Dispatches the lowercase alternative without replaying input")
    {
        input->initialize("mIXEd");
        input->set_caret(0);

        REQUIRE(resolver.step('\x1b').more());
        auto resolved = resolver.step('C');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.sequence == "\x1b" "c");
        REQUIRE(resolved.binding_target->is_func_name("capitalize"));
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_text() == "Mixed");
        REQUIRE(input->get_dispatch_count() == 1);
    }

    SECTION("Preserves a numeric argument until dispatch")
    {
        input->initialize("one two three");
        input->set_caret(4);
        input->set_numeric_argument(3);

        REQUIRE(resolver.step('\x1b').more());
        auto resolved = resolver.step('T');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target->is_func_name("transpose-words"));
        REQUIRE(input->has_numeric_argument());
        REQUIRE(resolved.dispatch());
        REQUIRE(!input->has_numeric_argument());
    }

    SECTION("Missing lowercase alternative falls back normally")
    {
        auto table = std::make_shared<tib::key_table>(true/*can_self_insert*/);
        REQUIRE(table->add("\x1b" "X", tib::binding_target_lowercase_version()));
        auto tables = std::make_shared<tib::key_table_list>();
        tables->emplace_back(std::move(table));
        input->set_bindings(std::move(tables));

        REQUIRE(resolver.step('\x1b').more());
        auto resolved = resolver.step('X');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
        REQUIRE(resolved.sequence == "X");
        REQUIRE(!resolved.binding_target);
        REQUIRE(resolved.dispatch());
        REQUIRE(input->get_dispatch_count() == 1);
        REQUIRE(input->get_text() == "X");
    }

    SECTION("Missing lowercase alternative continues searching other key tables")
    {
        auto base = std::make_shared<tib::key_table>();
        REQUIRE(base->add("\x1b" "X", tib::binding_target_func("accept-line")));
        auto overlay = std::make_shared<tib::key_table>();
        REQUIRE(overlay->add("\x1b" "X", tib::binding_target_lowercase_version()));
        auto tables = std::make_shared<tib::key_table_list>();
        tables->emplace_back(std::move(base));
        tables->emplace_back(std::move(overlay));
        input->set_bindings(std::move(tables));

        REQUIRE(resolver.step('\x1b').more());
        const auto resolved = resolver.step('X');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.sequence == "\x1b" "X");
        REQUIRE(resolved.key == 'X');
        REQUIRE(resolved.binding_target);
        REQUIRE(resolved.binding_target->is_func_name("accept-line"));
    }

    SECTION("Does not use a lowercase binding from another key table")
    {
        auto base = std::make_shared<tib::key_table>();
        REQUIRE(base->add("\x1b" "x", tib::binding_target_func("accept-line")));
        auto overlay = std::make_shared<tib::key_table>();
        REQUIRE(overlay->add("\x1b" "X", tib::binding_target_lowercase_version()));
        auto tables = std::make_shared<tib::key_table_list>();
        tables->emplace_back(std::move(base));
        tables->emplace_back(std::move(overlay));
        input->set_bindings(std::move(tables));

        REQUIRE(resolver.step('\x1b').more());
        const auto resolved = resolver.step('X');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::miss);
        REQUIRE(resolved.sequence == "X");
        REQUIRE(!resolved.binding_target);
        REQUIRE(!input->done());
    }
}

TEST_CASE("Quoted insert does not read ahead")
{
    REQUIRE(!tib::g_optimize_self_insert);
    const bool optimize_self_insert = tib::g_optimize_self_insert;
    MAKE_CLEANUP([optimize_self_insert]() { tib::g_optimize_self_insert = optimize_self_insert; });
    tib::g_optimize_self_insert = true;

    test_input_stream stream("ab");

    auto input = std::make_shared<self_insert_tester>();
    input->initialize();
    input->set_bindings(make_quoted_insert_key_table());

    tib::binding_resolver resolver;
    resolver.add_target(input);
    invoke_quoted_insert(resolver);

    auto resolved = resolver.step(uint8_t(tib::term_in()));
    REQUIRE(resolved.outcome == tib::dispatch_outcome::quoted_insert);
    REQUIRE(resolved.dispatch());
    REQUIRE(input->get_dispatch_count() == 2);
    REQUIRE(!stream.empty());
    REQUIRE(input->get_text() == "a");

    resolved = resolver.step(uint8_t(tib::term_in()));
    REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
    REQUIRE(resolved.dispatch());
    REQUIRE(input->get_dispatch_count() == 3);
    REQUIRE(stream.empty());
    REQUIRE(input->get_text() == "ab");

    input->undo();
    REQUIRE(input->get_text() == "a");
    input->undo();
    REQUIRE(input->get_text().empty());
}

TEST_CASE("Self insert optimization")
{
    REQUIRE(!tib::g_optimize_self_insert);
    const bool optimize_self_insert = tib::g_optimize_self_insert;
    MAKE_CLEANUP([optimize_self_insert]() { tib::g_optimize_self_insert = optimize_self_insert; });

    auto input = std::make_shared<self_insert_tester>();
    input->initialize();
    input->set_bindings(tib::make_default_key_table());

    tib::binding_resolver resolver;
    resolver.add_target(input);

    const char utf8[] = "\xf0\x9f\x9a\xa8\xf0\x9f\x98\x8eHello";

    SECTION("Disabled")
    {
        dispatch_macro(utf8, input, resolver);
        REQUIRE(input->get_dispatch_count() == sizeof(utf8) - 1);
        REQUIRE(input->get_text() == tib::cstring(utf8));
    }

    SECTION("Enabled")
    {
        tib::g_optimize_self_insert = true;
        dispatch_macro(utf8, input, resolver);
        REQUIRE(input->get_dispatch_count() == 1);
        REQUIRE(input->get_text() == tib::cstring(utf8));
    }
}

TEST_CASE("Optimized self insert undo grouping")
{
    REQUIRE(!tib::g_optimize_self_insert);
    const bool optimize_self_insert = tib::g_optimize_self_insert;
    MAKE_CLEANUP([optimize_self_insert]() { tib::g_optimize_self_insert = optimize_self_insert; });
    tib::g_optimize_self_insert = true;

    auto input = std::make_shared<self_insert_tester>();
    input->initialize();
    input->set_bindings(tib::make_default_key_table());

    tib::binding_resolver resolver;
    resolver.add_target(input);

    const char utf8[] = "\xf0\x9f\x9a\xa8\xf0\x9f\x98\x8eHello";
    dispatch_macro(utf8, input, resolver);
    REQUIRE(input->get_dispatch_count() == 1);
    REQUIRE(input->get_text() == tib::cstring(utf8));

    input->undo();
    REQUIRE(input->get_text().empty());

    input->redo();
    REQUIRE(input->get_text() == tib::cstring(utf8));
}
