// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "test.h"
#include "test_util.h"
#include "tib.h"
#include "tib_private.h"
#include <type_traits>

#ifdef _WIN32
TEST_CASE("Repeated key input")
{
    SECTION("Queues every repeat (ASCII)")
    {
        tib::pushed_input input;
        KEY_EVENT_RECORD record {};
        record.bKeyDown = true;
        record.wRepeatCount = 3;
        record.uChar.UnicodeChar = L'x';

        REQUIRE(input.push_key_event(record) == 1);
        REQUIRE(input.read() == 'x');
        REQUIRE(input.read() == 'x');
        REQUIRE(input.read() == 'x');
        REQUIRE(input.empty());
    }

    SECTION("Queues every repeat (UTF16)")
    {
        tib::pushed_input input;
        KEY_EVENT_RECORD record {};
        record.bKeyDown = true;
        record.wRepeatCount = 3;
        record.uChar.UnicodeChar = 0x03c7; // Greek Small Letter Chi

        REQUIRE(input.push_key_event(record) == 1);
        REQUIRE(input.read() == 0xcf);
        REQUIRE(input.read() == 0x87);
        REQUIRE(input.read() == 0xcf);
        REQUIRE(input.read() == 0x87);
        REQUIRE(input.read() == 0xcf);
        REQUIRE(input.read() == 0x87);
        REQUIRE(input.empty());
    }

    SECTION("Expands a wrapped queue")
    {
        tib::pushed_input input;
        KEY_EVENT_RECORD record {};
        record.bKeyDown = true;

        record.wRepeatCount = 100;
        record.uChar.UnicodeChar = L'a';
        REQUIRE(input.push_key_event(record) == 1);
        for (size_t i = 0; i < 90; ++i)
            REQUIRE(input.read() == 'a');

        record.wRepeatCount = 120;
        record.uChar.UnicodeChar = L'b';
        REQUIRE(input.push_key_event(record) == 1);

        record.wRepeatCount = 30;
        record.uChar.UnicodeChar = L'c';
        REQUIRE(input.push_key_event(record) == 1);

        for (size_t i = 0; i < 10; ++i)
            REQUIRE(input.read() == 'a');
        for (size_t i = 0; i < 120; ++i)
            REQUIRE(input.read() == 'b');
        for (size_t i = 0; i < 30; ++i)
            REQUIRE(input.read() == 'c');
        REQUIRE(input.empty());
    }

    SECTION("Prepends input")
    {
        tib::pushed_input input;
        REQUIRE(input.push('c'));
        REQUIRE(input.push('d'));
        REQUIRE(input.push_front("ab", 2));
        REQUIRE(input.read() == 'a');
        REQUIRE(input.read() == 'b');
        REQUIRE(input.read() == 'c');
        REQUIRE(input.read() == 'd');
        REQUIRE(input.empty());
    }
}
#endif

TEST_CASE("Key tables expose shared ownership to hosts")
{
    auto table = std::make_shared<tib::key_table>();
    auto host_reference = table.get()->shared_from_this();
    REQUIRE(host_reference == table);
    REQUIRE(!host_reference.owner_before(table));
    REQUIRE(!table.owner_before(host_reference));

    std::weak_ptr<tib::key_table> lifetime = table;
    table.reset();
    REQUIRE(!lifetime.expired());
    host_reference.reset();
    REQUIRE(lifetime.expired());
}

TEST_CASE("Adding a key binding moves its sequence storage")
{
    tib::key_table table;
    tib::key_binding binding { "sequence", tib::binding_target_func("command") };
    const char* const sequence_storage = binding.sequence.c_str();

    REQUIRE(table.add(std::move(binding)));
    REQUIRE(table.begin()->sequence.c_str() == sequence_storage);
}

namespace tib {

class binding_fallback_tester : public dispatcher_tester
{
public:
    uint32_t            get_fallback_count() const noexcept { return uint32_t(fallbacks.size()); }
    void                on_binding_fallback(const binding_handle& handle_crossed) noexcept override { fallbacks.push_back(handle_crossed); }
    std::vector<binding_handle> fallbacks;
};

} // namespace tib

using tib::binding_fallback_tester;

TEST_CASE("Partial match suppresses later binding fallback")
{
    auto prefix_table = std::make_shared<tib::key_table>();
    REQUIRE(prefix_table->add("ab", tib::binding_target_func("prefix-command")));
    auto prefix_target = std::make_shared<dispatcher_tester>();
    prefix_target->add_bindings(std::move(prefix_table), tib::binding_policy::exclusive);

    auto miss_table = std::make_shared<tib::key_table>();
    REQUIRE(miss_table->add("z", tib::binding_target_func("miss-command")));

    auto miss_target = std::make_shared<binding_fallback_tester>();
    miss_target->add_bindings(std::move(miss_table), tib::binding_policy::exclusive);
    miss_target->add_bindings(std::make_shared<tib::key_table>(), tib::binding_policy::fallback);

    tib::binding_resolver resolver;
    resolver.add_target(prefix_target);
    resolver.add_target(miss_target);

    const auto resolved = resolver.step('a');
    REQUIRE(miss_target->get_fallback_count() == 0);
    REQUIRE(resolved.outcome == tib::dispatch_outcome::more);
    auto complete = resolver.step('b');
    REQUIRE(complete.is_func_name("prefix-command"));
    REQUIRE(complete.dispatch());
    REQUIRE(miss_target->get_fallback_count() == 0);
}

TEST_CASE("Binding fallback commits only on dispatch")
{
    auto base = std::make_shared<tib::key_table>();
    REQUIRE(base->add("ab", tib::binding_target_func("base-command")));

    auto argument = std::make_shared<tib::key_table>();
    REQUIRE(argument->add("1", tib::binding_target_func("argument-digit")));

    auto target = std::make_shared<binding_fallback_tester>();
    target->add_bindings(std::move(base), tib::binding_policy::exclusive);
    auto argument_handle = target->add_bindings(argument, tib::binding_policy::fallback);

    tib::binding_resolver resolver;
    resolver.add_target(target);
    REQUIRE(resolver.is_keyseq_recognized("ab", 2));
    REQUIRE(resolver.is_keyseq_recognized("z", 1)); // Dispatch must commit the fallback.
    REQUIRE(target->get_fallback_count() == 0);

    REQUIRE(resolver.step('a').more());
    REQUIRE(target->get_fallback_count() == 0);
    auto resolved = resolver.step('b');
    REQUIRE(resolved.is_func_name("base-command"));
    REQUIRE(target->get_fallback_count() == 0);
    REQUIRE(resolved.dispatch());
    REQUIRE(target->get_fallback_count() == 1);
    argument_handle.remove();
    REQUIRE(!resolver.is_keyseq_recognized("z", 1));
}

