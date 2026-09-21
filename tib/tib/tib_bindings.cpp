// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "pch.h"
#include "maybe_windows.h"
#include "tib_base.h"
#include "tib_bindings.h"
#include "tib_context.h"
#include "tib_terminal.h"
#include <algorithm>
#include <assert.h>

namespace tib {

struct binding_resolver_state
{
    std::weak_ptr<dispatcher_target> quoted_insert_target;
};

enum class binding_match
{
    none,
    prefix,
    complete,
};

struct pattern_match
{
    binding_match       match = binding_match::none;
    size_t              length = 0;
    binding_params      params;
};

static pattern_match match_pattern(const key_binding* pattern, const char* input, size_t input_length)
{
    const char* const sequence = pattern->sequence.c_str();
    const size_t sequence_length = pattern->sequence.length();
    size_t input_pos = 0;
    size_t sequence_pos = 0;
    pattern_match result;

    while (input_pos < input_length && sequence_pos < sequence_length)
    {
        if (sequence[sequence_pos] == '%')
        {
            if (sequence_pos + 1 >= sequence_length)
            {
                assert(false && "malformed binding pattern");
                return result;
            }

            const char op = sequence[sequence_pos + 1];
            switch (op)
            {
            case '#':
                // Match 1 or more digits.
                if (input[input_pos] >= '0' && input[input_pos] <= '9')
                {
                    const size_t param_begin = input_pos;
                    do
                    {
                        ++input_pos;
                    } while (input_pos < input_length && input[input_pos] >= '0' && input[input_pos] <= '9');
                    result.params.emplace_back(input + param_begin, input_pos - param_begin);
                    sequence_pos += 2;
                    continue;
                }
                // Not a digit; pattern does not match.
                return result;

            case '!':
                // Match 1 character in the range 0x20..0xff or 0x00.
                // If >= 0x20 then subtract 0x20 and convert it to a numeric
                // string.  If 0x00 then convert 0 to a numeric string.  This
                // lets consumers of tib support the default mouse encoding
                // without needing to parse the raw bytes themselves.
                if (!input[input_pos] || uint8_t(input[input_pos]) >= 0x20)
                {
                    cstring param;
                    const uint8_t value = uint8_t(input[input_pos]);
                    if (value >= 0x20)
                        param.printf("%u", value - 0x20);
                    result.params.emplace_back(std::move(param));
                    sequence_pos += 2;
                    ++input_pos;
                    continue;
                }
                return result;

            case '%':
            default:
                ++sequence_pos;
                break;
            }
        }

        if (input[input_pos] != sequence[sequence_pos])
            return result;
        ++input_pos;
        ++sequence_pos;
    }

    if (sequence_pos == sequence_length)
    {
        // If the pattern sequence has been consumed then it might only be a
        // prefix of the full input.  A prefix is a shadow fallback, and the
        // input_pos separates it from the surplus input that must be
        // replayed.
        result.match = binding_match::complete;
        result.length = input_pos;
    }
    else if (input_pos == input_length)
    {
        // If the input has been exhausted before the pattern is complete,
        // then more input could still match.
        result.match = binding_match::prefix;
    }
    return result;
}

binding_target::binding_target(binding_type type, const char* text, size_t len) noexcept
{
    switch (type)
    {
    case binding_type::none:
        assert(m_type == binding_type::none);
        assert(!m_text);
        assert(!m_length);
        break;
    case binding_type::func:
        set_func(text);
        break;
    case binding_type::macro:
        set_macro(text, len);
        break;
    case binding_type::quoted_insert:
        assert(len == 1);
        assert(*text && uint8_t(*text) < c_input_terminal_reserved_begin);
        set_quoted_insert(*text);
        break;
    case binding_type::lowercase_version:
        assert(!text);
        assert(!len);
        set_lowercase_version();
        break;
    default:
        assert(false);
        break;
    }
}

binding_target binding_target_func(const char* name)
{
    return binding_target(binding_type::func, name);
}

binding_target binding_target_macro(const char* text, size_t len)
{
    return binding_target(binding_type::macro, text, len);
}

binding_target binding_target_quoted_insert(char c)
{
    return binding_target(binding_type::quoted_insert, &c, 1);
}

binding_target binding_target_lowercase_version()
{
    return binding_target(binding_type::lowercase_version, nullptr, 0);
}

bool binding_target::operator==(const binding_target& t) const noexcept
{
    if (m_type != t.m_type)
        return false;
    switch (m_type)
    {
    case binding_type::none:
    case binding_type::lowercase_version:
        break;
    case binding_type::func:
        if (!m_text != !t.m_text)
            return false;
        if (m_text && t.m_text && strcmp(m_text, t.m_text) != 0)
            return false;
        break;
    case binding_type::macro:
        if (!m_text != !t.m_text)
            return false;
        if (m_length != t.m_length)
            return false;
        if (m_text && t.m_text && memcmp(m_text, t.m_text, m_length) != 0)
            return false;
        break;
    default:
        assert(false);
        break;
    }
    return true;
}

bool binding_target::is_func_name(const char* name) const noexcept
{
    return (name && m_type == binding_type::func && m_text && strcmp(name, m_text) == 0);
}

void binding_target::clear() noexcept
{
    m_type = binding_type::none;
    m_text = nullptr;
    m_length = 0;
}

void binding_target::set_func(const char* name) noexcept
{
    assert(name);
    m_type = binding_type::func;
    m_text = name;
    m_length = 0;
}

void binding_target::set_macro(const char* text, size_t len) noexcept
{
    assert(text);
    len = resolve_auto_length(len, text);
    m_type = binding_type::macro;
    m_text = text;
    m_length = len;
}

void binding_target::set_quoted_insert(char c) noexcept
{
    assert(c && uint8_t(c) < c_input_terminal_reserved_begin);
    m_type = binding_type::quoted_insert;
    m_text = nullptr;
    m_length = uint8_t(c);
}

void binding_target::set_lowercase_version() noexcept
{
    m_type = binding_type::lowercase_version;
    m_text = nullptr;
    m_length = 0;
}

binding_target_copy::binding_target_copy(const binding_target& t) noexcept
{
    *this = t;
}

binding_target_copy& binding_target_copy::operator=(const binding_target& t) noexcept
{
    switch (t.get_type())
    {
    case binding_type::none:
        m_owned_text.clear();
        clear();
        break;
    case binding_type::func:
        m_owned_text.set(t.get_text());
        set_func(m_owned_text.c_str());
        break;
    case binding_type::macro:
        m_owned_text.set(t.get_text(), t.get_length());
        set_macro(m_owned_text.c_str(), m_owned_text.length());
        break;
    case binding_type::quoted_insert:
        m_owned_text.clear();
        set_quoted_insert(char(t.get_char()));
        break;
    case binding_type::lowercase_version:
        m_owned_text.clear();
        set_lowercase_version();
        break;
    default:
        assert(false);
        m_owned_text.clear();
        clear();
        break;
    }
    return *this;
}

key_table::~key_table()
{
}

int key_table::sort_predicate(const key_binding& candidate, const key_binding& binding)
{
    // Sort order is:
    //  1.  Literal bindings, in alphabetical order.
    //  2.  Pattern bindings, in alphabetical order.
    int comparison = int(candidate.pattern) - int(binding.pattern);
    if (!comparison)
    {
        const size_t common_length = min(candidate.sequence.length(), binding.sequence.length());
        comparison = memcmp(candidate.sequence.c_str(), binding.sequence.c_str(), common_length);
        if (!comparison)
            comparison = int(candidate.sequence.length()) - int(binding.sequence.length());
    }
    return comparison < 0;
}

bool key_table::add(key_binding&& binding)
{
    assert(binding.sequence.length() > 0);
    if (!binding.sequence.length())
        return false;

    if (binding.target.get_type() == binding_type::lowercase_version)
    {
        const char last = binding.sequence.c_str()[binding.sequence.length() - 1];
        assert(!binding.pattern && last >= 'A' && last <= 'Z');
        if (binding.pattern || last < 'A' || last > 'Z')
            return false;
    }

    const auto found = std::lower_bound(m_bindings.begin(), m_bindings.end(), binding, sort_predicate);

    if (found != m_bindings.end() && found->sequence == binding.sequence)
        *found = std::move(binding);
    else
        m_bindings.insert(found, std::move(binding));

    return true;
}

bool key_table::add(const char* sequence, const binding_target& target, bool pattern)
{
    key_binding binding;
    binding.sequence = sequence;
    binding.target = target;
    binding.pattern = pattern;
    return add(std::move(binding));
}

bool key_table::add(const char* sequence, size_t len, const binding_target& target, bool pattern)
{
    key_binding binding;
    binding.sequence.set(sequence, len);
    binding.target = target;
    binding.pattern = pattern;
    return add(std::move(binding));
}

bool key_table::remove(const cstring& sequence, bool pattern)
{
    assert(sequence.length() > 0);
    if (!sequence.length())
        return false;

    key_binding binding;
    binding.sequence = sequence;
    binding.pattern = pattern;

    const auto found = std::lower_bound(m_bindings.begin(), m_bindings.end(), binding, sort_predicate);

    if (found != m_bindings.end() && found->sequence == sequence)
        m_bindings.erase(found);

    return true;
}

void key_table::clear()
{
    m_bindings.clear();
}

std::shared_ptr<const key_table_list> dispatcher_target::get_bindings() const
{
    if (m_override_bindings)
        return m_override_bindings;
    return m_bindings;
}

void dispatcher_target::set_bindings(std::shared_ptr<const key_table_list> bindings)
{
    m_bindings = bindings;
}

void dispatcher_target::override_bindings(std::shared_ptr<const key_table_list> bindings)
{
    m_override_bindings = bindings;
}

resolved_binding::resolved_binding(std::shared_ptr<binding_resolver_state> state)
: m_resolver_state(state)
{
}

resolved_binding::operator bool()
{
    return (outcome == dispatch_outcome::match ||
            outcome == dispatch_outcome::self_insert ||
            outcome == dispatch_outcome::quoted_insert);
}

bool resolved_binding::dispatch()
{
    const bool self_insert = (outcome == dispatch_outcome::self_insert);
    const bool quoted_insert = (outcome == dispatch_outcome::quoted_insert);
    const bool literal_insert = self_insert || quoted_insert;
    assert(implies(self_insert, is_self_insertable(key)));
    assert(implies(literal_insert, sequence.length() == 1));
    assert(implies(literal_insert, uint8_t(sequence.c_str()[0]) == key));

    switch (outcome)
    {
    case dispatch_outcome::self_insert:
    case dispatch_outcome::quoted_insert:
    case dispatch_outcome::match:
        {
            if (m_replay.length())
            {
                // Anything in m_replay is surplus following the selected
                // shadow fallback.  Put it back at the head of pushed input
                // before dispatching, so it precedes both previously pushed
                // input and any macro text that dispatching the fallback may
                // enqueue.
                if (!term_push_input(m_replay.c_str(), m_replay.length()))
                    return false;
            }

            auto ctx = dispatcher_target.lock();
            if (ctx)
            {
                tib::binding_target quoted_target;
                const tib::binding_target* target = binding_target;
                if (quoted_insert)
                {
                    quoted_target.set_quoted_insert(char(key));
                    target = &quoted_target;
                }

                assert(self_insert == !target);
                if (target && target->get_type() == binding_type::macro)
                    term_push_macro_text(target->get_text(), target->get_length());
                else
                {
                    const int32_t result = ctx->dispatch(sequence, key, target, &params);
                    if (result == c_dispatch_request_quoted_insert && m_resolver_state)
                        m_resolver_state->quoted_insert_target = ctx;
                }
                return true;
            }
            else
            {
                outcome = dispatch_outcome::expired;
            }
        }
        break;
    case dispatch_outcome::miss:
        ding();
        break;
    }

    return false;
}

binding_resolver::binding_resolver()
    : m_state(std::make_shared<binding_resolver_state>())
{
}

void binding_resolver::clear_targets()
{
    m_registrants.clear();
    m_state->quoted_insert_target.reset();
}

void binding_resolver::add_target(std::weak_ptr<dispatcher_target> target)
{
    m_registrants.emplace_back(target);
    reset();
}

void binding_resolver::reset()
{
    m_sequence.clear();
}

resolved_binding binding_resolver::step(uint8_t c)
{
    if (!m_state->quoted_insert_target.expired())
    {
        const std::weak_ptr<dispatcher_target> weak = m_state->quoted_insert_target;
        m_state->quoted_insert_target.reset();
        reset();

        resolved_binding resolved(m_state);
        resolved.sequence.append(reinterpret_cast<const char*>(&c), 1);
        resolved.key = c;
        resolved.dispatcher_target = weak;
        resolved.outcome = dispatch_outcome::quoted_insert;
        return resolved;
    }

    m_sequence.append(reinterpret_cast<const char*>(&c), 1);
    return resolve(false);
}

resolved_binding binding_resolver::resolve_pending()
{
    return resolve(true);
}

resolved_binding binding_resolver::resolve(bool force)
{
    constexpr uint32_t c_max_binding_retries = 1;

    // step() always appends a byte before resolving, but resolve_pending() is
    // public and may be called when no sequence is pending.  Besides defining
    // that case as a miss, this guard keeps the input[-1] access below safe.
    if (!m_sequence.length())
        return resolved_binding();

    struct step_state
    {
        bool            is_prefix = false;
        int8_t          can_self_insert = -1;

        // Resolution cannot return the first complete match: it must retain
        // enough information to construct a resolved_binding after every
        // table has had a chance to provide a longer match or prefix.  The
        // saved_state below copies this once per dispatcher target so an
        // on_binding_miss() retry can discard candidates from stale bindings.
        struct candidate
        {
            size_t          length = 0;
            cstring         sequence;
            int32_t         key = 0;
            const binding_target* binding = nullptr;
            std::weak_ptr<dispatcher_target> dispatcher;
            binding_params  params;
            bool            self_insert = false;
        } best;
    };

#ifdef DEBUG
    bool retried_sequence = false;
#endif

retry_sequence:
    const char* const input = m_sequence.c_str();
    const size_t input_length = m_sequence.length();
    const uint8_t c = input[input_length - 1];

    // Centralize selection of the best complete fallback found so far.
    // Longer matches win across tables and dispatcher targets.  At equal
    // length, the first explicit binding retains normal resolver priority;
    // the sole exception lets an explicit binding replace the synthesized
    // self-insert candidate for the same byte.
    const auto consider_best = [](step_state& state,
                                  size_t length,
                                  const char* sequence,
                                  int32_t key,
                                  const binding_target* binding,
                                  const std::weak_ptr<dispatcher_target>& dispatcher,
                                  binding_params&& params,
                                  bool self_insert=false) {
        if (length < state.best.length ||
            (length == state.best.length && (!state.best.self_insert || self_insert)))
            return;

        state.best.length = length;
        state.best.sequence.set(sequence, length);
        state.best.key = key;
        state.best.binding = binding;
        state.best.dispatcher = dispatcher;
        state.best.params = std::move(params);
        state.best.self_insert = self_insert;
    };

    // Search the key tables in priority order (later tables overlay earlier
    // tables) looking for the longest complete match and for any binding that
    // can consume more input.  Equal-length matches retain priority order.
    step_state state;
    for (auto& weak : m_registrants)
    {
        std::shared_ptr<dispatcher_target> target = weak.lock();
        if (!target)
            continue;

        uint32_t retry_count = 0;
retry_target:
        const step_state saved_state = state;

        const auto bindings_list = target->get_bindings();
        if (!bindings_list)
            continue;

        for (auto& table = bindings_list->rbegin(); table != bindings_list->rend(); ++table)
        {
            // Only one table can accept self-insert input; the last one in
            // the bindings list from the first dispatcher_target with a
            // self-insert table wins.
            assert(state.can_self_insert <= 0);
            if (state.can_self_insert < 0)
                state.can_self_insert = (*table)->can_self_insert();

            // Self-insert acts as an implicit one-byte binding for the first
            // input byte.  Record it as a complete fallback here so a longer
            // binding can shadow it, and so later on_binding_miss() callbacks
            // do not treat an ultimately self-insertable sequence as a miss.
            // The can_self_insert bookkeeping above preserves the rule that
            // the highest-priority table which explicitly sets self-insert
            // policy decides whether this fallback is available.
            if (state.can_self_insert > 0 && is_self_insertable(input[0]))
            {
                binding_params no_params;
                consider_best(state, 1, input, uint8_t(input[0]),
                              nullptr, weak, std::move(no_params), true);
            }

            const auto& bindings = (*table)->m_bindings;
            const auto patterns = std::partition_point(bindings.begin(), bindings.end(), [](const key_binding& binding) {
                return !binding.pattern;
            });
            const auto find_literal = [&](const char* sequence, size_t length) {
                return std::lower_bound(bindings.begin(), patterns, sequence, [&](const key_binding& candidate, const char* sequence) {
                    const size_t common_length = min(candidate.sequence.length(), length);
                    const int comparison = memcmp(candidate.sequence.c_str(), sequence, common_length);
                    return comparison < 0 || (comparison == 0 && candidate.sequence.length() < length);
                });
            };

            // Independently determine whether the whole accumulated input is
            // still the prefix of a strictly longer literal binding (versus
            // pattern bindings).  The complete-fallback search below cannot
            // answer that when an exact binding also exists for the current
            // input.
            const auto found = find_literal(input, input_length);
            if (found != patterns &&
                found->sequence.length() >= input_length &&
                memcmp(found->sequence.c_str(), input, input_length) == 0)
            {
                auto longer = found;
                if (longer->sequence.length() == input_length)
                {
                    // Skip the exact binding: only a strict extension means
                    // the resolver needs another byte.  Since the literal
                    // bindings are in sorted order, any binding extending it
                    // is immediately next.
                    ++longer;
                }
                if (longer != patterns &&
                    longer->sequence.length() > input_length &&
                    memcmp(longer->sequence.c_str(), input, input_length) == 0)
                {
                    state.is_prefix = true;
                }
            }

            // Find the longest literal binding in this table that is a prefix
            // of the accumulated input.  It becomes the fallback if a longer
            // binding eventually mismatches or the host resolves a timeout.
            // Stop after the first match in this table, but keep scanning
            // other tables: a globally longer fallback beats table priority,
            // while equal lengths retain the earlier table's priority.
            for (size_t length = input_length; length; --length)
            {
                // Look for an exact literal binding for input[0..length).
                // On a miss, continue reaches the loop's --length expression,
                // which tries the next-shorter prefix.  This visits at most
                // input_length prefixes; each visit performs one binary
                // search of the table's sorted literal bindings.
                auto matched = find_literal(input, length);
                if (matched == patterns ||
                    matched->sequence.length() != length ||
                    memcmp(matched->sequence.c_str(), input, length) != 0)
                    continue;

                // A lowercase_version binding must check if the sequence
                // matches a binding if the last byte in the sequence is
                // converted to lowercase.
                cstring matched_sequence(input, length);
                int32_t matched_key = uint8_t(input[length - 1]);
                if (matched->target.get_type() == binding_type::lowercase_version)
                {
                    const char last = input[length - 1];
                    if (last >= 'A' && last <= 'Z')
                    {
                        matched_key = last + ('a' - 'A');
                        matched_sequence.set_at(length - 1, char(matched_key));
                        matched = find_literal(matched_sequence.c_str(), length);
                    }

                    if (matched == patterns ||
                        matched->sequence.length() != length ||
                        memcmp(matched->sequence.c_str(), matched_sequence.c_str(), length) != 0 ||
                        matched->target.get_type() == binding_type::lowercase_version)
                    {
                        // If there's no matching lowercase binding, then
                        // ignore the lowercase_version binding and just
                        // continue searching the key_tables.
                        continue;
                    }
                }

                // Offer this complete literal binding to the global fallback
                // selection.  Its consumed length also identifies any suffix
                // that must later be replayed.  No shorter literal binding in
                // this table can win, so the table-local search is finished.
                binding_params no_params;
                consider_best(state, length, matched_sequence.c_str(), matched_key,
                              &matched->target, weak, std::move(no_params));
                break;
            }

            // Patterns are effectively unsorted, so check every pattern
            // binding.  A complete result may consume only an initial portion
            // of input and is therefore a fallback candidate; a prefix result
            // means at least one pattern can still consume another byte.
            for (auto pattern = patterns; pattern != bindings.end(); ++pattern)
            {
                pattern_match result = match_pattern(&*pattern, input, input_length);
                if (result.match == binding_match::complete)
                {
                    // Preserve the pattern's consumed length and captures in
                    // case it is the longest fallback.  Any unconsumed input
                    // becomes replay text when that fallback is selected.
                    consider_best(state, result.length, input, uint8_t(input[result.length - 1]),
                                  &pattern->target, weak, std::move(result.params));
                }
                else if (result.match == binding_match::prefix)
                {
                    state.is_prefix = true;
                }
            }

            // Only one table gets to accept self-insert input.
            if (state.can_self_insert > 0)
                state.can_self_insert = 0;
        }

        // A viable longer binding is not a miss, nor is input for which a
        // complete fallback (including self-insert) has already been found.
        // Invoke the callback only when neither condition is true among the
        // dispatcher targets examined so far; this also preserves the rule
        // that an earlier target's partial match suppresses later callbacks.
        if (!state.is_prefix && !state.best.length && target->on_binding_miss(m_sequence, c))
        {
            state = saved_state;
            if (++retry_count <= c_max_binding_retries)
                goto retry_target;
            assert(false && "dispatcher_target exceeded binding miss retry limit");
        }
    }

    // Decide whether to wait only after all tables and targets have
    // contributed their prefix and fallback information.  Ordinarily any
    // viable extension keeps the sequence pending.  resolve_pending() passes
    // force=true so a timeout commits an available fallback; a pure prefix
    // without a fallback must still wait.  Keep m_sequence intact while
    // returning more.
    if (state.is_prefix && !(force && state.best.length))
    {
        resolved_binding resolved;
        resolved.sequence = m_sequence;
        resolved.key = c;
        resolved.outcome = dispatch_outcome::more;
        resolved.m_ambiguous = (state.best.length > 0);
        // Do not reset() yet; there is more...
        return resolved;
    }

    // Reaching here means no longer binding is viable, or the host explicitly
    // resolved an ambiguity.  Materialize the globally longest fallback now;
    // doing this after the prefix decision prevents premature dispatch.  Save
    // everything after the consumed prefix for dispatch() to replay, then
    // reset the resolver for the next logical input sequence.
    if (state.best.length)
    {
        resolved_binding resolved(m_state);
        resolved.sequence = std::move(state.best.sequence);
        resolved.key = state.best.key;
        resolved.binding_target = state.best.binding;
        resolved.dispatcher_target = state.best.dispatcher;
        resolved.params = std::move(state.best.params);
        resolved.outcome = state.best.self_insert ? dispatch_outcome::self_insert : dispatch_outcome::match;
        if (state.best.length < input_length)
            resolved.m_replay.set(input + state.best.length, input_length - state.best.length);
        reset();
        return resolved;
    }

    if (m_sequence.length() > 1 && (c & 0xc0) != 0x80)
    {
        // Discard the sequence before c and try again.
        reset();
        m_sequence.set(reinterpret_cast<const char*>(&c), 1);
        // This is not a loop: the sequence is now only c and length 1, so the
        // retry can't reach here again.
#ifdef DEBUG
        assert(!retried_sequence);
        retried_sequence = true;
#endif
        force = false;
        goto retry_sequence;
    }

    resolved_binding resolved;
    resolved.sequence = m_sequence;
    resolved.key = c;
    resolved.outcome = dispatch_outcome::miss;
    reset();
    return resolved;
}

bool is_self_insertable(char c)
{
    return (c < 0 || c >= ' ') && !(uint8_t(c) == c_input_terminal_eof || uint8_t(c) == c_input_terminal_resize);
}

bool is_self_insertable(int32_t key)
{
    return (key >= ' ' && key <= 0xff && key != c_input_terminal_eof && key != c_input_terminal_resize);
}

} // namespace tib
