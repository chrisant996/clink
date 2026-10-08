// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

#include "tib_base.h"

#include <memory>
#include <vector>
#include <assert.h>

namespace tib {

class editor_context;

enum class binding_type : uint8_t
{
    none,
    func,
    macro,
    quoted_insert,
    lowercase_version,  // Resolve the same-table binding with a lowercase final byte.
};

class key_table;

typedef std::vector<cstring> binding_params;

class binding_target
{
    friend key_table;

public:
                        ~binding_target() noexcept = default;
                        binding_target() = default;
                        binding_target(binding_type type, const char* text, size_t len=c_auto_length) noexcept;
                        binding_target(const binding_target& t) noexcept = default;
                        binding_target(binding_target&& t) noexcept = default;
                        operator bool() const { return m_type != binding_type::none; }
    binding_target&     operator=(const binding_target& t) noexcept = default;
    binding_target&     operator=(binding_target&& t) noexcept = default;
    bool                operator==(const binding_target& t) const noexcept;
    bool                is_func_name(const char* name) const noexcept;

    binding_type        get_type() const noexcept { return m_type; }
    const char*         get_text() const noexcept { return m_text.c_str(); }
    size_t              get_length() const noexcept { assert(m_type == binding_type::macro); return m_text.length(); }
    char                get_char() const noexcept { assert(m_type == binding_type::quoted_insert); return *m_text.c_str(); }

    void                clear() noexcept;
    void                set_func(const char* name) noexcept;
    void                set_macro(const char* text, size_t len=c_auto_length) noexcept;
    void                set_quoted_insert(char c) noexcept;
    void                set_lowercase_version() noexcept;

protected:
    binding_type        m_type = binding_type::none;
    cstring             m_text;
};

binding_target binding_target_func(const char* name);
binding_target binding_target_macro(const char* text, size_t len=c_auto_length);
binding_target binding_target_quoted_insert(char c);
binding_target binding_target_lowercase_version();

struct key_binding
{
                        // No explicit dtor, otherwise it defeats r-value move.
                        //~key_binding() = default;

    cstring             sequence;
    binding_target      target;
    bool                pattern = false;

    // Syntax for pattern bindings is:
    //
    //  %%              Matches literal '%' character.
    //  %#              Matches one or more digits, which are captured into
    //                  the params member of resolved_binding.
    //  %!              Matches one default-encoded mouse byte.  Bytes in the
    //                  range 0x20..0xff are decoded by subtracting 0x20; NUL
    //                  is decoded as zero.  The resulting value is captured
    //                  into the params member of resolved_binding.
    //  anything else   Matches itself.
    //
    // Pattern bindings are mainly intended for matching mouse input.
};

class key_table : public std::enable_shared_from_this<key_table>
{
    friend class binding_resolver;

public:
                        ~key_table();
                        key_table(int8_t can=-1) noexcept : m_can_self_insert(can) {}

    // TODO-FUTURE: Need some way to troubleshoot messed up bindings.
    bool                add(key_binding&& binding);
    bool                add(const char* sequence, const binding_target& target, bool pattern=false);
    bool                add(const char* sequence, size_t len, const binding_target& target, bool pattern=false);
    bool                remove(const cstring& sequence, bool pattern=false);
    void                clear();

    int8_t              can_self_insert() const noexcept { return m_can_self_insert; }
    void                set_can_self_insert(int8_t can=true) noexcept { m_can_self_insert = can; }