TEST_CASE("Binding fallback is reported on a final miss")
{
    auto base = std::make_shared<tib::key_table>();
    REQUIRE(base->add("a", tib::binding_target_func("base-command")));

    auto argument = std::make_shared<tib::key_table>();
    REQUIRE(argument->add("1", tib::binding_target_func("argument-digit")));

    auto target = std::make_shared<binding_fallback_tester>();
    target->add_bindings(std::move(base), tib::binding_policy::exclusive);
    target->add_bindings(argument, tib::binding_policy::fallback);

    tib::binding_resolver resolver;
    resolver.add_target(target);
    auto resolved = resolver.step('z');
    REQUIRE(resolved.outcome == tib::dispatch_outcome::miss);
    REQUIRE(target->get_fallback_count() == 0);
    REQUIRE(!resolved.dispatch());
    REQUIRE(target->get_fallback_count() == 1);
}

TEST_CASE("Binding stack registrations")
{
    auto target = std::make_shared<dispatcher_tester>();
    auto table = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*table, "x", "shared"));
    auto first = target->add_bindings(table);
    auto second = target->add_bindings(table);
    auto top = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*top, "x", "top"));
    auto third = target->add_bindings(top);
    REQUIRE(first != second);
    REQUIRE(second != third);
    REQUIRE(first);

    tib::binding_resolver resolver;
    resolver.add_target(target);

    SECTION("Explicit removal expires every owner and preserves priority")
    {
        auto second_copy = second;
        second = {};
        REQUIRE(second_copy);
        REQUIRE(target->get_bindings().size() == 3);
        auto second_observer = second_copy;
        second_copy.remove();
        REQUIRE(!second_copy);
        REQUIRE(!second_observer);
        second_observer.remove();
        second_copy.remove(); // Already removed: no effect.
        REQUIRE(target->get_bindings().size() == 2);
        REQUIRE(target->get_bindings().front().tables->front() == table);
        REQUIRE(target->get_bindings().back().tables->front() == top);
        REQUIRE(resolver.step('x').is_func_name("top"));
        third.remove();
        REQUIRE(!third);
        REQUIRE(resolver.step('x').is_func_name("shared"));
        std::weak_ptr<tib::key_table> lifetime = table;
        table.reset();
        REQUIRE(!lifetime.expired());
        const auto first_copy = first;
        const std::weak_ptr<const tib::key_table_list> first_tables = target->get_bindings().front().tables;
        first.remove();
        REQUIRE(!first);
        REQUIRE(!first_copy);
        REQUIRE(first_tables.expired());
        REQUIRE(lifetime.expired());
        REQUIRE(target->get_bindings().empty());
        REQUIRE(resolver.step('x').outcome == tib::dispatch_outcome::miss);
    }

    SECTION("Default and moved-from handles are expired")
    {
        tib::binding_handle empty;
        REQUIRE(!empty);
        empty.remove();
        auto moved = std::move(first);
        REQUIRE(!first);
        REQUIRE(moved);
        first.remove();
        REQUIRE(target->get_bindings().size() == 3);
        moved.remove();
        REQUIRE(!moved);
        REQUIRE(target->get_bindings().size() == 2);
    }

    SECTION("Outstanding handles expire without keeping the target or tables alive")
    {
        std::weak_ptr<dispatcher_tester> target_lifetime = target;
        std::weak_ptr<tib::key_table> table_lifetime = table;
        const std::weak_ptr<const tib::key_table_list> first_tables = target->get_bindings().front().tables;
        table.reset();
        target.reset();
        REQUIRE(target_lifetime.expired());
        REQUIRE(first_tables.expired());
        REQUIRE(table_lifetime.expired());
        REQUIRE(!first);
        REQUIRE(!second);
        REQUIRE(!third);
        // Removing or destroying expired handles cannot access former entries.
        first.remove();
        first = {};
        second = {};
        third = {};
    }

    SECTION("Discarded handles leave their registrations active")
    {
        auto discarded = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*discarded, "y", "discarded"));
        target->add_bindings(std::move(discarded));
        first = {};
        second = {};
        third = {};
        REQUIRE(target->get_bindings().size() == 4);
        REQUIRE(resolver.step('x').is_func_name("top"));
        REQUIRE(resolver.step('y').is_func_name("discarded"));
    }
}

TEST_CASE("Binding table list ownership and single-table convenience")
{
    auto target = std::make_shared<dispatcher_tester>();
    auto base = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*base, "x", "base"));
    auto tables = std::make_shared<tib::key_table_list>();
    tables->push_back(base);
    const auto handle = target->add_bindings(tables);
    REQUIRE(target->get_bindings().back().tables == tables);
    static_assert(std::is_same_v<decltype(*target->get_bindings().back().tables->front()), const tib::key_table&>);
    REQUIRE(tables->shared_from_this() == tables);
    tib::binding_resolver resolver;
    resolver.add_target(target);

    SECTION("A single table is wrapped in a separate list for each registration")
    {
        auto single = target->add_bindings(base);
        REQUIRE(target->get_bindings().back().tables != tables);
        REQUIRE(target->get_bindings().back().tables->size() == 1);
        REQUIRE(target->get_bindings().back().tables->front() == base);
        const auto other = target->add_bindings(base);
        REQUIRE(target->get_bindings()[1].tables != target->get_bindings()[2].tables);
        single.remove();
        REQUIRE(handle);
        REQUIRE(other);
        REQUIRE(resolver.step('x').is_func_name("base"));
    }

    SECTION("Const tables can be registered directly or added to a list")
    {
        std::shared_ptr<const tib::key_table> read_only = base;
        auto single = target->add_bindings(read_only);
        tables->push_back(read_only);
        REQUIRE(target->get_bindings().back().tables->front() == read_only);
        REQUIRE(resolver.step('x').is_func_name("base"));
        single.remove();
        REQUIRE(resolver.step('x').is_func_name("base"));
    }

    SECTION("Owners can edit tables while dispatchers retain const pointers")
    {
        REQUIRE(base->remove("x"));
        REQUIRE(add_binding(*base, "y", "updated"));
        REQUIRE(resolver.step('x').outcome == tib::dispatch_outcome::miss);
        REQUIRE(resolver.step('y').is_func_name("updated"));
    }

    SECTION("Suppliers can add remove and replace tables in a registered list")
    {
        auto overlay = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*overlay, "x", "overlay"));
        REQUIRE(add_binding(*overlay, "y", "overlay"));
        tables->push_back(overlay);
        REQUIRE(resolver.step('x').is_func_name("overlay"));
        REQUIRE(resolver.step('y').is_func_name("overlay"));
        tables->pop_back();
        REQUIRE(resolver.step('x').is_func_name("base"));
        REQUIRE(resolver.step('y').outcome == tib::dispatch_outcome::miss);
        tables->front() = overlay;
        REQUIRE(resolver.step('x').is_func_name("overlay"));
        tables->clear();
        REQUIRE(handle);
        REQUIRE(resolver.step('x').outcome == tib::dispatch_outcome::miss);
        REQUIRE(target->get_bindings().size() == 1);
    }

    SECTION("A shared list stays shared across independently removable registrations")
    {
        auto other = std::make_shared<dispatcher_tester>();
        auto other_handle = other->add_bindings(tables);
        tib::binding_resolver other_resolver;
        other_resolver.add_target(other);
        auto replacement = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*replacement, "x", "replacement"));
        tables->front() = replacement;
        REQUIRE(resolver.step('x').is_func_name("replacement"));
        REQUIRE(other_resolver.step('x').is_func_name("replacement"));
        other_handle.remove();
        REQUIRE(handle);
        REQUIRE(resolver.step('x').is_func_name("replacement"));
        REQUIRE(other_resolver.step('x').outcome == tib::dispatch_outcome::miss);
    }
}

