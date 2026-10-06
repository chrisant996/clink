// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "terminal.h"
#include "terminal_in.h"
#include "terminal_out.h"
#include "terminal_helpers.h"
#include "ecma48_terminal_out.h"
#include "win_screen_buffer.h"
#include "win_terminal_in.h"

#include <core/log.h>
#include <core/os.h>
#include <core/settings.h>
#include <core/callstack.h>

extern "C" void rl_ding(void);

extern setting_bool g_debug_log_terminal;

uint32 g_ambiguous_keyseq_timeout = 500;

#ifdef _MSC_VER
extern setting_bool g_debug_log_output_callstacks;
#endif

//------------------------------------------------------------------------------

namespace
{

class input_adapter : public tib::terminal_in
{
public:
    explicit input_adapter(tib_terminal_bridge& bridge) : m_bridge(bridge) {}
    int32_t read() noexcept override { return m_bridge.read(); }
    bool avail(uint32_t timeout) noexcept override { return m_bridge.available(timeout); }
    bool peek(int32_t& value) noexcept override
    {
        value = m_bridge.peek();
        return (value != ::terminal_in::input_none);
    }
private:
    tib_terminal_bridge& m_bridge;
};

class output_adapter : public tib::terminal_out
{
public:
    explicit output_adapter(tib_terminal_bridge& bridge) : m_bridge(bridge) {}
    void write(const char* text, size_t len) noexcept override { m_bridge.write(text, len); }
    void ding() noexcept override { m_bridge.ding(); }
private:
    tib_terminal_bridge& m_bridge;
};

} // namespace

//------------------------------------------------------------------------------

static bool get_cursor_visibility_mode()
{
#ifdef DEBUG
    // For this to take effect, it must be set before host is instantiated, so
    // before `clink inject`.
    str<16> value;
    return !(os::get_env("DEBUG_NO_HIDE_CURSOR", value) && atoi(value.c_str()) != 0);
#else
    return true;
#endif
}

bool init_terminal()
{
    return tib_terminal_bridge::init_terminal();
}

bool init_terminal(bool cursor_visibility)
{
    return tib_terminal_bridge::init_terminal(cursor_visibility);
}

bool init_terminal(terminal_in* in, terminal_out* out)
{
    return tib_terminal_bridge::init_terminal(in, out);
}

bool uninit_terminal()
{
    const bool was_init = !!tib_terminal_bridge::get();
    assert(tib_terminal_bridge::get());
    delete tib_terminal_bridge::get();
    assert(!tib_terminal_bridge::get());
    return was_init;
}

//------------------------------------------------------------------------------

bool tib_terminal_bridge::init_terminal()
{
    assert(!get());
    if (get())
        return false;

    new tib_terminal_bridge();
    return true;
}

bool tib_terminal_bridge::init_terminal(bool cursor_visibility)
{
    assert(!get());
    if (get())
        return false;

    new tib_terminal_bridge(cursor_visibility);
    return true;
}

bool tib_terminal_bridge::init_terminal(terminal_in* in, terminal_out* out)
{
    assert(!get());
    if (get())
        return false;

    new tib_terminal_bridge(in, out);
    return true;
}

tib_terminal_bridge::tib_terminal_bridge()
{
    init(get_cursor_visibility_mode());
}

tib_terminal_bridge::tib_terminal_bridge(bool cursor_visibility)
{
    init(cursor_visibility);
}

tib_terminal_bridge::tib_terminal_bridge(terminal_in* in, terminal_out* out)
{
    m_in = in;
    m_out = out;
}

tib_terminal_bridge::~tib_terminal_bridge()
{
    if (m_old_out)
        redirect(nullptr);

    while (m_began > 0)
        end();

    if (m_inout_owned)
    {
        delete m_out;
        delete m_in;
    }

    if (m_screen_owned)
    {
        delete m_screen;
    }
}

void tib_terminal_bridge::init(bool cursor_visibility)
{
    assert(!m_screen);
    assert(!m_in);
    assert(!m_out);

    m_screen = new win_screen_buffer();
    m_screen_owned = true;

    m_in = new win_terminal_in(cursor_visibility);
    m_out = new ecma48_terminal_out(*m_screen);
    m_inout_owned = true;
}

