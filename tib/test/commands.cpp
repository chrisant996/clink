// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "maybe_windows.h"
#include "test.h"
#include "test_util.h"
#include "tib.h"
#include "tib_private.h"

static void initialize_context(tib::editor_context& context, const char* text,
                               tib::textpos_t caret)
{
    context.initialize(text);
    context.set_caret(caret);
}

static void invoke_command(tib::editor_context& context, const char* name)
{
    const auto command = tib::editor_context::lookup_registered_command(name);
    REQUIRE(command != nullptr);
    REQUIRE(command(context, 0, name, nullptr) == 0);
}

static void dispatch_macro_command(tib::editor_context& context, const char* sequence, const char* name)
{
    const tib::binding_target binding = tib::binding_target_func(name);
    REQUIRE(context.dispatch(sequence, 0, &binding, nullptr) == 0);
}

static int32_t accept_test_parameter(tib::editor_context& ctx, int32_t, const char*, const tib::resolved_binding*) noexcept
{
    tib::cstring parameter;
    if (!ctx.accept_read_parameter(parameter))
        return -1;
    ctx.insert_text(parameter.c_str());
    return 0;
}

TEST_CASE("Parameter request command lookup")
{
    static const tib::editor_command commands[] =
    {
        { "parameter-end", tib::end_of_line },
        { "PARAMETER-BEGIN", tib::begin_of_line },
    };
    tib::parameter_request request;
    REQUIRE(request.find_command("parameter-end") == nullptr);
    REQUIRE(request.find_command(nullptr) == nullptr);
    REQUIRE(request.find_command("forward-char") == nullptr);

    request.set_commands(commands, 1);
    REQUIRE(request.find_command("PaRaMeTeR-EnD") == tib::end_of_line);
    REQUIRE(request.find_command("parameter-begin") == nullptr);

    request.set_commands(commands);
    REQUIRE(request.find_command("parameter-end") == tib::end_of_line);
    REQUIRE(request.find_command("parameter-begin") == tib::begin_of_line);
    REQUIRE(request.find_command(nullptr) == nullptr);
    REQUIRE(request.find_command("missing-command") == nullptr);
    REQUIRE(request.find_command("parameter") == nullptr);
    REQUIRE(request.find_command("parameter-end-extra") == nullptr);
    REQUIRE(request.find_command("forward-char") == nullptr);

    request.set_commands(commands + 1, 1);
    REQUIRE(request.find_command("parameter-end") == nullptr);
    REQUIRE(request.find_command("parameter-begin") == tib::begin_of_line);

    request.set_commands(nullptr, 0);
    REQUIRE(request.find_command("parameter-begin") == nullptr);
    REQUIRE(request.find_command(nullptr) == nullptr);
}

TEST_CASE("Parameter request commands")
{
    auto context = std::make_shared<tib::editor_context>();
    context->initialize("original");
    context->add_bindings(tib::make_default_key_table(), tib::binding_policy::exclusive);
    tib::parameter_request request;
    request.initial_text = "abc";
    request.bindings = { std::make_shared<tib::key_table>() };

    SECTION("request commands shadow globals without changing other editors")
    {
        auto other_owner = std::make_shared<tib::editor_context>();
        auto& other = *other_owner;
        other.initialize();
        tib::parameter_request other_request;
        other_request.bindings = { std::make_shared<tib::key_table>() };
        static const tib::editor_command other_commands[] =
        {
            { "parameter-end", tib::begin_of_line },
        };
        other_request.set_commands(other_commands);
        REQUIRE(other.start_read_parameter(std::move(other_request)));

        static const tib::editor_command commands[] =
        {
            { "parameter-end", tib::end_of_line },
            { "FORWARD-CHAR", tib::begin_of_line },
        };
        request.set_commands(commands);
        request.bindings.front()->add("\t", tib::binding_target_func("PARAMETER-END"));
        request.bindings.front()->add("\006", tib::binding_target_func("forward-char"));
        REQUIRE(context->find_available_command("parameter-end") == nullptr);
        REQUIRE(context->start_read_parameter(std::move(request)));
        REQUIRE(context->find_available_command("PaRaMeTeR-EnD") == tib::end_of_line);
        REQUIRE(context->find_available_command("forward-char") == tib::begin_of_line);
        REQUIRE(context->find_available_command("backward-char") == tib::backward_char);
        REQUIRE(context->find_available_command(nullptr) == nullptr);
        REQUIRE(context->find_available_command("missing-command") == nullptr);
        REQUIRE(tib::editor_context::lookup_registered_command("parameter-end") == nullptr);
        REQUIRE(tib::editor_context::lookup_registered_command("forward-char") == tib::forward_char);
        REQUIRE(other.find_available_command("parameter-end") == tib::begin_of_line);
        REQUIRE(other.find_available_command("forward-char") == tib::forward_char);

        tib::binding_resolver resolver;
        resolver.add_target(context);
        auto resolved = resolver.step('\t');
        REQUIRE(resolved.is_func_name("parameter-end"));
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_caret() == 3);
        REQUIRE(context->get_last_command_func() == tib::end_of_line);
        resolved = resolver.step('\006');
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_caret() == 0);
        // This binding comes from the normal parameter editing table.
        REQUIRE(resolver.step('\033').more());
        REQUIRE(resolver.step('[').more());
        resolved = resolver.step('F');
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_caret() == 3);

        context->cancel_read_parameter();
        REQUIRE(context->find_available_command("parameter-end") == nullptr);
        REQUIRE(context->find_available_command("forward-char") == tib::forward_char);
        REQUIRE(context->get_text() == "original");
        REQUIRE(other.find_available_command("parameter-end") == tib::begin_of_line);
    }

    SECTION("caller-owned commands and dynamic names remain valid through acceptance")
    {
        const tib::cstring name("parameter-accept");
        const tib::editor_command commands[] =
        {
            { name.c_str(), accept_test_parameter },
        };
        request.set_commands(commands);
        request.bindings.front()->add("\r", tib::binding_target_func(name.c_str()));
        REQUIRE(context->start_read_parameter(std::move(request)));
        REQUIRE(context->find_available_command("parameter-accept") == accept_test_parameter);

        tib::binding_resolver resolver;
        resolver.add_target(context);
        auto resolved = resolver.step('\r');
        REQUIRE(resolved.is_func_name("parameter-accept"));
        REQUIRE(resolved.dispatch());
        REQUIRE(!context->is_read_parameter());
        REQUIRE(context->find_available_command("parameter-accept") == nullptr);
        REQUIRE(context->get_text() == "originalabc");
        REQUIRE(!context->is_done());
    }

    SECTION("one command array can be reused by requests that end independently")
    {
        static const tib::editor_command commands[] =
        {
            { "parameter-end", tib::end_of_line },
        };
        auto other_owner = std::make_shared<tib::editor_context>();
        auto& other = *other_owner;
        other.initialize();
        tib::parameter_request other_request;
        other_request.bindings = { std::make_shared<tib::key_table>() };
        other_request.set_commands(commands);
        request.set_commands(commands);
        REQUIRE(context->start_read_parameter(std::move(request)));
        REQUIRE(other.start_read_parameter(std::move(other_request)));
        context->reset_state();
        REQUIRE(!context->is_read_parameter());
        REQUIRE(context->find_available_command("parameter-end") == nullptr);
        REQUIRE(other.find_available_command("parameter-end") == tib::end_of_line);
        other.set_done();
        REQUIRE(!other.is_read_parameter());
        REQUIRE(other.find_available_command("parameter-end") == nullptr);
    }

    SECTION("additional tables share resolution and later tables override earlier tables")
    {
        auto first = request.bindings.front();
        REQUIRE(first->add("\t", tib::binding_target_func("begin-of-line")));
        REQUIRE(first->add("\006", tib::binding_target_func("end-of-line")));
        REQUIRE(first->add("\033x", tib::binding_target_func("begin-of-line")));
        auto second = std::make_shared<tib::key_table>();
        REQUIRE(second->add("\t", tib::binding_target_func("end-of-line")));
        REQUIRE(second->add("\033[F", tib::binding_target_func("begin-of-line")));
        REQUIRE(second->add("\033xy", tib::binding_target_func("end-of-line")));
        request.bindings.push_back(second);
        REQUIRE(context->start_read_parameter(std::move(request)));
        tib::binding_resolver resolver;
        resolver.add_target(context);

        auto resolved = resolver.step('\t');
        REQUIRE(resolved.is_func_name("end-of-line"));
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_caret() == 3);
        REQUIRE(resolver.step('\033').more());
        REQUIRE(resolver.step('[').more());
        resolved = resolver.step('F');
        REQUIRE(resolved.is_func_name("begin-of-line"));
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_caret() == 0);
        resolved = resolver.step('\006');
        REQUIRE(resolved.is_func_name("end-of-line"));
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_caret() == 3);
        REQUIRE(resolver.step('\033').more());
        REQUIRE(resolver.step('[').more());
        resolved = resolver.step('D');
        REQUIRE(resolved.is_func_name("backward-char"));
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_caret() == 2);
        REQUIRE(resolver.step('\033').more());
        REQUIRE(resolver.step('x').ambiguous());
        resolved = resolver.step('y');
        REQUIRE(resolved.is_func_name("end-of-line"));
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_caret() == 3);
    }

    SECTION("a supplied accept binding preserves the built-in parameter bindings")
    {
        static const tib::editor_command commands[] =
        {
            { "parameter-accept", accept_test_parameter },
        };
        request.set_commands(commands);
        REQUIRE(request.bindings.front()->add("\r", tib::binding_target_func("parameter-accept")));
        REQUIRE(context->start_read_parameter(std::move(request)));
        tib::binding_resolver resolver;
        resolver.add_target(context);
        REQUIRE(resolver.step('\033').more());
        REQUIRE(resolver.step('[').more());
        auto resolved = resolver.step('C');
        REQUIRE(resolved.is_func_name("forward-char"));
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_caret() == 1);
        resolved = resolver.step('\r');
        REQUIRE(resolved.is_func_name("parameter-accept"));
        REQUIRE(resolved.dispatch());
        REQUIRE(!context->is_read_parameter());
        REQUIRE(context->get_text() == "originalabc");
    }

    SECTION("requests without additional commands keep global lookup")
    {
        REQUIRE(context->start_read_parameter(std::move(request)));
        REQUIRE(context->find_available_command("forward-char") == tib::forward_char);
        context->cancel_read_parameter();
        request.bindings = { std::make_shared<tib::key_table>() };
        request.set_commands(nullptr, 0);
        REQUIRE(context->start_read_parameter(std::move(request)));
        REQUIRE(context->find_available_command("forward-char") == tib::forward_char);
    }
}