TEST_CASE("Binding table lists define policy boundaries")
{
    auto target = std::make_shared<binding_fallback_tester>();
    auto base = std::make_shared<tib::key_table>(true);
    REQUIRE(add_binding(*base, "abc", "base"));
    target->add_bindings(base);
    auto mode = std::make_shared<tib::key_table>(false);
    REQUIRE(add_binding(*mode, "a", "mode"));
    auto overlay = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*overlay, "x", "overlay"));
    auto tables = std::make_shared<tib::key_table_list>();
    tables->push_back(mode);
    tables->push_back(overlay);
    tib::binding_resolver resolver;
    resolver.add_target(target);

    SECTION("Fallback applies to the whole list and survives removal of its first table")
    {
        const auto handle = target->add_bindings(tables, tib::binding_policy::fallback);
        REQUIRE(resolver.step('a').is_func_name("mode"));
        REQUIRE(resolver.step('x').is_func_name("overlay"));
        REQUIRE(target->fallbacks.empty());
        tables->erase(tables->begin());
        REQUIRE(resolver.step('a').ambiguous());
        REQUIRE(resolver.step('b').ambiguous());
        auto resolved = resolver.step('c');
        REQUIRE(resolved.is_func_name("base"));
        REQUIRE(resolved.dispatch());
        REQUIRE(target->fallbacks.size() == 1);
        REQUIRE(target->fallbacks[0] == handle);
        REQUIRE(target->get_bindings().back().tables == tables);
    }

    SECTION("An empty exclusive list still excludes lower groups")
    {
        const auto handle = target->add_bindings(tables, tib::binding_policy::exclusive);
        tables->clear();
        REQUIRE(handle);
        REQUIRE(resolver.step('a').outcome == tib::dispatch_outcome::miss);
        REQUIRE(target->fallbacks.empty());
    }

    SECTION("An empty fallback list still notifies for its registration")
    {
        const auto handle = target->add_bindings(tables, tib::binding_policy::fallback);
        tables->clear();
        auto resolved = resolver.step('x');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
        REQUIRE(resolved.dispatch());
        REQUIRE(target->fallbacks.size() == 1);
        REQUIRE(target->fallbacks[0] == handle);
    }
}

TEST_CASE("Binding stack search policies")
{
    auto target = std::make_shared<binding_fallback_tester>();
    auto base = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*base, "abc", "long"));
    REQUIRE(add_binding(*base, "x", "base"));
    target->add_bindings(base);
    auto mode = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*mode, "a", "short"));
    tib::binding_resolver resolver;
    resolver.add_target(target);

    SECTION("Overlays share longest-match resolution")
    {
        target->add_bindings(mode);
        REQUIRE(resolver.step('a').ambiguous());
        REQUIRE(resolver.step('b').more());
        REQUIRE(resolver.step('c').is_func_name("long"));
        REQUIRE(resolver.step('x').is_func_name("base"));
        REQUIRE(target->fallbacks.empty());
    }

    SECTION("Fallback hides lower prefixes until the group misses")
    {
        const auto boundary = target->add_bindings(mode, tib::binding_policy::fallback);
        REQUIRE(resolver.step('a').is_func_name("short"));
        auto resolved = resolver.step('x');
        REQUIRE(resolved.is_func_name("base"));
        REQUIRE(target->fallbacks.empty());
        REQUIRE(resolved.dispatch());
        REQUIRE(target->fallbacks.size() == 1);
        REQUIRE(target->fallbacks[0] == boundary);
        REQUIRE(boundary);
    }

    SECTION("Exclusive groups exclude lower tables but not other targets")
    {
        target->add_bindings(mode, tib::binding_policy::exclusive);
        REQUIRE(resolver.step('a').is_func_name("short"));
        REQUIRE(resolver.step('x').outcome == tib::dispatch_outcome::miss);
        auto other = std::make_shared<dispatcher_tester>();
        other->add_bindings(base);
        resolver.add_target(other);
        REQUIRE(resolver.step('x').is_func_name("base"));
        REQUIRE(target->fallbacks.empty());
    }
}