void tib_terminal_bridge::begin(bool can_hide_cursor)
{
    assert(!m_began == !g_terminal);
    assert(!m_old_out); // Because begin/end won't apply correctly.

    if (!m_began)
    {
        g_terminal = this;

        m_old_input_hook = tib::hook_new_terminal_in;
        m_old_output_hook = tib::hook_new_terminal_out;
        tib::hook_new_terminal_in = [](tib::pushed_input&) -> tib::terminal_in* {
            return new input_adapter(*g_terminal);
        };
        tib::hook_new_terminal_out = []() -> tib::terminal_out* {
            return new output_adapter(*g_terminal);
        };

        m_lookahead = terminal_in::input_none;
        reset_bindings();
        tib::term_clear_input();
    }

    if (m_in)
        m_in->begin(can_hide_cursor);
    if (m_out)
        m_out->begin();

    if (!m_began)
    {
        tib::term_begin();
    }

    ++m_began;
}

void tib_terminal_bridge::end(bool can_show_cursor)
{
    assert(!m_old_out); // Because begin/end won't apply correctly.

    if (!m_began)
        return;

    --m_began;

    if (!m_began)
    {
        tib::term_end();
    }

    if (m_out)
        m_out->end();
    if (m_in)
        m_in->end(can_show_cursor);

    if (!m_began)
    {
        reset_bindings();
        set_chord(nullptr, 0);
        m_lookahead = terminal_in::input_none;

        tib::hook_new_terminal_in = m_old_input_hook;
        tib::hook_new_terminal_out = m_old_output_hook;

        g_terminal = nullptr;
    }
}

void tib_terminal_bridge::redirect(terminal_out* redirect)
{
    if (!m_old_out && redirect)
    {
        m_old_out = m_out;
        m_out = redirect;
    }
    else if (m_old_out && !redirect)
    {
        m_out = m_old_out;
        m_old_out = nullptr;
    }
}

void tib_terminal_bridge::set_chord(const char* keys, uint32 len)
{
    assert(m_in);
    if (!m_in)
        return;

// TODO-TIB: this borrows the keys pointer; is that safe?
    m_chord = keys;
    m_chord_len = len;
}

int32 tib_terminal_bridge::read()
{
    if (!m_in)
        return terminal_in::input_none;

    // Bytes already delivered by Clink's resolver precede new driver input.
    if (m_chord_len)
    {
        --m_chord_len;
        return uint8(*m_chord++);
    }
    if (m_lookahead != terminal_in::input_none)
    {
        const int32 value = m_lookahead;
        m_lookahead = terminal_in::input_none;
        return value;
    }
    return m_in->read();
}

int32 tib_terminal_bridge::peek()
{
    if (!m_in)
        return terminal_in::input_none;

    if (m_chord_len)
        return uint8(*m_chord);

    // Cache the signed result, including resize/abort/exit.  Clink's read()
    // checks dimensions, whereas its peek() does not.  Keeping the read here
    // makes peek/read agree without losing an event to tib's byte pushback.
    if (m_lookahead == terminal_in::input_none)
        m_lookahead = m_in->read();
    return m_lookahead;
}

bool tib_terminal_bridge::available(uint32 timeout)
{
    if (!m_in)
        return false;

    return (m_chord_len ||
            m_lookahead != terminal_in::input_none ||
            m_in->available(timeout));
}

void tib_terminal_bridge::write(const char* text, size_t len)
{
    if (!m_out)
        return;

    if (g_debug_log_terminal.get() &&
        !suppress_implicit_write_console_logging::is_suppressed())
    {
        LOGCURSORPOS(GetStdHandle(STD_OUTPUT_HANDLE));
        const char* ctx = terminal_fwrite_context::get_context();
        LOG("%s \"%.*s\", %d", ctx ? ctx : "RL_OUTSTREAM", int32(len), text, int32(len));
#ifdef _MSC_VER
        if (g_debug_log_output_callstacks.get())
        {
            char stk[8192];
            format_callstack(2, 20, stk, sizeof(stk), false);
            LOG("%s", stk);
        }
#endif
    }

    suppress_implicit_write_console_logging no_log;

    m_out->write(text, int32(len));
    m_out->flush();
}

void tib_terminal_bridge::write(const char* text)
{
    write(text, strlen(text));
}

void tib_terminal_bridge::ding()
{
    rl_ding();
}