TEST_CASE("Parameter request end callback")
{
    int32_t calls = 0;
    bool expected_canceled = true;
    bool start_next = false;
    tib::cstring parameter;
    auto context = std::make_shared<tib::editor_context>();
    context->initialize("original");
    context->set_selection(2, 5);
    context->set_overwrite_mode(true);
    context->set_numeric_argument(7);
    const auto normal = context->add_bindings(tib::make_default_key_table(), tib::binding_policy::exclusive);
    tib::parameter_request request;
    request.initial_text = "abc";
    request.on_end = [&](bool canceled)
    {
        ++calls;
        REQUIRE(canceled == expected_canceled);
        REQUIRE(!context->is_read_parameter());
        REQUIRE(context->get_text() == "original");
        REQUIRE(context->get_anchor() == 2);
        REQUIRE(context->get_caret() == 5);
        REQUIRE(context->get_overwrite_mode());
        REQUIRE(context->get_numeric_argument() == 7);
        REQUIRE(context->get_bindings().size() == 1);
        REQUIRE(normal);
        if (!canceled)
            REQUIRE(parameter == "abc");
        context->cancel_read_parameter(); // The ended request cannot notify twice.
        if (start_next)
            REQUIRE(context->start_read_parameter(tib::parameter_request()));
    };
    REQUIRE(context->start_read_parameter(std::move(request)));
    REQUIRE(calls == 0);

    SECTION("acceptance")
    {
        expected_canceled = false;
        REQUIRE(context->accept_read_parameter(parameter));
        REQUIRE(parameter == "abc");
        REQUIRE(!context->accept_read_parameter(parameter));
    }

    SECTION("explicit cancellation")
    {
        context->cancel_read_parameter();
        context->cancel_read_parameter();
    }

    SECTION("reset")
    {
        context->reset_state();
    }

    SECTION("done")
    {
        context->set_done();
    }

    SECTION("initialize")
    {
        context->initialize("replacement");
    }

    SECTION("transfer text")
    {
        tib::cstring text;
        context->transfer_text(text);
        REQUIRE(text == "original");
    }

    SECTION("the callback can start the next request")
    {
        start_next = true;
        expected_canceled = false;
        REQUIRE(context->accept_read_parameter(parameter));
        REQUIRE(context->is_read_parameter());
        context->cancel_read_parameter();
    }

    REQUIRE(calls == 1);
}

TEST_CASE("Rejected parameter request does not call end callback")
{
    int32_t calls = 0;
    auto context = std::make_shared<tib::editor_context>();
    context->initialize();
    tib::parameter_request request;
    request.initial_text = "x";
    request.max_length = 0;
    request.on_end = [&](bool) { ++calls; };
    REQUIRE(!context->start_read_parameter(std::move(request)));
    context->cancel_read_parameter();
    tib::cstring parameter;
    REQUIRE(!context->accept_read_parameter(parameter));
    REQUIRE(calls == 0);
}