TEST_CASE("Nested binding fallback boundaries")
{
    auto target = std::make_shared<binding_fallback_tester>();
    auto base = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*base, "abc", "base"));
    target->add_bindings(base, tib::binding_policy::exclusive);
    auto middle = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*middle, "abq", "middle"));
    const auto middle_handle = target->add_bindings(middle, tib::binding_policy::fallback);
    auto top = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*top, "ax", "top"));
    auto overlay = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*overlay, "t", "overlay"));
    auto top_tables = std::make_shared<tib::key_table_list>();
    top_tables->push_back(top);
    top_tables->push_back(overlay);
    auto top_handle = target->add_bindings(top_tables, tib::binding_policy::fallback);
    tib::binding_resolver resolver;
    resolver.add_target(target);

    SECTION("Each miss retries the whole sequence and notifies in search order")
    {
        REQUIRE(resolver.is_keyseq_recognized("abc", 3));
        REQUIRE(target->fallbacks.empty());
        REQUIRE(resolver.step('a').more());
        REQUIRE(resolver.step('b').more());
        REQUIRE(target->fallbacks.empty());
        auto resolved = resolver.step('c');
        REQUIRE(resolved.is_func_name("base"));
        REQUIRE(target->fallbacks.empty());
        REQUIRE(resolved.dispatch());
        REQUIRE(target->fallbacks.size() == 2);
        REQUIRE(target->fallbacks[0] == top_handle);
        REQUIRE(target->fallbacks[1] == middle_handle);
        // Notifications do not implicitly end either mode.
        REQUIRE(resolver.step('t').is_func_name("overlay"));
    }

    SECTION("A final miss still commits every crossed boundary")
    {
        auto resolved = resolver.step('z');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::miss);
        REQUIRE(!resolved.dispatch());
        REQUIRE(target->fallbacks.size() == 2);
        REQUIRE(target->fallbacks[0] == top_handle);
        REQUIRE(target->fallbacks[1] == middle_handle);
    }

    SECTION("Removed registrations cannot receive stale notifications")
    {
        auto resolved = resolver.step('z');
        const auto removed_copy = top_handle;
        top_handle.remove();
        REQUIRE(!removed_copy);
        REQUIRE(!resolved.dispatch());
        REQUIRE(target->fallbacks.size() == 1);
        REQUIRE(target->fallbacks[0] == middle_handle);
    }

    SECTION("Removing and readding a group cancels its old queued notification")
    {
        auto resolved = resolver.step('z');
        top_handle.remove();
        const auto replacement = target->add_bindings(top_tables, tib::binding_policy::fallback);
        REQUIRE(!resolved.dispatch());
        REQUIRE(target->fallbacks.size() == 1);
        REQUIRE(target->fallbacks[0] == middle_handle);
        REQUIRE(replacement);
        REQUIRE(!top_handle);

        target->fallbacks.clear();
        resolved = resolver.step('z');
        REQUIRE(!resolved.dispatch());
        REQUIRE(target->fallbacks.size() == 2);
        REQUIRE(target->fallbacks[0] == replacement);
        REQUIRE(target->fallbacks[1] == middle_handle);
    }

    SECTION("Discarding a queued handle preserves a valid callback handle")
    {
        auto resolved = resolver.step('z');
        top_handle = {};
        REQUIRE(!resolved.dispatch());
        REQUIRE(target->fallbacks.size() == 2);
        REQUIRE(target->fallbacks[0]);
        REQUIRE(target->fallbacks[1] == middle_handle);
    }

    SECTION("Pending fallback handles do not keep their target alive")
    {
        auto resolved = resolver.step('z');
        std::weak_ptr<binding_fallback_tester> lifetime = target;
        target.reset();
        REQUIRE(lifetime.expired());
        REQUIRE(!top_handle);
        REQUIRE(!middle_handle);
        REQUIRE(!resolved.dispatch());
    }

    SECTION("Callbacks can remove or discard another crossed handle")
    {
        class removing_target : public binding_fallback_tester
        {
        public:
            void on_binding_fallback(const tib::binding_handle& handle_crossed) noexcept override
            {
                binding_fallback_tester::on_binding_fallback(handle_crossed);
                if (discard_only)
                    remove_on_fallback = {};
                else
                    remove_on_fallback.remove();
            }
            tib::binding_handle remove_on_fallback;
            bool discard_only = false;
        };
        auto removing = std::make_shared<removing_target>();
        removing->add_bindings(base);
        removing->remove_on_fallback = removing->add_bindings(middle, tib::binding_policy::fallback);
        const auto first = removing->add_bindings(top, tib::binding_policy::fallback);
        tib::binding_resolver other_resolver;
        other_resolver.add_target(removing);
        SECTION("Removal cancels the later callback")
        {
            auto resolved = other_resolver.step('z');
            REQUIRE(!resolved.dispatch());
            REQUIRE(removing->fallbacks.size() == 1);
            REQUIRE(removing->fallbacks[0] == first);
        }

        SECTION("Discarding preserves the later callback")
        {
            removing->discard_only = true;
            auto resolved = other_resolver.step('z');
            REQUIRE(!resolved.dispatch());
            REQUIRE(removing->fallbacks.size() == 2);
            REQUIRE(removing->fallbacks[0] == first);
            REQUIRE(removing->fallbacks[1]);
        }
    }

    SECTION("A crossed group stays crossed while lower input remains pending")
    {
        REQUIRE(resolver.step('a').more());
        REQUIRE(resolver.step('b').more());
        REQUIRE(add_binding(*top, "abc", "new-top"));
        auto resolved = resolver.step('c');
        REQUIRE(resolved.is_func_name("base"));
        REQUIRE(resolved.dispatch());
        REQUIRE(target->fallbacks.size() == 2);
    }
}

TEST_CASE("Discarded handles preserve fallback notifications")
{
    auto target = std::make_shared<binding_fallback_tester>();
    auto base = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*base, "abc", "base"));
    target->add_bindings(base, tib::binding_policy::exclusive);
    auto middle = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*middle, "abq", "middle"));
    target->add_bindings(middle, tib::binding_policy::fallback);
    auto top = std::make_shared<tib::key_table>();
    REQUIRE(add_binding(*top, "ax", "top"));
    target->add_bindings(top, tib::binding_policy::fallback);
    tib::binding_resolver resolver;
    resolver.add_target(target);

    REQUIRE(resolver.step('a').more());
    REQUIRE(resolver.step('x').is_func_name("top"));
    REQUIRE(resolver.step('a').more());
    REQUIRE(resolver.step('b').more());
    auto resolved = resolver.step('c');
    REQUIRE(resolved.is_func_name("base"));
    REQUIRE(target->fallbacks.empty());
    REQUIRE(resolved.dispatch());
    REQUIRE(target->fallbacks.size() == 2);
    REQUIRE(target->fallbacks[0]);
    REQUIRE(target->fallbacks[1]);
    REQUIRE(target->fallbacks[0] != target->fallbacks[1]);
    REQUIRE(target->get_bindings().size() == 3);
    REQUIRE(resolver.step('a').more());
    REQUIRE(resolver.step('b').more());
    REQUIRE(resolver.step('c').is_func_name("base"));

    // A callback handle can remove bindings whose original handle was discarded.
    auto top_handle = target->fallbacks[0];
    top_handle.remove();
    REQUIRE(!target->fallbacks[0]);
    REQUIRE(target->get_bindings().size() == 2);
}

TEST_CASE("Binding fallback self-insert policy")
{
    auto target = std::make_shared<binding_fallback_tester>();
    target->add_bindings(std::make_shared<tib::key_table>(true));
    auto mode = std::make_shared<tib::key_table>(false);
    const auto handle = target->add_bindings(mode, tib::binding_policy::fallback);
    tib::binding_resolver resolver;
    resolver.add_target(target);

    SECTION("A missed group's self-insert prohibition does not affect its fallback")
    {
        auto resolved = resolver.step('x');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
        REQUIRE(resolved.dispatch());
        REQUIRE(target->fallbacks.size() == 1);
        REQUIRE(target->fallbacks[0] == handle);
    }

    SECTION("Self-insert in the upper group prevents fallback")
    {
        mode->set_can_self_insert(true);
        REQUIRE(resolver.step(0xc3).more());
        auto resolved = resolver.step(0xa9);
        REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
        REQUIRE(resolved.sequence == "\xc3\xa9");
        REQUIRE(resolved.dispatch());
        REQUIRE(target->fallbacks.empty());
    }
}

