// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "test.h"
#include "test_util.h"
#include "tib.h"
#include <type_traits>

class lookup_context : public tib::editor_context
{
public:
    int32_t dispatch(const tib::cstring& sequence, int32_t key, const tib::binding_target* binding, const tib::resolved_binding* resolved) noexcept override
    {
        ++dispatches;
        return editor_context::dispatch(sequence, key, binding, resolved);
    }
    void on_binding_fallback(const tib::binding_handle& handle) noexcept override
    {
        ++fallbacks;
        editor_context::on_binding_fallback(handle);
    }
    uint32_t dispatches = 0;
    uint32_t fallbacks = 0;
};

TEST_CASE("Binding lookup by key sequence")
{
    auto context = std::make_shared<lookup_context>();
    context->initialize("original");
    auto table = std::make_shared<tib::key_table>(false);
    REQUIRE(table->add("\007", tib::binding_target_func("abort")));
    REQUIRE(table->add("\030a", tib::binding_target_func("forward-char")));
    REQUIRE(table->add("a", tib::binding_target_func("begin-of-line")));
    REQUIRE(table->add("ab", tib::binding_target_func("end-of-line")));
    REQUIRE(table->add("\xc3\xa9", tib::binding_target_func("backward-char")));
    REQUIRE(table->add("\0\xff", 2, tib::binding_target_func("abort")));
    REQUIRE(table->add("m", tib::binding_target_macro("text")));
    REQUIRE(table->add("\033[%#A", tib::binding_target_func("forward-char"), true));
    context->add_bindings(table, tib::binding_policy::exclusive);

    auto resolved = context->lookup_keyseq("\007");
    REQUIRE(resolved.is_func_name("abort"));
    REQUIRE(resolved.sequence == "\007");
    REQUIRE(resolved.key == 7);
    REQUIRE(resolved.dispatcher_target.lock() == context);
    REQUIRE(!resolved.dispatch());
    REQUIRE(resolved.is_func_name("abort"));
    REQUIRE(context->lookup_keyseq("\030a").is_func_name("forward-char"));
    REQUIRE(context->lookup_keyseq("\xc3\xa9").is_func_name("backward-char"));
    REQUIRE(context->lookup_keyseq("\0\xff", 2).is_func_name("abort"));

    // Complete ambiguous bindings are committed; prefixes and surplus input
    // must not be mistaken for complete key sequences.
    resolved = context->lookup_keyseq("a");
    REQUIRE(resolved.is_func_name("begin-of-line"));
    REQUIRE(!resolved.dispatch());
    REQUIRE(context->lookup_keyseq("ab").is_func_name("end-of-line"));
    REQUIRE(context->lookup_keyseq("ac").outcome == tib::dispatch_outcome::miss);
    REQUIRE(context->lookup_keyseq("\030\007").outcome == tib::dispatch_outcome::miss);
    REQUIRE(context->lookup_keyseq("\007x").outcome == tib::dispatch_outcome::miss);
    REQUIRE(context->lookup_keyseq("\030").outcome == tib::dispatch_outcome::miss);
    REQUIRE(context->lookup_keyseq("\xc3").outcome == tib::dispatch_outcome::miss);
    REQUIRE(context->lookup_keyseq("z").outcome == tib::dispatch_outcome::miss);
    REQUIRE(context->lookup_keyseq("").outcome == tib::dispatch_outcome::miss);
    REQUIRE(context->lookup_keyseq(nullptr).outcome == tib::dispatch_outcome::miss);

    resolved = context->lookup_keyseq("\033[12A");
    REQUIRE(resolved.is_func_name("forward-char"));
    REQUIRE(resolved.params.size() == 1);
    REQUIRE(resolved.params[0] == "12");
    resolved = context->lookup_keyseq("m");
    REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
    REQUIRE(resolved.binding_target.get_type() == tib::binding_type::macro);
    REQUIRE(!resolved.dispatch());
    REQUIRE(!tib::term_has_macro_input());

    table->set_can_self_insert(true);
    resolved = context->lookup_keyseq("z");
    REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
    REQUIRE(!resolved.dispatch());
    REQUIRE(context->lookup_keyseq("\xce\xbb").outcome == tib::dispatch_outcome::self_insert);
    REQUIRE(context->get_text() == "original");
    REQUIRE(context->dispatches == 0);
    REQUIRE(context->fallbacks == 0);
}