TEST_CASE("Parameter request backspace")
{
    auto context = std::make_shared<tib::editor_context>();
    context->initialize("original");
    context->set_selection(2, 5);
    context->set_overwrite_mode(true);
    context->set_numeric_argument(7);
    const auto normal = context->add_bindings(tib::make_default_key_table(), tib::binding_policy::exclusive);
    REQUIRE(!context->is_read_parameter());

    int32_t end_calls = 0;
    tib::parameter_request request;
    request.on_end = [&](bool canceled) { ++end_calls; REQUIRE(canceled); };
    request.bindings = { std::make_shared<tib::key_table>() };
    REQUIRE(context->start_read_parameter(std::move(request)));
    REQUIRE(context->is_read_parameter());
    const auto request_bindings = context->get_bindings().back().handle;
    tib::cstring output;
    test_output_stream capture(output);

    SECTION("empty input cancels before numeric repetition")
    {
        context->set_numeric_argument(4);
    }

    SECTION("deleting the last character needs another backspace to cancel")
    {
        context->insert_text("x");
        invoke_command(*context, "del-char-left");
        REQUIRE(context->is_read_parameter());
        REQUIRE(context->get_text() == "");
        REQUIRE(context->get_caret() == 0);
    }

    SECTION("deleting a selection needs another backspace to cancel")
    {
        context->insert_text("abc");
        context->set_selection(3, 0);
        invoke_command(*context, "del-char-left");
        REQUIRE(context->is_read_parameter());
        REQUIRE(context->get_text() == "");
        REQUIRE(context->get_caret() == 0);
    }

    SECTION("nonempty input at caret zero stays active")
    {
        context->insert_text("abc");
        context->set_caret(0);
        invoke_command(*context, "del-char-left");
        REQUIRE(context->is_read_parameter());
        REQUIRE(context->get_text() == "abc");
        REQUIRE(context->get_caret() == 0);
        REQUIRE(capture.get_ding_count() == 1);
        return;
    }

    SECTION("normal deletion in nonempty input stays active")
    {
        context->insert_text("abc");
        context->set_caret(2);
        invoke_command(*context, "del-char-left");
        REQUIRE(context->is_read_parameter());
        REQUIRE(context->get_text() == "ac");
        REQUIRE(context->get_caret() == 1);
        REQUIRE(capture.get_ding_count() == 0);
        return;
    }

    tib::binding_resolver resolver;
    resolver.add_target(context);
    auto resolved = resolver.step('\177');
    REQUIRE(resolved.is_func_name("del-char-left"));
    REQUIRE(resolved.dispatch());
    REQUIRE(!context->is_read_parameter());
    REQUIRE(end_calls == 1);
    REQUIRE(!request_bindings);
    REQUIRE(normal);
    REQUIRE(context->get_text() == "original");
    REQUIRE(context->get_anchor() == 2);
    REQUIRE(context->get_caret() == 5);
    REQUIRE(context->get_overwrite_mode());
    REQUIRE(!context->has_numeric_argument());
    REQUIRE(context->get_numeric_argument() == 1);
    REQUIRE(!context->is_done());
    REQUIRE(capture.get_ding_count() == 0);
}

TEST_CASE("Backspace on empty input without a parameter request")
{
    tib::editor_context context;
    context.initialize();
    tib::cstring output;
    test_output_stream capture(output);
    invoke_command(context, "del-char-left");
    REQUIRE(!context.is_read_parameter());
    REQUIRE(!context.is_done());
    REQUIRE(context.get_text() == "");
    REQUIRE(capture.get_ding_count() == 1);
}

TEST_CASE("Parameter request default key table")
{
    auto context = std::make_shared<tib::editor_context>();
    context->initialize("original");
    context->add_bindings(tib::make_default_key_table(), tib::binding_policy::exclusive);
    auto custom = std::make_shared<tib::key_table>(true/*can_self_insert*/);
    REQUIRE(custom->add("\006", tib::binding_target_func("host-forward")));
    REQUIRE(custom->add("\t", tib::binding_target_func("parameter-end")));
    REQUIRE(custom->add("\001", tib::binding_target_func("parameter-begin")));

    tib::parameter_request request;
    request.initial_text = "abc";
    auto first = std::make_shared<tib::key_table>();
    auto second = std::make_shared<tib::key_table>();
    REQUIRE(first->add("\r", tib::binding_target_func("parameter-accept")));
    REQUIRE(first->add("\001", tib::binding_target_func("parameter-end")));
    REQUIRE(first->add("\t", tib::binding_target_func("parameter-end")));
    REQUIRE(second->add("\t", tib::binding_target_func("parameter-begin")));
    request.bindings = { first, second };
    static const tib::editor_command commands[] =
    {
        { "host-forward", tib::forward_char },
        { "parameter-begin", tib::begin_of_line },
        { "parameter-end", tib::end_of_line },
        { "parameter-accept", accept_test_parameter },
    };
    request.set_commands(commands);

    bool use_custom = false;
    SECTION("the built-in table remains the default")
    {
    }
    SECTION("a context table replaces the built-in table")
    {
        use_custom = true;
        context->set_read_parameter_base_bindings(custom);
    }
    SECTION("a request can omit the built-in table")
    {
        request.no_base_bindings = true;
    }
    SECTION("a request can omit the custom context table")
    {
        use_custom = true;
        context->set_read_parameter_base_bindings(custom);
        request.no_base_bindings = true;
    }
    SECTION("nullptr restores the built-in table")
    {
        context->set_read_parameter_base_bindings(custom);
        context->set_read_parameter_base_bindings(nullptr);
    }

    // Hosts using their own tables need not register any tib command names.
    std::vector<std::pair<tib::cstring, tib::editor_command_func_t>> saved_commands;
    MAKE_CLEANUP([&saved_commands]()
    {
        if (!saved_commands.empty())
        {
            tib::editor_context::clear_all_commands();
            for (const auto& command : saved_commands)
                tib::editor_context::register_command(command.first.c_str(), command.second);
        }
    });
    const bool skip = request.no_base_bindings;
    if (use_custom || skip)
    {
        for (const auto& command : tib::editor_context::get_registered_commands())
            saved_commands.emplace_back(command.first, command.second);
        tib::editor_context::clear_all_commands();
    }
    REQUIRE(context->start_read_parameter(std::move(request)));
    const auto& group = context->get_bindings().back();
    REQUIRE(group.policy == tib::binding_policy::exclusive);
    const auto& tables = *group.tables;
    REQUIRE(tables.size() == (skip ? 2 : 3));
    REQUIRE(tables[tables.size() - 2] == first);
    REQUIRE(tables.back() == second);
    if (use_custom && !skip)
        REQUIRE(tables.front() == custom);

    tib::binding_resolver resolver;
    resolver.add_target(context);

    // The request overrides the context table; later request tables win.
    auto resolved = resolver.step('\001');
    REQUIRE(resolved.is_func_name("parameter-end"));
    REQUIRE(resolved.dispatch());
    REQUIRE(context->get_caret() == 3);
    resolved = resolver.step('\t');
    REQUIRE(resolved.is_func_name("parameter-begin"));
    REQUIRE(resolved.dispatch());
    REQUIRE(context->get_caret() == 0);

    resolved = resolver.step('\006');
    if (use_custom && !skip)
    {
        REQUIRE(resolved.is_func_name("host-forward"));
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_caret() == 1);
    }
    else
        REQUIRE(resolved.outcome == tib::dispatch_outcome::miss);

    // No built-in or normal editor bindings may leak into custom/skip mode.
    resolved = resolver.step('\007');
    if (use_custom || skip)
        REQUIRE(resolved.outcome == tib::dispatch_outcome::miss);
    else
        REQUIRE(resolved.is_func_name("abort"));

    resolved = resolver.step('\r');
    REQUIRE(resolved.is_func_name("parameter-accept"));
    REQUIRE(resolved.dispatch());
    REQUIRE(!context->is_read_parameter());
    REQUIRE(context->get_text() == "originalabc");

    // The normal editor bindings are restored after acceptance.
    resolved = resolver.step('\007');
    REQUIRE(resolved.is_func_name("abort"));
}