    auto                begin() const noexcept { return m_bindings.cbegin(); }
    auto                end() const noexcept { return m_bindings.cend(); }
    auto                cbegin() const noexcept { return m_bindings.cbegin(); }
    auto                cend() const noexcept { return m_bindings.cend(); }

private:
    static int          sort_predicate(const key_binding& candidate, const key_binding& binding);

private:
    std::vector<key_binding> m_bindings;
    int8_t              m_can_self_insert = -1;
};

class key_table_list : public std::vector<std::shared_ptr<key_table>>, public std::enable_shared_from_this<key_table_list>
{
};

enum class dispatch_outcome
{
    miss,               // Input sequence did not resolve to a binding.
    self_insert,        // Input sequence is literal text.
    quoted_insert,      // Input byte is to be delivered as a quoted insert.
    more,               // Input sequence is a prefix of one or more bindings.
    match,              // Input sequence resolved to a binding.
    expired,            // The dispatcher_target weak reference was expired.
};

constexpr int32_t c_dispatch_request_quoted_insert = int32_max;

struct binding_resolver_state;

class dispatcher_target : public std::enable_shared_from_this<dispatcher_target>
{
public:
    // The fallback_requested argument allows probing potential fallback
    // bindings after a miss in override bindings.
    std::shared_ptr<const key_table_list> get_bindings(bool fallback_requested=false) const;
    void                set_bindings(std::shared_ptr<const key_table_list> bindings);
    void                override_bindings(std::shared_ptr<const key_table_list> bindings, bool fall_back_on_miss=false);

    // The binding_resolver::step() produces a resolved_binding in three cases:
    //
    //  1.  The input sequence matched a key binding.
    //  2.  A key_table has self-insert enabled and the input sequence is a
    //      single self-insert character.
    //  3.  The input is literal, i.e. a quoted insert.
    //
    // Returning negative means the input sequence was not handled, and
    // implies permission for something else to choose to handle the input
    // sequence.
    // Returning c_dispatch_request_quoted_insert asks the binding_resolver to
    // send the next input byte back to this dispatcher_target as a quoted
    // insert i.e. as literal input not translated through key bindings.
    virtual int32_t     dispatch(const cstring& sequence, int32_t key, const binding_target* binding, const binding_params* params) noexcept = 0;

    // Called by resolved_binding::dispatch() when resolution used the
    // fallback bindings after the override bindings missed.
    virtual void        on_binding_fallback() noexcept {}

private:
    std::shared_ptr<const key_table_list> m_bindings;
    std::shared_ptr<const key_table_list> m_override_bindings;
    bool                m_fall_back_on_miss = false;
};

struct resolved_binding
{
                        resolved_binding() = default;
                        resolved_binding(std::shared_ptr<binding_resolver_state> state);
                        operator bool();
    bool                more() const { return outcome == dispatch_outcome::more; }
                        // True when the pending sequence contains a complete
                        // fallback and remains a prefix of a longer binding.
    bool                ambiguous() const { return more() && m_ambiguous; }
    bool                is_func_name(const char* name) const;
    bool                dispatch();

    cstring             sequence;
    int32_t             key = 0;
    binding_target      binding_target;
    std::weak_ptr<dispatcher_target> dispatcher_target;
    dispatch_outcome    outcome = dispatch_outcome::miss;
    binding_params      params;

private:
    friend class binding_resolver;
    enum class probe_result { continue_input, recognized, unrecognized };

    probe_result        get_probe_result() const;

private:
    std::shared_ptr<binding_resolver_state> m_resolver_state;
    cstring             m_replay;
    bool                m_ambiguous = false;
    std::vector<std::weak_ptr<tib::dispatcher_target>> m_fallback_targets;
};

class binding_resolver
{
public:
                        ~binding_resolver() = default;
                        binding_resolver();

    void                clear_targets();
    void                add_target(std::weak_ptr<dispatcher_target> target);

    void                reset();
    resolved_binding    step(uint8_t c);
                        // Commit the longest complete binding in the pending
                        // sequence, normally after an ambiguity timeout.
    resolved_binding    resolve_pending();
    bool                quoted_insert_pending() const;
    bool                pending() const { return !m_sequence.empty(); }

                        // This is for checking whether additional input will
                        // trigger a miss, which is the primitive needed for
                        // an incremental "is bound" operation.
    bool                is_keyseq_recognized(const char* sequence, size_t len) const;

private:
    resolved_binding    resolve(bool force);

    std::vector<std::weak_ptr<dispatcher_target>> m_registrants;
    cstring             m_sequence;
    std::shared_ptr<binding_resolver_state> m_state;
    std::vector<std::weak_ptr<dispatcher_target>> m_fallback_targets;
};

bool is_self_insertable(char c);
bool is_self_insertable(int32_t key);

} // namespace tib