TEST_CASE("Key bindings")
{
    SECTION("Main")
    {
        auto base = std::make_shared<tib::key_table>();
        add_binding(*base, "\x1b[A", "command-one");
        add_binding(*base, "\x1b[B", "command-two");
        add_binding(*base, "\x1b[1~", "command-one");

        auto overlay = std::make_shared<tib::key_table>();
        add_binding(*overlay, "\x1b[A", "command-override");

        auto tables = std::make_shared<tib::key_table_list>();
        tables->push_back(base);
        std::shared_ptr<dispatcher_tester> tester = std::make_shared<dispatcher_tester>();
        tester->add_bindings(tables, tib::binding_policy::exclusive);

        tib::binding_resolver resolver;

        SECTION("Base table")
        {
            assert(base->can_self_insert() <= 0);
            resolver.add_target(tester);

            {
                REQUIRE(resolver.step('\x1b').more());
                REQUIRE(resolver.step('[').more());
                auto resolved = resolver.step('A');
                REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
                REQUIRE(resolved.is_func_name("command-one"));
            }

            {
                auto resolved = resolver.step('x');
                REQUIRE(resolved.outcome == tib::dispatch_outcome::miss);
                REQUIRE(!resolved.binding_target);
                REQUIRE(resolved.sequence == "x");
            }

            {
                REQUIRE(resolver.step('\x1b').more());
                REQUIRE(resolver.step('[').more());
                auto resolved = resolver.step('Z');
                REQUIRE(resolved.outcome == tib::dispatch_outcome::miss);
                REQUIRE(!resolved.binding_target);
                REQUIRE(resolved.sequence == "Z");
            }
        }

        SECTION("Self insert")
        {
            assert(base->can_self_insert() <= 0);
            base->set_can_self_insert(true);
            resolver.add_target(tester);

            {
                REQUIRE(resolver.step('\x1b').more());
                REQUIRE(resolver.step('[').more());
                auto resolved = resolver.step('A');
                REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
                REQUIRE(resolved.is_func_name("command-one"));
            }

            {
                auto resolved = resolver.step('x');
                REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
                REQUIRE(!resolved.binding_target);
                REQUIRE(resolved.sequence == "x");
            }

            {
                REQUIRE(resolver.step('\x1b').more());
                REQUIRE(resolver.step('[').more());
                auto resolved = resolver.step('Z');
                REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
                REQUIRE(!resolved.binding_target);
                REQUIRE(resolved.sequence == "Z");
            }
        }

        SECTION("Overlay table")
        {
            assert(base->can_self_insert() <= 0);
            tables->push_back(overlay);
            resolver.add_target(tester);

            {
                REQUIRE(resolver.step('\x1b').more());
                REQUIRE(resolver.step('[').more());
                auto resolved = resolver.step('A');
                REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
                REQUIRE(resolved.is_func_name("command-override"));
            }

            {
                REQUIRE(resolver.step('\x1b').more());
                REQUIRE(resolver.step('[').more());
                auto resolved = resolver.step('B');
                REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
                REQUIRE(resolved.is_func_name("command-two"));
            }
        }

        SECTION("Pattern bindings")
        {
            const auto resolve_mouse = [&](const char* pattern,
                                           const char* target,
                                           const char* sequence,
                                           size_t sequence_length,
                                           std::initializer_list<const char*> expected_params) {
                REQUIRE(base->add({ pattern, tib::binding_target_func(target), true }));
                resolver.add_target(tester);

                tib::resolved_binding resolved;
                for (size_t i = 0; i < sequence_length; ++i)
                {
                    resolved = resolver.step(uint8_t(sequence[i]));
                    REQUIRE(i + 1 == sequence_length || resolved.more());
                }
                REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
                REQUIRE(resolved.is_func_name(target));
                REQUIRE(resolved.params.size() == expected_params.size());

                size_t i = 0;
                for (const char* expected : expected_params)
                    REQUIRE(resolved.params[i++] == tib::cstring(expected));
            };

            SECTION("VT200, default encoding")
            {
                const char sequence[] = "\x1b[M\x23\x58\x6e";
                resolve_mouse("\x1b[M%!%!%!", "vt200-default", sequence, sizeof(sequence) - 1,
                              { "3", "56", "78" });
            }

            SECTION("VT200, SGR encoding")
            {
                const char sequence[] = "\x1b[<2;56;78m";
                resolve_mouse("\x1b[<%#;%#;%#m", "vt200-sgr", sequence, sizeof(sequence) - 1,
                              { "2", "56", "78" });
            }

            SECTION("DRAG, default encoding")
            {
                const char sequence[] = "\x1b[M\x42\x20\x2a";
                resolve_mouse("\x1b[M%!%!%!", "drag-default", sequence, sizeof(sequence) - 1,
                              { "34", "0", "10" });
            }

            SECTION("DRAG, SGR encoding")
            {
                const char sequence[] = "\x1b[<34;9;10M";
                resolve_mouse("\x1b[<%#;%#;%#M", "drag-sgr", sequence, sizeof(sequence) - 1,
                              { "34", "9", "10" });
            }

            SECTION("ANY, default encoding")
            {
                const char sequence[] = { '\x1b', '[', 'M', '\x43', char(0xff), '\0' };
                resolve_mouse("\x1b[M%!%!%!", "any-default", sequence, sizeof(sequence),
                              { "35", "223", "" });
            }

            SECTION("ANY, SGR encoding")
            {
                const char sequence[] = "\x1b[<35;223;224M";
                resolve_mouse("\x1b[<%#;%#;%#M", "any-sgr", sequence, sizeof(sequence) - 1,
                              { "35", "223", "224" });
            }
        }
    }
}