int32 tib_terminal_bridge::get_columns() const
{
    if (!m_out)
        return 80;
    return m_out->get_columns();
}

int32 tib_terminal_bridge::get_rows() const
{
    if (!m_out)
        return 25;
    return m_out->get_rows();
}

bool tib_terminal_bridge::wait_for_input(input_idle* idle)
{
    assert(m_began);

    // Must go through tib::term_in_avail because of pushed input.
// TODO-TIB: is this correctly integrated with m_chord_len?
    if (tib::term_in_avail(0))
        return false;

    if (m_ambiguous)
    {
        // Timeout and fallback dispatch apply to every registered tib target.
        // Pure prefixes (without a complete fallback) continue waiting.
        m_in->select(idle, g_ambiguous_keyseq_timeout);
        if (!tib::term_in_avail(0))
        {
            auto resolved = m_resolver.resolve_pending();
            m_ambiguous = resolved.ambiguous();
            return resolved.dispatch();
        }
    }
    else
        m_in->select(idle);
    return false;
}

bool tib_terminal_bridge::get_cursor_pos(int16& x, int16& y) const
{
    assert(m_out);
    if (!m_out)
        return false;
    return m_out->get_cursor_pos(x, y);
}

void tib_terminal_bridge::add_target(std::weak_ptr<tib::dispatcher_target> target)
{
    m_resolver.add_target(target);
    m_ambiguous = false;
}

void tib_terminal_bridge::reset_bindings()
{
    m_resolver.reset();
    m_ambiguous = false;
}

bool tib_terminal_bridge::is_bound(const char* seq, int32 len)
{
#ifdef TIB_TODO
    if (!len)
    {
LNope:
        if (RL_ISSTATE (RL_STATE_MULTIKEY))
        {
            RL_UNSETSTATE(RL_STATE_MULTIKEY);
            _rl_keyseq_chain_dispose();
        }
        rl_ding();
        return false;
    }

    // `quoted-insert` must accept all input (that's its whole purpose).
    if (rl_is_insert_next_callback_pending())
        return true;

    // The F2, F4, and F9 console compatibility implementations can accept
    // input, but extended keys are meaningless so don't accept them.  The
    // intent is to allow printable textual input, control characters, and ESC.
    if (win_fn_callback_pending())
    {
        const char* bindableEsc = get_bindable_esc();
        if (bindableEsc && strcmp(seq, bindableEsc) == 0)
            return true;
        if (len > 1 && seq[0] == '\x1b')
            goto LNope;
        return true;
    }

    // Various states should only accept "simple" input, i.e. not CSI sequences,
    // so that unrecognized portions of key sequences don't bleed in as textual
    // input.
    if (RL_ISSTATE(RL_SIMPLE_INPUT_STATES))
    {
        if (seq[0] == '\x1b')
            goto LNope;
        return true;
    }

    // The intent here is to accept all UTF8 input (not sure why readline
    // reports them as not bound, but this seems good enough for now).
    if (len > 1 && uint8(seq[0]) >= ' ')
        return true;

    // NOTE:  Checking readline's keymap is incorrect when a special bind group
    // is active that should block on_input from reaching readline.  But the way
    // that blocking is achieved is by adding a "" binding that matches
    // everything not explicitly bound in the keymap.  So it works out
    // naturally, without additional effort.

    // Using nullptr for the keymap starts from the root of the current keymap,
    // but in a multi key sequence this needs to use the current dispatching
    // node of the current keymap.
    Keymap keymap = RL_ISSTATE (RL_STATE_MULTIKEY) ? _rl_dispatching_keymap : nullptr;
    if (rl_function_of_keyseq_len(seq, len, keymap, nullptr))
        return true;

    goto LNope;
#endif

    if (len > 0 && m_resolver.accepts(seq, size_t(len)))
        return true;
    reset_bindings();
    tib::ding();
    return false;
}

bool tib_terminal_bridge::pending_input() const
{
    return m_resolver.pending() || quoted_insert_pending();
}

bool tib_terminal_bridge::quoted_insert_pending() const
{
    return m_resolver.quoted_insert_pending();
}

void tib_terminal_bridge::dispatch(uint8 key)
{
    auto resolved = m_resolver.step(key);
    m_ambiguous = resolved.ambiguous();
    resolved.dispatch();
}