TEST_CASE("Ending a parameter request removes only its binding group")
{
    auto context = std::make_shared<tib::editor_context>();
    context->initialize("original");
    auto normal = tib::make_default_key_table();
    normal->add("!", tib::binding_target_func("accept-line"));
    const auto normal_handle = context->add_bindings(normal, tib::binding_policy::exclusive);
    tib::parameter_request request;
    request.initial_text = "abc";
    request.bindings = { std::make_shared<tib::key_table>(), std::make_shared<tib::key_table>() };
    const std::weak_ptr<tib::key_table> first_table = request.bindings.front();
    const std::weak_ptr<tib::key_table> second_table = request.bindings.back();
    REQUIRE(context->start_read_parameter(std::move(request)));
    const auto request_handle = context->get_bindings().back().handle;
    const std::weak_ptr<const tib::key_table_list> request_tables = context->get_bindings().back().tables;

    auto later = std::make_shared<tib::key_table>();
    later->add("\006", tib::binding_target_func("begin-of-line"));
    const auto later_handle = context->add_bindings(later);
    tib::binding_resolver resolver;
    resolver.add_target(context);
    REQUIRE(resolver.step('!').outcome == tib::dispatch_outcome::self_insert);

    const auto verify_request_ended = [&]()
    {
        REQUIRE(!context->is_read_parameter());
        REQUIRE(!request_handle);
        REQUIRE(request_tables.expired());
        REQUIRE(first_table.expired());
        REQUIRE(second_table.expired());
        REQUIRE(normal_handle);
        REQUIRE(later_handle);
        REQUIRE(context->get_bindings().size() == 2);
        REQUIRE(resolver.step('!').is_func_name("accept-line"));
        REQUIRE(resolver.step('\006').is_func_name("begin-of-line"));
    };

    SECTION("Cancel")
    {
        context->cancel_read_parameter();
        verify_request_ended();
    }

    SECTION("Accept")
    {
        tib::cstring parameter;
        REQUIRE(context->accept_read_parameter(parameter));
        REQUIRE(parameter == "abc");
        verify_request_ended();
    }

    SECTION("Reset")
    {
        context->reset_state();
        verify_request_ended();
    }

    SECTION("Done")
    {
        context->set_done();
        verify_request_ended();
    }

    SECTION("Initialize")
    {
        context->initialize("replacement");
        verify_request_ended();
    }
}

class completion_context : public tib::editor_context, public tib::editor_callbacks
{
public:
    completion_context() { set_callbacks(this); }

    static int32_t start_completion(tib::editor_context& ctx, int32_t, const char*, const tib::resolved_binding*)
    {
        auto& self = static_cast<completion_context&>(ctx);
        self.completion = self.add_bindings(self.table, self.policy);
        return 0;
    }

    static int32_t end_completion(tib::editor_context& ctx, int32_t, const char*, const tib::resolved_binding*)
    {
        auto& self = static_cast<completion_context&>(ctx);
        self.completion.remove();
        return 0;
    }

    void on_binding_fallback(tib::editor_context& ctx, const tib::binding_handle& handle_crossed) override
    {
        REQUIRE(handle_crossed == completion);
        fallbacks.push_back(handle_crossed);
        completion.remove();
    }

    void do_display(tib::editor_context&) override {}

    std::shared_ptr<tib::key_table> table = std::make_shared<tib::key_table>();
    tib::binding_policy policy = tib::binding_policy::fallback;
    tib::binding_handle completion;
    std::vector<tib::binding_handle> fallbacks;
};

TEST_CASE("Parameter request binding sub-modes")
{
    auto context = std::make_shared<completion_context>();
    context->initialize("original");
    auto normal = tib::make_default_key_table(true);
    normal->add("\t", tib::binding_target_func("test-start-completion"));
    const auto normal_handle = context->add_bindings(normal);
    auto outer = std::make_shared<tib::key_table>();
    outer->add("!", tib::binding_target_func("accept-line"));
    const auto outer_handle = context->add_bindings(outer);
    tib::editor_context::register_command("test-start-completion", completion_context::start_completion);
    tib::editor_context::register_command("test-end-completion", completion_context::end_completion);
    context->table->add("\006", tib::binding_target_func("begin-of-line"));
    context->table->add("\r", tib::binding_target_func("test-end-completion"));
    tib::parameter_request request;
    request.initial_text = "abc";
    request.bindings = { std::make_shared<tib::key_table>() };
    request.bindings.front()->add("\t", tib::binding_target_func("test-start-completion"));
    request.bindings.front()->add("\006", tib::binding_target_func("end-of-line"));
    request.bindings.front()->add("\005", tib::binding_target_func("end-of-line"));
    request.bindings.front()->add("\r", tib::binding_target_func("test-accept-parameter"));
    static const tib::editor_command commands[] =
    {
        { "test-accept-parameter", accept_test_parameter },
    };
    request.set_commands(commands);
    REQUIRE(context->start_read_parameter(std::move(request)));
    tib::binding_resolver resolver;
    resolver.add_target(context);

    SECTION("A global command enters completion and receives fallback by handle")
    {
        auto resolved = resolver.step('\t');
        REQUIRE(resolved.is_func_name("test-start-completion"));
        REQUIRE(resolved.dispatch());
        REQUIRE(context->completion);
        context->set_caret(3);
        resolved = resolver.step('\006');
        REQUIRE(resolved.is_func_name("begin-of-line"));
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_caret() == 0);
        REQUIRE(context->fallbacks.empty());
        // The request's binding must be used after the completion group misses.
        resolved = resolver.step('\005');
        REQUIRE(resolved.is_func_name("end-of-line"));
        REQUIRE(context->fallbacks.empty());
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_caret() == 3);
        REQUIRE(context->is_read_parameter());
        REQUIRE(!context->completion);
        REQUIRE(context->fallbacks.size() == 1);
        REQUIRE(!context->fallbacks[0]);
        // The normal host binding is still hidden by the parameter boundary.
        REQUIRE(resolver.step('!').outcome == tib::dispatch_outcome::self_insert);
    }

    SECTION("An exclusive completion mode can prohibit fallthrough")
    {
        context->policy = tib::binding_policy::exclusive;
        REQUIRE(resolver.step('\t').dispatch());
        REQUIRE(resolver.step('\005').outcome == tib::dispatch_outcome::miss);
        REQUIRE(context->fallbacks.empty());
        context->completion.remove();
        REQUIRE(resolver.step('\005').is_func_name("end-of-line"));
    }

    SECTION("Enter exits completion before it can accept the request")
    {
        REQUIRE(resolver.step('\t').dispatch());
        REQUIRE(resolver.step('\r').dispatch());
        REQUIRE(!context->completion);
        REQUIRE(context->is_read_parameter());
        REQUIRE(context->get_text() == "abc");
        REQUIRE(resolver.step('\r').dispatch());
        REQUIRE(!context->is_read_parameter());
        REQUIRE(context->get_text() == "originalabc");
        REQUIRE(normal_handle);
        REQUIRE(outer_handle);
        // The same global completion command also works in normal editing.
        REQUIRE(resolver.step('\t').dispatch());
        REQUIRE(context->completion);
    }
}