TEST_CASE("Shadowed key bindings")
{
    const auto make_resolver = [](std::shared_ptr<tib::key_table> table,
                                  std::shared_ptr<dispatcher_tester>& tester) {
        tester = std::make_shared<dispatcher_tester>();
        tester->add_bindings(std::move(table), tib::binding_policy::exclusive);
        tib::binding_resolver resolver;
        resolver.add_target(tester);
        return resolver;
    };

    SECTION("Longer binding wins")
    {
        auto table = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*table, "a", "short"));
        REQUIRE(add_binding(*table, "abc", "long"));
        std::shared_ptr<dispatcher_tester> tester;
        auto resolver = make_resolver(std::move(table), tester);

        auto resolved = resolver.step('a');
        REQUIRE(resolved.more());
        REQUIRE(resolved.ambiguous());
        resolved = resolver.step('b');
        REQUIRE(resolved.more());
        REQUIRE(resolved.ambiguous());
        resolved = resolver.step('c');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.sequence == "abc");
        REQUIRE(resolved.key == 'c');
        REQUIRE(resolved.is_func_name("long"));
    }

    SECTION("Mismatch replays the suffix")
    {
        auto table = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*table, "a", "short"));
        REQUIRE(add_binding(*table, "abc", "long"));
        std::shared_ptr<dispatcher_tester> tester;
        auto resolver = make_resolver(std::move(table), tester);

        REQUIRE(resolver.step('a').ambiguous());
        REQUIRE(resolver.step('b').ambiguous());
        auto resolved = resolver.step('x');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.sequence == "a");
        REQUIRE(resolved.key == 'a');
        REQUIRE(resolved.is_func_name("short"));
        REQUIRE(resolved.dispatch());
        REQUIRE(tib::term_in() == 'b');
        REQUIRE(tib::term_in() == 'x');
    }

    SECTION("Nearest nested binding wins")
    {
        auto table = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*table, "a", "short"));
        REQUIRE(add_binding(*table, "abc", "middle"));
        REQUIRE(add_binding(*table, "abcde", "long"));
        std::shared_ptr<dispatcher_tester> tester;
        auto resolver = make_resolver(std::move(table), tester);

        REQUIRE(resolver.step('a').ambiguous());
        REQUIRE(resolver.step('b').ambiguous());
        REQUIRE(resolver.step('c').ambiguous());
        REQUIRE(resolver.step('d').ambiguous());
        auto resolved = resolver.step('x');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.sequence == "abc");
        REQUIRE(resolved.is_func_name("middle"));
        REQUIRE(resolved.dispatch());
        REQUIRE(tib::term_in() == 'd');
        REQUIRE(tib::term_in() == 'x');
    }

    SECTION("Pending binding can be resolved explicitly")
    {
        auto table = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*table, "a", "short"));
        REQUIRE(add_binding(*table, "abc", "long"));
        std::shared_ptr<dispatcher_tester> tester;
        auto resolver = make_resolver(std::move(table), tester);

        REQUIRE(resolver.step('a').ambiguous());
        REQUIRE(resolver.step('b').ambiguous());
        auto resolved = resolver.resolve_pending();
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.sequence == "a");
        REQUIRE(resolved.is_func_name("short"));
        REQUIRE(resolved.dispatch());
        REQUIRE(tib::term_in() == 'b');
    }

    SECTION("Self insert is an implicit fallback")
    {
        auto table = std::make_shared<tib::key_table>();
        table->set_can_self_insert(true);
        REQUIRE(add_binding(*table, "abc", "long"));
        std::shared_ptr<dispatcher_tester> tester;
        auto resolver = make_resolver(std::move(table), tester);

        auto miss_table = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*miss_table, "z", "miss"));
        auto miss_target = std::make_shared<binding_fallback_tester>();
        miss_target->add_bindings(std::move(miss_table), tib::binding_policy::exclusive);
        miss_target->add_bindings(std::make_shared<tib::key_table>(), tib::binding_policy::fallback);
        resolver.add_target(miss_target);

        REQUIRE(resolver.step('a').ambiguous());
        REQUIRE(resolver.step('b').ambiguous());
        auto resolved = resolver.step('x');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
        REQUIRE(resolved.sequence == "a");
        REQUIRE(resolved.key == 'a');
        REQUIRE(!resolved.binding_target);
        REQUIRE(miss_target->get_fallback_count() == 0);
        REQUIRE(resolved.dispatch());
        REQUIRE(tib::term_in() == 'b');
        REQUIRE(tib::term_in() == 'x');
    }

    SECTION("Macro fallback precedes replayed suffix")
    {
        auto table = std::make_shared<tib::key_table>();
        REQUIRE(table->add("a", tib::binding_target_macro("M")));
        REQUIRE(add_binding(*table, "abc", "long"));
        std::shared_ptr<dispatcher_tester> tester;
        auto resolver = make_resolver(std::move(table), tester);
        test_input_stream pending("P");
        REQUIRE(tib::term_in_peek() == 'P');

        REQUIRE(resolver.step('a').ambiguous());
        REQUIRE(resolver.step('b').ambiguous());
        auto resolved = resolver.step('x');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.binding_target.get_type() == tib::binding_type::macro);
        REQUIRE(resolved.dispatch());
        REQUIRE(tib::term_has_macro_input());
        REQUIRE(tib::term_in_peek() == 'M');
        REQUIRE(tib::term_in() == 'M');
        REQUIRE(!tib::term_has_macro_input());
        REQUIRE(tib::term_in_peek() == 'b');
        REQUIRE(tib::term_in() == 'b');
        REQUIRE(tib::term_in() == 'x');
        REQUIRE(tib::term_in() == 'P');
    }

    SECTION("Pattern binding can be the fallback")
    {
        auto table = std::make_shared<tib::key_table>();
        REQUIRE(table->add({ "a%#", tib::binding_target_func("pattern"), true }));
        REQUIRE(add_binding(*table, "a12z", "long"));
        std::shared_ptr<dispatcher_tester> tester;
        auto resolver = make_resolver(std::move(table), tester);

        REQUIRE(resolver.step('a').more());
        REQUIRE(resolver.step('1').ambiguous());
        REQUIRE(resolver.step('2').ambiguous());
        auto resolved = resolver.step('x');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.sequence == "a12");
        REQUIRE(resolved.is_func_name("pattern"));
        REQUIRE(resolved.params.size() == 1);
        REQUIRE(resolved.params[0] == "12");
        REQUIRE(resolved.dispatch());
        REQUIRE(tib::term_in() == 'x');
    }

    SECTION("Longer overlay binding shadows a base binding")
    {
        auto base = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*base, "a", "base"));
        auto overlay = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*overlay, "abc", "overlay"));
        auto tester = std::make_shared<dispatcher_tester>();
        auto tables = std::make_shared<tib::key_table_list>();
        tables->push_back(std::move(base));
        tables->push_back(std::move(overlay));
        tester->add_bindings(std::move(tables), tib::binding_policy::exclusive);
        tib::binding_resolver resolver;
        resolver.add_target(tester);

        REQUIRE(resolver.step('a').ambiguous());
        REQUIRE(resolver.step('b').ambiguous());
        auto resolved = resolver.step('c');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.is_func_name("overlay"));
    }

    SECTION("Lowercase-version binding can be a fallback")
    {
        auto table = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*table, "ax", "lower"));
        REQUIRE(table->add("aX", tib::binding_target_lowercase_version()));
        REQUIRE(add_binding(*table, "aXz", "long"));
        std::shared_ptr<dispatcher_tester> tester;
        auto resolver = make_resolver(std::move(table), tester);

        REQUIRE(resolver.step('a').more());
        REQUIRE(resolver.step('X').ambiguous());
        auto resolved = resolver.step('q');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.sequence == "ax");
        REQUIRE(resolved.key == 'x');
        REQUIRE(resolved.is_func_name("lower"));
        REQUIRE(resolved.dispatch());
        REQUIRE(tib::term_in() == 'q');
    }

    SECTION("Shadow fallback suppresses table fallback")
    {
        auto prefix_table = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*prefix_table, "a", "short"));
        REQUIRE(add_binding(*prefix_table, "abc", "long"));
        auto prefix_target = std::make_shared<dispatcher_tester>();
        prefix_target->add_bindings(std::move(prefix_table), tib::binding_policy::exclusive);

        auto miss_table = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*miss_table, "z", "miss"));
        auto miss_target = std::make_shared<binding_fallback_tester>();
        miss_target->add_bindings(std::move(miss_table), tib::binding_policy::exclusive);
        miss_target->add_bindings(std::make_shared<tib::key_table>(), tib::binding_policy::fallback);

        tib::binding_resolver resolver;
        resolver.add_target(prefix_target);
        resolver.add_target(miss_target);
        REQUIRE(resolver.step('a').ambiguous());
        REQUIRE(resolver.step('b').ambiguous());
        auto resolved = resolver.step('x');
        REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
        REQUIRE(resolved.is_func_name("short"));
        REQUIRE(miss_target->get_fallback_count() == 0);
        REQUIRE(resolved.dispatch());
        REQUIRE(tib::term_in() == 'b');
        REQUIRE(tib::term_in() == 'x');
    }

    SECTION("UTF8 self insert waits for the complete codepoint")
    {
        auto table = std::make_shared<tib::key_table>();
        table->set_can_self_insert(true);
        REQUIRE(add_binding(*table, "\xf0\x9fX", "long"));
        std::shared_ptr<dispatcher_tester> tester;
        auto resolver = make_resolver(std::move(table), tester);
        test_input_stream remaining("\x80", 1);

        REQUIRE(resolver.step(0xf0).more());
        REQUIRE(resolver.step(0x9f).more());
        REQUIRE(resolver.step(0x98).more());
        auto resolved = resolver.step(uint8_t(tib::term_in()));
        REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
        REQUIRE(resolved.sequence == tib::cstring("\xf0\x9f\x98\x80", 4));
        REQUIRE(resolved.dispatch());
        REQUIRE(remaining.empty());
    }

    SECTION("Reset discards an ambiguous sequence")
    {
        auto table = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*table, "a", "short"));
        REQUIRE(add_binding(*table, "abc", "long"));
        std::shared_ptr<dispatcher_tester> tester;
        auto resolver = make_resolver(std::move(table), tester);

        REQUIRE(resolver.step('a').ambiguous());
        resolver.reset();
        auto resolved = resolver.resolve_pending();
        REQUIRE(resolved.outcome == tib::dispatch_outcome::miss);
        REQUIRE(resolved.sequence.empty());
    }
}