TEST_CASE("Binding lookup by command")
{
    auto context = std::make_shared<lookup_context>();
    context->initialize("original");
    auto base = std::make_shared<tib::key_table>(false);
    REQUIRE(base->add("\007", tib::binding_target_func("abort")));
    REQUIRE(base->add("\030a", tib::binding_target_func("abort")));
    auto tables = std::make_shared<tib::key_table_list>();
    tables->push_back(base);
    context->add_bindings(tables);
    auto top = std::make_shared<tib::key_table>(false);

    SECTION("names are case insensitive and do not require registry lookup")
    {
        REQUIRE(top->add("x", tib::binding_target_func("host-command")));
        tables->push_back(top);
        auto resolved = context->lookup_command("HOST-COMMAND");
        REQUIRE(resolved.is_func_name("host-command"));
        REQUIRE(resolved.sequence == "x");
        REQUIRE(tib::editor_context::lookup_registered_command("host-command") == nullptr);
        REQUIRE(context->lookup_command(nullptr).outcome == tib::dispatch_outcome::miss);
        REQUIRE(context->lookup_command("missing").outcome == tib::dispatch_outcome::miss);
    }

    SECTION("later tables and groups supply the preferred binding")
    {
        REQUIRE(top->add("z", tib::binding_target_func("ABORT")));
        tables->push_back(top);
        REQUIRE(context->lookup_command("abort").sequence == "z");
        auto mode = std::make_shared<tib::key_table>(false);
        REQUIRE(mode->add("y", tib::binding_target_func("abort")));
        auto handle = context->add_bindings(mode);
        REQUIRE(context->lookup_command("abort").sequence == "y");
        handle.remove();
        REQUIRE(context->lookup_command("abort").sequence == "z");
    }

    SECTION("a macro or function override hides a lower binding")
    {
        REQUIRE(top->add("\007", tib::binding_target_macro("abort")));
        tables->push_back(top);
        REQUIRE(context->lookup_command("abort").sequence == "\030a");
        REQUIRE(top->add("\030a", tib::binding_target_func("forward-char")));
        REQUIRE(context->lookup_command("abort").outcome == tib::dispatch_outcome::miss);
    }

    SECTION("exclusive groups hide lower bindings even on a miss")
    {
        context->add_bindings(top, tib::binding_policy::exclusive);
        REQUIRE(context->lookup_command("abort").outcome == tib::dispatch_outcome::miss);
        REQUIRE(top->add("x", tib::binding_target_func("abort")));
        REQUIRE(context->lookup_command("abort").sequence == "x");
    }

    SECTION("fallback lookup neither dispatches nor crosses a mode boundary")
    {
        auto handle = context->add_bindings(top, tib::binding_policy::fallback);
        REQUIRE(context->numeric_digit('2'));
        const auto count = context->get_bindings().size();
        auto resolved = context->lookup_command("abort");
        REQUIRE(resolved.is_func_name("abort"));
        REQUIRE(context->lookup_keyseq(resolved.sequence.c_str(), resolved.sequence.length()).is_func_name("abort"));
        REQUIRE(context->is_numeric_argument_mode());
        REQUIRE(context->get_numeric_argument() == 2);
        REQUIRE(!resolved.dispatch());
        REQUIRE(context->is_numeric_argument_mode());
        REQUIRE(context->get_numeric_argument() == 2);
        REQUIRE(context->get_bindings().size() == count);
        REQUIRE(handle);
    }

    SECTION("numeric argument bindings can mask an abort binding")
    {
        context->clear_all_bindings();
        REQUIRE(top->add("2", tib::binding_target_func("abort")));
        context->add_bindings(top);
        REQUIRE(context->numeric_digit('3'));
        REQUIRE(context->lookup_command("abort").outcome == tib::dispatch_outcome::miss);
        REQUIRE(context->is_numeric_argument_mode());
        REQUIRE(context->get_numeric_argument() == 3);
    }

    SECTION("a shorter fallback binding hides a longer base sequence")
    {
        REQUIRE(top->add("\030", tib::binding_target_func("forward-char")));
        context->add_bindings(top, tib::binding_policy::fallback);
        REQUIRE(context->lookup_keyseq("\030a").outcome == tib::dispatch_outcome::miss);
        REQUIRE(context->lookup_command("abort").sequence == "\007");
    }

    SECTION("patterns and macros are not synthesized into input")
    {
        context->clear_all_bindings();
        REQUIRE(top->add("\033[%#A", tib::binding_target_func("abort"), true));
        REQUIRE(top->add("m", tib::binding_target_macro("\007")));
        context->add_bindings(top);
        REQUIRE(context->lookup_command("abort").outcome == tib::dispatch_outcome::miss);
    }

    SECTION("binary sequences retain their full length")
    {
        context->clear_all_bindings();
        REQUIRE(top->add("\0\xff", 2, tib::binding_target_func("abort")));
        context->add_bindings(top);
        auto resolved = context->lookup_command("abort");
        REQUIRE(resolved.is_func_name("abort"));
        REQUIRE(resolved.sequence.equals("\0\xff", 2));
    }

    SECTION("an ambiguous complete binding remains usable")
    {
        context->clear_all_bindings();
        REQUIRE(top->add("x", tib::binding_target_func("abort")));
        REQUIRE(top->add("xy", tib::binding_target_func("forward-char")));
        context->add_bindings(top);
        REQUIRE(context->lookup_command("abort").sequence == "x");
    }

    REQUIRE(context->get_text() == "original");
    REQUIRE(context->dispatches == 0);
    REQUIRE(context->fallbacks == 0);
}