TEST_CASE("Numeric argument bindings restore outer registrations")
{
    auto context = std::make_shared<completion_context>();
    context->initialize();
    auto tables = std::make_shared<tib::key_table_list>();
    tables->push_back(tib::make_default_key_table(true));
    auto host = std::make_shared<tib::key_table>();
    host->add("x", tib::binding_target_func("begin-of-line"));
    tables->push_back(host);
    const auto host_handle = context->add_bindings(tables);
    tib::binding_resolver resolver;
    resolver.add_target(context);

    SECTION("Switching argument tables and ending argument mode preserves outer bindings")
    {
        REQUIRE(context->universal_argument());
        REQUIRE(context->numeric_digit('2'));
        REQUIRE(context->get_numeric_argument() == 2);
        context->clear_numeric_argument();
        REQUIRE(context->get_bindings().size() == 1);
        REQUIRE(host_handle);
        REQUIRE(resolver.step('x').is_func_name("begin-of-line"));
    }

    SECTION("Numeric fallback only retires its own group")
    {
        REQUIRE(context->universal_argument());
        auto resolved = resolver.step('x');
        REQUIRE(resolved.is_func_name("begin-of-line"));
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_bindings().size() == 1);
        REQUIRE(host_handle);
        REQUIRE(context->fallbacks.empty());
    }

    SECTION("A request hides and restores an existing host mode")
    {
        tib::parameter_request request;
        request.bindings = { std::make_shared<tib::key_table>() };
        REQUIRE(context->start_read_parameter(std::move(request)));
        REQUIRE(resolver.step('x').outcome == tib::dispatch_outcome::self_insert);
        context->cancel_read_parameter();
        REQUIRE(host_handle);
        REQUIRE(resolver.step('x').is_func_name("begin-of-line"));
    }
}

TEST_CASE("Keyboard macros")
{
    SECTION("records commands and omits recording controls")
    {
        tib::editor_context context;
        context.initialize();

        dispatch_macro_command(context, "\030(", "start-kbd-macro");
        REQUIRE(context.is_recording_kbd_macro());
        REQUIRE(!context.has_last_kbd_macro());

        REQUIRE(context.dispatch("a", 'a', nullptr, nullptr) == 0);
        dispatch_macro_command(context, "\030)", "end-kbd-macro");
        REQUIRE(!context.is_recording_kbd_macro());
        REQUIRE(context.has_last_kbd_macro());
        REQUIRE(context.get_last_kbd_macro() == "a");
        REQUIRE(context.get_text() == "a");
    }

    SECTION("replays the recorded input")
    {
        tib::editor_context context;
        context.initialize();
        MAKE_CLEANUP([]() { tib::term_clear_playback(); });

        REQUIRE(context.start_kbd_macro(false));
        REQUIRE(context.dispatch("x", 'x', nullptr, nullptr) == 0);
        REQUIRE(context.end_kbd_macro());
        REQUIRE(context.get_last_kbd_macro() == "x");

        context.set_numeric_argument(2);
        dispatch_macro_command(context, "\030e", "call-last-kbd-macro");
        REQUIRE(tib::term_has_macro_input());
        for (int32_t repeat = 0; repeat < 2; ++repeat)
        {
            const int32_t key = tib::term_in();
            REQUIRE(key == 'x');
            REQUIRE(context.dispatch("x", key, nullptr, nullptr) == 0);
        }
        REQUIRE(!tib::term_has_macro_input());
        REQUIRE(context.get_text() == "xxx");

        REQUIRE(context.dispatch("y", 'y', nullptr, nullptr) == 0);
        dispatch_macro_command(context, "\030(", "start-kbd-macro");
        REQUIRE(context.dispatch("z", 'z', nullptr, nullptr) == 0);
        dispatch_macro_command(context, "\030)", "end-kbd-macro");
        REQUIRE(context.get_last_kbd_macro() == "z");
        REQUIRE(context.get_text() == "xxxyz");
    }

    SECTION("records all batched self insert bytes")
    {
        const bool optimize_self_insert = tib::g_optimize_self_insert;
        MAKE_CLEANUP([optimize_self_insert]() { tib::g_optimize_self_insert = optimize_self_insert; });
        tib::g_optimize_self_insert = true;

        auto context = std::make_shared<tib::editor_context>();
        context->initialize();
        context->add_bindings(tib::make_default_key_table(), tib::binding_policy::exclusive);
        tib::binding_resolver resolver;
        resolver.add_target(context);
        REQUIRE(context->start_kbd_macro(false));
        test_input_stream stream("bc");
        auto resolved = resolver.step('a');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_text() == "abc");
        REQUIRE(context->end_kbd_macro());
        REQUIRE(context->get_last_kbd_macro() == "abc");

        test_input_stream unrecorded("ef");
        resolved = resolver.step('d');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
        REQUIRE(resolved.dispatch());
        REQUIRE(context->get_text() == "abcdef");
        REQUIRE(context->start_kbd_macro(false));
        REQUIRE(context->end_kbd_macro());
        REQUIRE(context->get_last_kbd_macro().empty());
    }

    SECTION("depth limit discards pending playback")
    {
        tib::editor_context context;
        context.initialize();
        MAKE_CLEANUP([]() { tib::term_clear_playback(); });
        REQUIRE(context.start_kbd_macro(false));
        REQUIRE(context.dispatch("x", 'x', nullptr, nullptr) == 0);
        REQUIRE(context.end_kbd_macro());

        context.set_numeric_argument(tib::c_max_playback_depth + 1);
        dispatch_macro_command(context, "\030e", "call-last-kbd-macro");
        REQUIRE(!tib::term_has_macro_input());
        REQUIRE(context.count_error_messages() == 1);
        REQUIRE(strstr(context.get_error_message(0), "Maximum macro execution depth exceeded") != nullptr);
    }
}

static int32_t dummy_command(tib::editor_context&, int32_t, const char*, const tib::resolved_binding*)
{
    PREVENT_COMDAT_FOLDING();
    return 0;
}

TEST_CASE("Prefix meta command")
{
    tib::editor_context context;
    context.initialize("abc");
    context.set_last_command("dummy-command", dummy_command);
    context.set_numeric_argument(3);
    context.set_mark_active();
    MAKE_CLEANUP([]() { tib::term_clear_playback(); });

    const tib::binding_target binding = tib::binding_target_func("prefix-meta");
    REQUIRE(context.dispatch("\030p", 'p', &binding, nullptr) == 0);
    REQUIRE(context.get_last_command_func() == dummy_command);
    REQUIRE(context.get_numeric_argument() == 3);
    REQUIRE(context.has_numeric_argument());
    REQUIRE(context.is_mark_active());
    REQUIRE(tib::term_in() == 0x1b);
}

TEST_CASE("Abort command")
{
    SECTION("resets editor state")
    {
        tib::editor_context context;
        tib::border_definition border;
        border.left = "|";
        initialize_context(context, "abc", 2);
        context.set_border(&border);
        context.set_selection(0, 2);
        context.set_overwrite_mode(true);
        context.set_named_value("operation", "pending", false);
        context.set_numeric_argument(-12);
        context.set_last_command("dummy-command", dummy_command);

        invoke_command(context, "abort");

        REQUIRE(context.get_text() == "abc");
        REQUIRE(context.get_selection_state().get_anchor() == 0);
        REQUIRE(context.get_selection_state().get_caret() == 2);
        REQUIRE(context.get_overwrite_mode());
        REQUIRE(context.get_border() == &border);
        REQUIRE(context.get_named_value("operation") == nullptr);
        REQUIRE(!context.has_numeric_argument());
        REQUIRE(!strcmp(context.get_last_command_name(), ""));
        REQUIRE(context.get_last_command_func() == nullptr);
    }

    SECTION("interrupts pending overwrite input")
    {
        tib::editor_context context;
        initialize_context(context, "ab", 0);
        context.set_overwrite_mode(true);
        context.insert_char(char(0xc2), true);

        invoke_command(context, "abort");
        context.insert_char(char(0xa2), true);
        REQUIRE(context.get_text() == "\xc2\xa2" "b");

        REQUIRE(context.undo());
        REQUIRE(context.get_text() == "\xc2" "b");
    }
}