TEST_CASE("UTF8 multi-byte input")
{
    SECTION("Dispatcher preserves UTF8 bytes")
    {
        REQUIRE(!tib::g_optimize_self_insert);

        auto input = std::make_shared<tib::editor_context>();
        input->initialize();
        input->add_bindings(tib::make_default_key_table(), tib::binding_policy::exclusive);

        tib::binding_resolver resolver;
        resolver.add_target(input);

        const char utf8[] = "\xf0\x9f\x98\x80";
        size_t index = 0;
        for (const char c : utf8)
        {
            if (c)
            {
                auto resolved = resolver.step(c);
                if (++index < sizeof(utf8) - 1)
                    REQUIRE(resolved.more());
                else
                {
                    REQUIRE(resolved.outcome == tib::dispatch_outcome::self_insert);
                    REQUIRE(resolved.sequence == tib::cstring(utf8));
                    REQUIRE(resolved.dispatch());
                }
            }
        }
        REQUIRE(input->get_text() == tib::cstring(utf8));
    }
}

PERF_CASE("PERF, resolve 26000 bindings")
{
    SECTION("Main")
    {
        static const char* const c_sequences[] = // 260 sequences.
        {
            // 100 one-char sequences.
            "\001", "\002", "\003", "\004", "\005", "\006", "\007", "\010", "\011", "\012",
            "\013", "\014", "\015", "\016", "\017", "\020", "\021", "\022", "\023", "\024",
            "a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
            "k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
            "u", "v", "w", "x", "y", "z", "[", "]", ",", ".",
            "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
            "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
            "U", "V", "W", "X", "Y", "Z", "{", "}", "<", ">",
            "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
            "!", "@", "#", "$", "%", "^", "&", "*", "(", ")",
            // 30 two-char sequences.
            "\033A", "\033B", "\033C", "\033D", "\033E", "\033F", "\033G", "\033H", "\033I", "\033J",
            "\033K", "\033L", "\033M", "\033N", "\033O", "\033P", "\033Q", "\033R", "\033S", "\033T",
            "\033U", "\033V", "\033W", "\033X", "\033Y", "\033Z", "\033!", "\033@", "\033#", "\033$",
            // 30 three-char sequences.
            "\033[A", "\033[B", "\033[C", "\033[D", "\033[E", "\033[F", "\033[G", "\033[H", "\033[I", "\033[J",
            "\033[K", "\033[L", "\033[M", "\033[N", "\033[O", "\033[P", "\033[Q", "\033[R", "\033[S", "\033[T",
            "\033[U", "\033[V", "\033[W", "\033[X", "\033[Y", "\033[Z", "\033[!", "\033[@", "\033[#", "\033[$",
            // 40 longer sequences.
            "\033[27;27~",
            "\033[27;1;101~",
            "\033[27;1;102~",
            "\033[27;1;103~",
            "\033[27;1;104~",
            "\033[27;1;105~",
            "\033[27;1;106~",
            "\033[27;1;107~",
            "\033[27;1;108~",
            "\033[27;1;109~",
            "\033[27;2;100~",
            "\033[27;2;101~",
            "\033[27;2;102~",
            "\033[27;2;103~",
            "\033[27;2;104~",
            "\033[27;2;105~",
            "\033[27;2;106~",
            "\033[27;2;107~",
            "\033[27;2;108~",
            "\033[27;2;109~",
            "\033[27;3;100~",
            "\033[27;3;101~",
            "\033[27;3;102~",
            "\033[27;3;103~",
            "\033[27;3;104~",
            "\033[27;3;105~",
            "\033[27;3;106~",
            "\033[27;3;107~",
            "\033[27;3;108~",
            "\033[27;3;109~",
            "\033[27;4;100~",
            "\033[27;4;101~",
            "\033[27;4;102~",
            "\033[27;4;103~",
            "\033[27;4;104~",
            "\033[27;4;105~",
            "\033[27;4;106~",
            "\033[27;4;107~",
            "\033[27;4;108~",
            "\033[27;4;109~",
            // 40 more interleaved longer sequences.
            "\033[27;3;150~",
            "\033[27;3;151~",
            "\033[27;3;152~",
            "\033[27;3;153~",
            "\033[27;3;154~",
            "\033[27;3;155~",
            "\033[27;3;156~",
            "\033[27;3;157~",
            "\033[27;3;158~",
            "\033[27;3;159~",
            "\033[27;3;160~",
            "\033[27;3;161~",
            "\033[27;3;162~",
            "\033[27;3;163~",
            "\033[27;3;164~",
            "\033[27;3;165~",
            "\033[27;3;166~",
            "\033[27;3;167~",
            "\033[27;3;168~",
            "\033[27;3;169~",
            "\033[27;3;170~",
            "\033[27;3;171~",
            "\033[27;3;172~",
            "\033[27;3;173~",
            "\033[27;3;174~",
            "\033[27;3;175~",
            "\033[27;3;176~",
            "\033[27;3;177~",
            "\033[27;3;178~",
            "\033[27;3;179~",
            "\033[27;3;180~",
            "\033[27;3;181~",
            "\033[27;3;182~",
            "\033[27;3;183~",
            "\033[27;3;184~",
            "\033[27;3;185~",
            "\033[27;3;186~",
            "\033[27;3;187~",
            "\033[27;3;188~",
            "\033[27;3;189~",
            // 10 short chord sequences.
            "\030a", "\030b", "\030c", "\030d", "\030e", "\030f", "\030g", "\030h", "\030i", "\030j",
            // 10 long chord sequences.
            "\030\033[27;1;101~",
            "\030\033[27;1;102~",
            "\030\033[27;1;103~",
            "\030\033[27;1;104~",
            "\030\033[27;1;105~",
            "\030\033[27;1;106~",
            "\030\033[27;1;107~",
            "\030\033[27;1;108~",
            "\030\033[27;1;109~",
            "\030\033[27;1;110~",
        };
        static_assert(std::size(c_sequences) == 260);

        auto table = std::make_shared<tib::key_table>();
        for (const char* sequence : c_sequences)
            REQUIRE(add_binding(*table, sequence, "command-one"));

        std::shared_ptr<dispatcher_tester> tester = std::make_shared<dispatcher_tester>();
        tester->add_bindings(table, tib::binding_policy::exclusive);

        tib::binding_resolver resolver;
        resolver.add_target(tester);

        constexpr uint32_t c_passes = 100;

        uint32_t num_resolved = 0;
        for (size_t pass = 0; pass < c_passes; ++pass)
        {
            for (const char* sequence : c_sequences)
            {
                tib::dispatch_outcome outcome = tib::dispatch_outcome::miss;
                for (const char* p = sequence; *p; ++p)
                {
                    auto resolved = resolver.step(*p);
                    outcome = resolved.outcome;
                    REQUIRE(!p[1] || resolved.more());
                }
                REQUIRE(outcome == tib::dispatch_outcome::match);
                ++num_resolved;
            }
        }

        REQUIRE(num_resolved == c_passes * std::size(c_sequences));

        static_assert(c_passes * std::size(c_sequences) == 26000);
    }
}