static_assert(!std::is_copy_constructible<tib::binding_resolver>::value, "Resolver cloning must be explicit");
static_assert(!std::is_copy_assignable<tib::binding_resolver>::value, "Resolver cloning must be explicit");

TEST_CASE("Binding lookup uses normal input state during quoted insert")
{
    auto context = std::make_shared<lookup_context>();
    context->initialize("original");
    auto table = std::make_shared<tib::key_table>(false);
    REQUIRE(table->add("\026", tib::binding_target_func("quoted-insert")));
    REQUIRE(table->add("\007", tib::binding_target_func("abort")));
    context->add_bindings(table);
    tib::binding_resolver resolver;
    resolver.add_target(context);
    auto quote = resolver.step('\026');
    REQUIRE(quote.dispatch());
    REQUIRE(resolver.quoted_insert_pending());

    auto by_sequence = context->lookup_keyseq("\007");
    REQUIRE(by_sequence.is_func_name("abort"));
    REQUIRE(!by_sequence.dispatch());
    auto by_command = context->lookup_command("abort");
    REQUIRE(by_command.sequence == "\007");
    REQUIRE(!by_command.dispatch());
    REQUIRE(resolver.quoted_insert_pending());
    REQUIRE(context->get_text() == "original");
    REQUIRE(context->dispatches == 1);

    auto real = resolver.step('\007');
    REQUIRE(real.outcome == tib::dispatch_outcome::quoted_insert);
    REQUIRE(real.dispatch());
    REQUIRE(!resolver.quoted_insert_pending());
    REQUIRE(context->get_text() == "original\007");
}

TEST_CASE("Expired resolver results do not dispatch")
{
    auto context = std::make_shared<lookup_context>();
    context->initialize("original");
    auto table = std::make_shared<tib::key_table>(false);
    REQUIRE(table->add("x", tib::binding_target_func("begin-of-line")));
    REQUIRE(table->add("xy", tib::binding_target_func("end-of-line")));
    REQUIRE(table->add("m", tib::binding_target_macro("text")));
    REQUIRE(table->add("q", tib::binding_target_func("quoted-insert")));
    context->add_bindings(table);
    context->add_bindings(std::make_shared<tib::key_table>(), tib::binding_policy::fallback);

    tib::cstring output;
    test_output_stream capture(output);
    test_input_stream input("b");
    std::vector<tib::resolved_binding> results;
    std::unique_ptr<tib::binding_resolver> registrations_only;
    {
        tib::binding_resolver resolver;
        resolver.add_target(context);
        registrations_only = std::make_unique<tib::binding_resolver>(&resolver);
        auto prefix = resolver.step('x');
        REQUIRE(prefix.more());
        results.push_back(prefix);
        auto complete = resolver.resolve_pending();
        REQUIRE(complete.is_func_name("begin-of-line"));
        results.push_back(complete);
        auto miss = resolver.step('z');
        REQUIRE(miss.outcome == tib::dispatch_outcome::miss);
        results.push_back(miss);
        auto macro = resolver.step('m');
        REQUIRE(macro.binding_target.get_type() == tib::binding_type::macro);
        results.push_back(macro);
        REQUIRE(resolver.step('x').more());
        auto replay = resolver.step('z');
        REQUIRE(replay.is_func_name("begin-of-line"));
        results.push_back(replay);
        auto quoted_command = resolver.step('q');
        REQUIRE(quoted_command.is_func_name("quoted-insert"));
        results.push_back(quoted_command);
    }

    // The target, registrations, and copied results stay alive.  They must
    // not keep the producing resolver's state alive.
    for (auto& resolved : results)
        REQUIRE(!resolved.dispatch());
    REQUIRE(results[1].is_func_name("begin-of-line"));
    REQUIRE(context->dispatches == 0);
    REQUIRE(context->fallbacks == 0);
    REQUIRE(context->get_text() == "original");
    REQUIRE(capture.get_ding_count() == 0);
    REQUIRE(output.empty());
    REQUIRE(!tib::term_has_macro_input());
    REQUIRE(tib::term_in() == 'b');
}