TEST_CASE("Commands honor numeric arguments")
{
    SECTION("Negative forward-char moves backward the requested count")
    {
        tib::editor_context context;
        initialize_context(context, "abcdef", 5);
        context.set_numeric_argument(-3);
        invoke_command(context, "forward-char");
        REQUIRE(context.get_caret() == 2);
    }

    SECTION("Negative forward-word moves backward the requested count")
    {
        tib::editor_context context;
        initialize_context(context, "one two three four", 18);
        context.set_numeric_argument(-2);
        invoke_command(context, "forward-word");
        REQUIRE(context.get_caret() == 8);
    }

    SECTION("Positive repeated deletion is one undo operation")
    {
        tib::editor_context context;
        initialize_context(context, "abcdef", 1);
        context.set_numeric_argument(3);
        invoke_command(context, "del-char-right");
        REQUIRE(context.get_text() == "aef");
        REQUIRE(context.get_caret() == 1);
        REQUIRE(context.undo());
        REQUIRE(context.get_text() == "abcdef");
        REQUIRE(context.get_caret() == 1);
    }

    SECTION("Negative repeated deletion inverts direction and is one undo operation")
    {
        tib::editor_context context;
        initialize_context(context, "abcdef", 5);
        context.set_numeric_argument(-3);
        invoke_command(context, "del-char-right");
        REQUIRE(context.get_text() == "abf");
        REQUIRE(context.get_caret() == 2);
        REQUIRE(context.undo());
        REQUIRE(context.get_text() == "abcdef");
        REQUIRE(context.get_caret() == 5);
    }
}

TEST_CASE("Select word command")
{
    SECTION("Selects the word containing the caret")
    {
        tib::editor_context context;
        initialize_context(context, "alpha beta", 2);
        const auto command = tib::editor_context::lookup_registered_command("select-word");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "select-word", nullptr) == 0);
        const auto& selection = context.get_selection_state();
        REQUIRE(selection.get_sel_begin() == 0);
        REQUIRE(selection.get_sel_end() == 5);
    }

    SECTION("Selects a Unicode word without splitting graphemes")
    {
        static const char text[] = "x \xc3\xa9" "e\xcc\x81" "y z";
        tib::editor_context context;
        initialize_context(context, text, 5);
        const auto command = tib::editor_context::lookup_registered_command("select-word");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "select-word", nullptr) == 0);
        const auto& selection = context.get_selection_state();
        REQUIRE(selection.get_sel_begin() == 2);
        REQUIRE(selection.get_sel_end() == 8);
    }

    SECTION("Selects the preceding word at end of input")
    {
        tib::editor_context context;
        initialize_context(context, "alpha beta", 10);
        const auto command = tib::editor_context::lookup_registered_command("select-word");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "select-word", nullptr) == 0);
        const auto& selection = context.get_selection_state();
        REQUIRE(selection.get_sel_begin() == 6);
        REQUIRE(selection.get_sel_end() == 10);
    }
}

TEST_CASE("Bigword commands")
{
    SECTION("Forward and backward bigword use whitespace boundaries")
    {
        tib::editor_context context;
        initialize_context(context, "one.two\tthree", 0);
        invoke_command(context, "forward-bigword");
        REQUIRE(context.get_caret() == 7);
        invoke_command(context, "forward-bigword");
        REQUIRE(context.get_caret() == 13);
        invoke_command(context, "backward-bigword");
        REQUIRE(context.get_caret() == 8);
        invoke_command(context, "backward-bigword");
        REQUIRE(context.get_caret() == 0);
    }

    SECTION("Delete bigword left includes punctuation")
    {
        tib::editor_context context;
        initialize_context(context, "one.two three", 7);
        invoke_command(context, "del-bigword-left");
        REQUIRE(context.get_text() == " three");
        REQUIRE(context.get_caret() == 0);
    }

    SECTION("Delete bigword right includes punctuation")
    {
        tib::editor_context context;
        initialize_context(context, "one.two three", 0);
        invoke_command(context, "del-bigword-right");
        REQUIRE(context.get_text() == " three");
        REQUIRE(context.get_caret() == 0);
    }

    SECTION("Select bigword includes punctuation")
    {
        tib::editor_context context;
        initialize_context(context, "one.two three", 4);
        invoke_command(context, "select-bigword");
        const auto& selection = context.get_selection_state();
        REQUIRE(selection.get_sel_begin() == 0);
        REQUIRE(selection.get_sel_end() == 7);
    }

    SECTION("Transpose bigwords treats punctuation as part of each bigword")
    {
        tib::editor_context context;
        initialize_context(context, "one.two three", 8);
        invoke_command(context, "transpose-bigwords");
        REQUIRE(context.get_text() == "three one.two");
        REQUIRE(context.get_caret() == 13);
    }
}

TEST_CASE("Case transform commands")
{
    SECTION("Upper case transforms the selection")
    {
        tib::editor_context context;
        initialize_context(context, "one mIxEd three", 9);
        context.set_selection(9, 4);
        invoke_command(context, "upper-case");
        REQUIRE(context.get_text() == "one MIXED three");
        REQUIRE(context.get_selection_state().get_anchor() == 9);
        REQUIRE(context.get_selection_state().get_caret() == 4);
        REQUIRE(context.get_selection_state().has_selection());
    }

    SECTION("Lower case transforms the selection")
    {
        tib::editor_context context;
        initialize_context(context, "one MIXED three", 4);
        context.set_selection(4, 9);
        invoke_command(context, "lower-case");
        REQUIRE(context.get_text() == "one mixed three");
        REQUIRE(context.get_selection_state().get_anchor() == 4);
        REQUIRE(context.get_selection_state().get_caret() == 9);
        REQUIRE(context.get_selection_state().has_selection());
    }

    SECTION("Capitalize transforms the selection to title case")
    {
        tib::editor_context context;
        initialize_context(context, "one mIxEd caSE three", 4);
        context.set_selection(4, 14);
        invoke_command(context, "capitalize");
        REQUIRE(context.get_text() == "one Mixed Case three");
        REQUIRE(context.get_selection_state().get_anchor() == 4);
        REQUIRE(context.get_selection_state().get_caret() == 14);
        REQUIRE(context.get_selection_state().has_selection());
    }

    SECTION("No selection transforms from the care to the end of the word")
    {
        tib::editor_context context;
        initialize_context(context, "one mIxxEd three", 7);
        invoke_command(context, "upper-case");
        REQUIRE(context.get_text() == "one mIxXED three");
        REQUIRE(context.get_selection_state().get_anchor() == 10);
        REQUIRE(context.get_selection_state().get_caret() == 10);
    }

    SECTION("No selection transforms the next word from whitespace")
    {
        tib::editor_context context;
        initialize_context(context, "one   MIXED three", 4);
        invoke_command(context, "lower-case");
        REQUIRE(context.get_text() == "one   mixed three");
        REQUIRE(context.get_selection_state().get_anchor() == 11);
        REQUIRE(context.get_selection_state().get_caret() == 11);
    }

    SECTION("No selection is no-op at end of input")
    {
        tib::editor_context context;
        initialize_context(context, "one mIXED", 9);
        invoke_command(context, "capitalize");
        REQUIRE(context.get_text() == "one mIXED");
        REQUIRE(context.get_selection_state().get_anchor() == 9);
        REQUIRE(context.get_selection_state().get_caret() == 9);
    }

    SECTION("Toggle case cycles through upper title and lower case")
    {
        tib::editor_context context;
        initialize_context(context, "mIxEd caSE", 0);

        context.set_selection(0, 10);
        invoke_command(context, "toggle-case");
        REQUIRE(context.get_text() == "MIXED CASE");
        REQUIRE(context.get_selection_state().get_anchor() == 0);
        REQUIRE(context.get_selection_state().get_caret() == 10);

        context.set_selection(10, 0);
        invoke_command(context, "toggle-case");
        REQUIRE(context.get_text() == "Mixed Case");
        REQUIRE(context.get_selection_state().get_anchor() == 10);
        REQUIRE(context.get_selection_state().get_caret() == 0);

        context.set_selection(0, 10);
        invoke_command(context, "toggle-case");
        REQUIRE(context.get_text() == "mixed case");
        REQUIRE(context.get_selection_state().get_anchor() == 0);
        REQUIRE(context.get_selection_state().get_caret() == 10);

        context.set_selection(10, 0);
        invoke_command(context, "toggle-case");
        REQUIRE(context.get_text() == "MIXED CASE");
        REQUIRE(context.get_selection_state().get_anchor() == 10);
        REQUIRE(context.get_selection_state().get_caret() == 0);
    }

    SECTION("A case transform is one undo operation")
    {
        tib::editor_context context;
        initialize_context(context, "one mIXED three", 6);
        invoke_command(context, "capitalize");
        REQUIRE(context.get_text() == "one mIXed three");
        context.undo();
        REQUIRE(context.get_text() == "one mIXED three");
        REQUIRE(context.get_selection_state().get_anchor() == 6);
        REQUIRE(context.get_selection_state().get_caret() == 6);
    }
}