PERF_CASE("PERF, resolve 1000 pathological shadowed sequences")
{
    SECTION("Main")
    {
        constexpr size_t c_binding_count = 100;
        constexpr size_t c_average_length = 20;
        constexpr size_t c_middle_length = 20;
        constexpr size_t c_long_length = 39;
        constexpr size_t c_passes = 1000;
        static_assert(1 + (c_binding_count - 2) * c_middle_length + c_long_length ==
                      c_binding_count * c_average_length);

        auto table = std::make_shared<tib::key_table>();
        REQUIRE(add_binding(*table, "a", "short"));

        // These 98 bindings share a 19-byte prefix, making their binary-search
        // comparisons relatively expensive, but none matches the test input.
        char sequence[c_middle_length + 1];
        memset(sequence, 'a', c_middle_length);
        sequence[c_middle_length] = '\0';
        for (size_t i = 0; i < c_binding_count - 2; ++i)
        {
            uint8_t discriminator = uint8_t(i + 1);
            if (discriminator >= 'a')
                ++discriminator;
            sequence[c_middle_length - 1] = char(discriminator);
            REQUIRE(add_binding(*table, sequence, "middle"));
        }

        // Together with the one-byte binding and 98 twenty-byte bindings,
        // this makes the average binding length exactly 20 bytes.
        char long_sequence[c_long_length + 1];
        memset(long_sequence, 'a', c_long_length);
        long_sequence[c_long_length - 1] = 'z';
        long_sequence[c_long_length] = '\0';
        REQUIRE(add_binding(*table, long_sequence, "long"));

        auto tester = std::make_shared<dispatcher_tester>();
        tester->add_bindings(table, tib::binding_policy::exclusive);
        tib::binding_resolver resolver;
        resolver.add_target(tester);

        // Match the long binding through its penultimate byte, then mismatch.
        // Resolving must search every shorter length before finding "a" as the
        // shadow fallback and treating the remaining 38 bytes as replay text.
        char input[c_long_length];
        memset(input, 'a', sizeof(input));
        input[c_long_length - 1] = 'x';

        uint32_t num_resolved = 0;
        for (size_t pass = 0; pass < c_passes; ++pass)
        {
            bool all_prefixes = true;
            for (size_t i = 0; i + 1 < sizeof(input); ++i)
                all_prefixes = resolver.step(uint8_t(input[i])).more() && all_prefixes;

            const auto resolved = resolver.step(uint8_t(input[sizeof(input) - 1]));
            REQUIRE(all_prefixes);
            REQUIRE(resolved.outcome == tib::dispatch_outcome::match);
            REQUIRE(resolved.sequence == "a");
            REQUIRE(resolved.is_func_name("short"));
            ++num_resolved;
        }

        REQUIRE(num_resolved == c_passes);
    }
}

PERF_CASE("PERF, resolve default commands 10000 times")
{
    SECTION("Main")
    {
        tib::editor_context::ensure_commands();
        const auto& commands = tib::editor_context::get_registered_commands();

        constexpr uint32_t c_passes = 10000;

        uint32_t num_resolved = 0;
        for (size_t pass = 0; pass < c_passes; ++pass)
        {
            for (const auto& command : commands)
            {
                REQUIRE(tib::editor_context::lookup_command(command.first) == command.second);
                ++num_resolved;
            }
        }

        REQUIRE(num_resolved == c_passes * commands.size());
    }
}