TEST_CASE("Screen line cursor column continuation")
{
    static const char input_bytes[] = "\x1b[B\x1b[1;2B"; // Down, Shift-Down.
    test_input_stream stream(input_bytes);

    auto context = std::make_shared<tib::editor_context>();
    context->set_max_width(10);
    context->set_max_height(3);
    context->set_variable_height(true);
    initialize_context(*context, "abcdefgh\nxy\nabcdefgh", 7);
    context->add_bindings(tib::make_default_key_table(), tib::binding_policy::exclusive);

    tib::binding_resolver resolver;
    resolver.add_target(context);

    while (tib::term_in_avail())
    {
        context->display();
        const int32_t c = tib::term_in();
        REQUIRE(c >= 0);
        auto resolved = resolver.step(uint8_t(c));
        if (!resolved.more())
            REQUIRE(resolved.dispatch());
    }

    REQUIRE(stream.empty());
    REQUIRE(context->get_last_command_func() == tib::cua_screen_line_down);
    REQUIRE(context->get_selection_state().get_anchor() == 11);
    REQUIRE(context->get_selection_state().get_caret() == 19);
}

TEST_CASE("Transpose chars command")
{
    SECTION("Swaps the grapheme at the caret with the preceding grapheme")
    {
        tib::editor_context context;
        initialize_context(context, "abcd", 2);
        const auto command = tib::editor_context::lookup_registered_command("transpose-chars");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-chars", nullptr) == 0);
        REQUIRE(context.get_text() == "acbd");
        REQUIRE(context.get_caret() == 3);
    }

    SECTION("Swaps two graphemes before the caret at end of input")
    {
        tib::editor_context context;
        initialize_context(context, "abcd", 4);
        const auto command = tib::editor_context::lookup_registered_command("transpose-chars");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-chars", nullptr) == 0);
        REQUIRE(context.get_text() == "abdc");
        REQUIRE(context.get_caret() == 4);
    }

    SECTION("Treats a combining sequence as one grapheme")
    {
        static const char text[] = "a" "e\xcc\x81" "b";
        tib::editor_context context;
        initialize_context(context, text, 4);
        const auto command = tib::editor_context::lookup_registered_command("transpose-chars");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-chars", nullptr) == 0);
        REQUIRE(context.get_text() == "ab" "e\xcc\x81");
        REQUIRE(context.get_caret() == 5);
    }

    SECTION("Does nothing without two graphemes")
    {
        tib::editor_context context;
        initialize_context(context, "a", 1);
        const auto command = tib::editor_context::lookup_registered_command("transpose-chars");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-chars", nullptr) == 0);
        REQUIRE(context.get_text() == "a");
        REQUIRE(context.get_caret() == 1);
    }
}

TEST_CASE("Transpose words command")
{
    SECTION("Swaps the previous word with a word beginning at the caret")
    {
        tib::editor_context context;
        initialize_context(context, "one two three", 4);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two one three");
        REQUIRE(context.get_caret() == 7);
    }

    SECTION("Swaps the words on either side of the caret")
    {
        tib::editor_context context;
        initialize_context(context, "one,  two three", 4);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two,  one three");
        REQUIRE(context.get_caret() == 9);
    }

    SECTION("Swaps the surrounding words at the end of a word")
    {
        tib::editor_context context;
        initialize_context(context, "one two three", 3);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two one three");
        REQUIRE(context.get_caret() == 7);
    }

    SECTION("Swaps the preceding word with the word containing the caret")
    {
        tib::editor_context context;
        initialize_context(context, "one,  two three", 7);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two,  one three");
        REQUIRE(context.get_caret() == 9);
    }

    SECTION("Does nothing at the beginning of the first word")
    {
        tib::editor_context context;
        initialize_context(context, "one,  two three", 0);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "one,  two three");
        REQUIRE(context.get_caret() == 0);
    }

    SECTION("Swaps the two preceding words at end of input")
    {
        tib::editor_context context;
        initialize_context(context, "one two three", 13);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "one three two");
        REQUIRE(context.get_caret() == 13);
    }

    SECTION("Uses the last word and its predecessor when no word follows")
    {
        tib::editor_context context;
        initialize_context(context, "one two three", 10);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "one three two");
        REQUIRE(context.get_caret() == 13);
    }

    SECTION("Swaps the previous word with the last word beginning at the caret")
    {
        tib::editor_context context;
        initialize_context(context, "one two three", 8);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "one three two");
        REQUIRE(context.get_caret() == 13);
    }

    SECTION("Does nothing without a word before leading separators")
    {
        tib::editor_context context;
        initialize_context(context, "  one two", 0);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "  one two");
        REQUIRE(context.get_caret() == 0);
    }

    SECTION("Swap the preceding words after trailing separators")
    {
        tib::editor_context context;
        initialize_context(context, "one$two,,", 9);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two$one,,");
        REQUIRE(context.get_caret() == 7);
    }

    SECTION("Swaps the preceding words from within trailing separators")
    {
        tib::editor_context context;
        initialize_context(context, "one two  ", 8);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two one  ");
        REQUIRE(context.get_caret() == 7);
    }

    SECTION("Treats Unicode graphemes as word characters")
    {
        static const char text[] = "a" "e\xcc\x81" " \xc3\xa9" "b";
        tib::editor_context context;
        initialize_context(context, text, 6);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "\xc3\xa9" "b a" "e\xcc\x81");
        REQUIRE(context.get_caret() == 8);
    }

    SECTION("Does nothing without two words")
    {
        tib::editor_context context;
        initialize_context(context, "one", 3);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "one");
        REQUIRE(context.get_caret() == 3);
    }

    SECTION("Does nothing without any words")
    {
        tib::editor_context context;
        initialize_context(context, "  , ", 2);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "  , ");
        REQUIRE(context.get_caret() == 2);
    }

    SECTION("Uses the caret end of a selection and clears the selection")
    {
        tib::editor_context context;
        initialize_context(context, "one two three", 5);
        context.set_selection(0, 5);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two one three");
        REQUIRE(context.get_caret() == 7);
        REQUIRE(!context.get_selection_state().has_selection());
    }

    SECTION("Undo restores a transpose as one operation")
    {
        tib::editor_context context;
        initialize_context(context, "one two", 4);
        const auto command = tib::editor_context::lookup_registered_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        context.undo();
        REQUIRE(context.get_text() == "one two");
        REQUIRE(context.get_caret() == 4);
    }
}

TEST_CASE("Named value pinning")
{
    tib::editor_context context;
    context.initialize("abc");

    SECTION("pinned values expire and unpinned values survive edits")
    {
        context.set_named_value("text", "pending", true);
        context.set_named_value_int("number", 42, true);
        context.set_named_value("persistent", "saved", false);
        context.set_named_value_int("persistent-number", 7, false);
        REQUIRE(!strcmp(context.get_named_value("text"), "pending"));
        REQUIRE(context.get_named_value_int("number") == 42);
        context.insert_char('d');
        REQUIRE(context.get_named_value("text") == nullptr);
        REQUIRE(context.get_named_value_int("number", -1) == -1);
        REQUIRE(!strcmp(context.get_named_value("persistent"), "saved"));
        REQUIRE(context.get_named_value_int("persistent-number") == 7);
    }

    SECTION("setters replace stale values and replace their pins")
    {
        context.set_named_value("text", "old", true);
        context.set_named_value_int("number", 1, true);
        context.inc_change_counter();
        context.set_named_value("text", "new", true);
        context.set_named_value_int("number", 2, true);
        REQUIRE(!strcmp(context.get_named_value("text"), "new"));
        REQUIRE(context.get_named_value_int("number") == 2);
        context.inc_change_counter();
        REQUIRE(context.get_named_value("text") == nullptr);
        REQUIRE(context.get_named_value_int("number", -1) == -1);
        context.set_named_value("text", "unpinned", false);
        context.set_named_value_int("number", 3, false);
        context.inc_change_counter();
        REQUIRE(!strcmp(context.get_named_value("text"), "unpinned"));
        REQUIRE(context.get_named_value_int("number") == 3);
        context.set_named_value("text", "pinned", true);
        context.set_named_value_int("number", 4, true);
        context.inc_change_counter();
        REQUIRE(context.get_named_value("text") == nullptr);
        REQUIRE(context.get_named_value_int("number", -1) == -1);
        context.clear_named_value("text");
        context.clear_named_value("number");
        REQUIRE(context.get_named_value("text") == nullptr);
        REQUIRE(context.get_named_value_int("number", -1) == -1);
    }
}

static int32_t dispatch_mouse(tib::editor_context& context, uint32_t button,
                              char key, uint32_t x, uint32_t y=1)
{
    tib::resolved_binding resolved;
    resolved.params.resize(3);
    resolved.params[0].printf("%u", button);
    resolved.params[1].printf("%u", x);
    resolved.params[2].printf("%u", y);
    tib::cstring sequence;
    sequence.printf("\x1b[<%u;%u;%u%c", button, x, y, key);
    const auto binding = tib::binding_target_func("mouse-input");
    return context.dispatch(sequence, key, &binding, &resolved);
}

TEST_CASE("Mouse state pinning")
{
    tib::editor_context context;
    context.initialize("one two three");
    context.set_max_width(20);
    context.set_origin(1, 1);
    context.display();

    SECTION("drag requires an accepted press and stops at release")
    {
        REQUIRE(dispatch_mouse(context, 32, 'M', 6) == 0);
        REQUIRE(!context.get_selection_state().has_selection());
        REQUIRE(dispatch_mouse(context, 0, 'M', 2) == 0);
        REQUIRE(dispatch_mouse(context, 32, 'M', 6) == 0);
        REQUIRE(context.get_selection_state().get_anchor() == 1);
        REQUIRE(context.get_caret() == 6);
        REQUIRE(dispatch_mouse(context, 0, 'm', 6) == -1);
        REQUIRE(dispatch_mouse(context, 32, 'M', 8) == 0);
        REQUIRE(context.get_caret() == 6);
        REQUIRE(dispatch_mouse(context, 0, 'M', 9) == 0);
        REQUIRE(dispatch_mouse(context, 32, 'M', 11) == 0);
        REQUIRE(context.get_selection_state().get_anchor() == 8);
        REQUIRE(context.get_caret() == 11);
        REQUIRE(dispatch_mouse(context, 0, 'M', 30) == -1);
        REQUIRE(dispatch_mouse(context, 32, 'M', 12) == 0);
        REQUIRE(context.get_caret() == 11);
    }

    SECTION("buffer edits invalidate a held press")
    {
        REQUIRE(dispatch_mouse(context, 0, 'M', 2) == 0);
        context.insert_char('x');
        context.display();
        const auto caret = context.get_caret();
        REQUIRE(dispatch_mouse(context, 32, 'M', 6) == 0);
        REQUIRE(context.get_caret() == caret);
        REQUIRE(!context.get_selection_state().has_selection());
        REQUIRE(dispatch_mouse(context, 0, 'M', 4) == 0);
        REQUIRE(dispatch_mouse(context, 32, 'M', 6) == 0);
        REQUIRE(context.get_selection_state().get_anchor() == 3);
        REQUIRE(context.get_caret() == 6);
    }

    SECTION("double clicks and word drags continue only on the same baseline")
    {
        REQUIRE(dispatch_mouse(context, 0, 'M', 2) == 0);
        REQUIRE(dispatch_mouse(context, 0, 'm', 2) == -1);
        REQUIRE(dispatch_mouse(context, 0, 'M', 2) == 0);
        REQUIRE(context.get_selection_state().get_sel_begin() == 0);
        REQUIRE(context.get_selection_state().get_sel_end() == 3);
        REQUIRE(dispatch_mouse(context, 32, 'M', 6) == 0);
        REQUIRE(context.get_selection_state().get_sel_begin() == 0);
        REQUIRE(context.get_selection_state().get_sel_end() == 7);
        context.inc_change_counter();
        REQUIRE(dispatch_mouse(context, 0, 'M', 2) == 0);
        REQUIRE(!context.get_selection_state().has_selection());
        REQUIRE(context.get_caret() == 1);
    }
}

TEST_CASE("Cursor column pinning")
{
    tib::editor_context context;
    context.set_max_width(10);
    context.set_max_height(4);
    context.set_variable_height(true);
    initialize_context(context, "abcdefgh\nxy\nabcdefgh\nabcdefgh", 7);
    context.set_origin(1, 1);
    context.display();

    SECTION("keyboard continuation captures a new column after the baseline changes")
    {
        dispatch_macro_command(context, "\x1b[B", "screen-line-down");
        REQUIRE(context.get_caret() == 11);
        context.display();
        const auto column = context.get_relative_cursor().x;
        context.inc_change_counter();
        dispatch_macro_command(context, "\x1b[B", "screen-line-down");
        REQUIRE(context.get_caret() == 12 + column);
    }

    SECTION("wheel continuation captures a new column after the baseline changes")
    {
        REQUIRE(dispatch_mouse(context, 65, 'M', 1) == 0);
        context.set_caret(13);
        context.display();
        const auto column = context.get_relative_cursor().x;
        context.inc_change_counter();
        REQUIRE(dispatch_mouse(context, 65, 'M', 1) == 0);
        REQUIRE(context.get_named_value_int("cursor_column_continuation", -1) == column);
    }
}
