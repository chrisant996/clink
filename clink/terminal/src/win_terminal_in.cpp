// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "win_terminal_in.h"
#include "scroll.h"
#include "input_idle.h"
#include "key_tester.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/settings.h>

#include <Windows.h>
#include <assert.h>
#include <map>

//------------------------------------------------------------------------------
static setting_bool g_differentiate_keys(
    "terminal.differentiate_keys",
    "Use special sequences for Ctrl-H, -I, -M, -[",
    "When enabled, pressing Ctrl-H or Ctrl-I or Ctrl-M or Ctrl-[ generate special\n"
    "key sequences to enable binding them separately from Backspace or Tab or\n"
    "Enter or Escape.",
    false);

static setting_bool g_use_altgr_substitute(
    "terminal.use_altgr_substitute",
    "Support Windows' Ctrl-Alt substitute for AltGr",
    "Windows provides Ctrl-Alt as a substitute for AltGr, historically to\n"
    "support keyboards with no AltGr key.  This may collide with some of\n"
    "Readline's bindings.",
    false);

extern setting_bool g_adjust_cursor_style;

//------------------------------------------------------------------------------
extern "C" void reset_wcwidths();

//------------------------------------------------------------------------------
static const int CTRL_PRESSED = LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED;
static const int ALT_PRESSED = LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED;

// TODO: 0.4.8 keyboard compatibility mode
#define CSI(x) "\x1b[" #x
#define SS3(x) "\x1bO" #x
#define ACSI(x) "\x1b\x1b[" #x
#define ASS3(x) "\x1b\x1bO" #x
#define MOK(x) "\x1b[27;" #x
namespace terminfo { //                       Shf        Ctl        CtlShf     Alt        AtlShf     AltCtl     AltCtlShf
static const char* const kcuu1[] = { CSI(A),  CSI(1;2A), CSI(1;5A), CSI(1;6A), CSI(1;3A), CSI(1;4A), CSI(1;7A), CSI(1;8A) }; // up
static const char* const kcud1[] = { CSI(B),  CSI(1;2B), CSI(1;5B), CSI(1;6B), CSI(1;3B), CSI(1;4B), CSI(1;7B), CSI(1;8B) }; // down
static const char* const kcub1[] = { CSI(D),  CSI(1;2D), CSI(1;5D), CSI(1;6D), CSI(1;3D), CSI(1;4D), CSI(1;7D), CSI(1;8D) }; // left
static const char* const kcuf1[] = { CSI(C),  CSI(1;2C), CSI(1;5C), CSI(1;6C), CSI(1;3C), CSI(1;4C), CSI(1;7C), CSI(1;8C) }; // right
static const char* const kich1[] = { CSI(2~), CSI(2;2~), CSI(2;5~), CSI(2;6~), CSI(2;3~), CSI(2;4~), CSI(2;7~), CSI(2;8~) }; // insert
static const char* const kdch1[] = { CSI(3~), CSI(3;2~), CSI(3;5~), CSI(3;6~), CSI(3;3~), CSI(3;4~), CSI(3;7~), CSI(3;8~) }; // delete
static const char* const khome[] = { CSI(H),  CSI(1;2H), CSI(1;5H), CSI(1;6H), CSI(1;3H), CSI(1;4H), CSI(1;7H), CSI(1;8H) }; // home
static const char* const kend[]  = { CSI(F),  CSI(1;2F), CSI(1;5F), CSI(1;6F), CSI(1;3F), CSI(1;4F), CSI(1;7F), CSI(1;8F) }; // end
static const char* const kpp[]   = { CSI(5~), CSI(5;2~), CSI(5;5~), CSI(5;6~), CSI(5;3~), CSI(5;4~), CSI(5;7~), CSI(5;8~) }; // pgup
static const char* const knp[]   = { CSI(6~), CSI(6;2~), CSI(6;5~), CSI(6;6~), CSI(6;3~), CSI(6;4~), CSI(6;7~), CSI(6;8~) }; // pgdn
static const char* const kbks[]  = { "\b",    MOK(2;8~), "\x7f",    MOK(6;8~), "\x1b\b",  MOK(4;8~), "\x1b\x7f",MOK(8;8~) }; // bkspc
static const char* const kcbt    = CSI(Z);
static const char* const kfx[]   = {
    // kf1-12 : Fx unmodified
    SS3(P),     SS3(Q),     SS3(R),     SS3(S),
    CSI(15~),   CSI(17~),   CSI(18~),   CSI(19~),
    CSI(20~),   CSI(21~),   CSI(23~),   CSI(24~),

    // kf13-24 : shift
    CSI(1;2P),  CSI(1;2Q),  CSI(1;2R),  CSI(1;2S),
    CSI(15;2~), CSI(17;2~), CSI(18;2~), CSI(19;2~),
    CSI(20;2~), CSI(21;2~), CSI(23;2~), CSI(24;2~),

    // kf25-36 : ctrl
    CSI(1;5P),  CSI(1;5Q),  CSI(1;5R),  CSI(1;5S),
    CSI(15;5~), CSI(17;5~), CSI(18;5~), CSI(19;5~),
    CSI(20;5~), CSI(21;5~), CSI(23;5~), CSI(24;5~),

    // kf37-48 : ctrl-shift
    CSI(1;6P),  CSI(1;6Q),  CSI(1;6R),  CSI(1;6S),
    CSI(15;6~), CSI(17;6~), CSI(18;6~), CSI(19;6~),
    CSI(20;6~), CSI(21;6~), CSI(23;6~), CSI(24;6~),

    // kf1-12 : alt
    ASS3(P),     ASS3(Q),     ASS3(R),     ASS3(S),
    ACSI(15~),   ACSI(17~),   ACSI(18~),   ACSI(19~),
    ACSI(20~),   ACSI(21~),   ACSI(23~),   ACSI(24~),

    // kf13-24 : alt-shift
    ACSI(1;2P),  ACSI(1;2Q),  ACSI(1;2R),  ACSI(1;2S),
    ACSI(15;2~), ACSI(17;2~), ACSI(18;2~), ACSI(19;2~),
    ACSI(20;2~), ACSI(21;2~), ACSI(23;2~), ACSI(24;2~),

    // kf25-36 : alt-ctrl
    ACSI(1;5P),  ACSI(1;5Q),  ACSI(1;5R),  ACSI(1;5S),
    ACSI(15;5~), ACSI(17;5~), ACSI(18;5~), ACSI(19;5~),
    ACSI(20;5~), ACSI(21;5~), ACSI(23;5~), ACSI(24;5~),

    // kf37-48 : alt-ctrl-shift
    ACSI(1;6P),  ACSI(1;6Q),  ACSI(1;6R),  ACSI(1;6S),
    ACSI(15;6~), ACSI(17;6~), ACSI(18;6~), ACSI(19;6~),
    ACSI(20;6~), ACSI(21;6~), ACSI(23;6~), ACSI(24;6~),
};

//                                            Shf     Ctl         CtlShf      Alt   AtlShf   AltCtl      AltCtlShf
static const char* const ktab[]  = { "\t",    CSI(Z), MOK(5;9~),  MOK(6;9~),  "",   "",      "",         ""         }; // TAB
static const char* const kspc[]  = { " ",     " ",    MOK(5;32~), MOK(6;32~), "",   "",      MOK(7;32~), MOK(8;32~) }; // SPC

static int xterm_modifier(int key_flags)
{
    // Calculate Xterm's modifier number.
    int i = 0;
    i |= !!(key_flags & SHIFT_PRESSED);
    i |= !!(key_flags & ALT_PRESSED) << 1;
    i |= !!(key_flags & CTRL_PRESSED) << 2;
    return i + 1;
}

static int keymod_index(int key_flags)
{
    // Calculate key sequence table modifier index.
    int i = 0;
    i |= !!(key_flags & SHIFT_PRESSED);
    i |= !!(key_flags & CTRL_PRESSED) << 1;
    i |= !!(key_flags & ALT_PRESSED) << 2;
    return i;
}

static bool is_vk_recognized(int key_vk)
{
    switch (key_vk)
    {
    case 'A':   case 'B':   case 'C':   case 'D':
    case 'E':   case 'F':   case 'G':   case 'H':
    case 'I':   case 'J':   case 'K':   case 'L':
    case 'M':   case 'N':   case 'O':   case 'P':
    case 'Q':   case 'R':   case 'S':   case 'T':
    case 'U':   case 'V':   case 'W':   case 'X':
    case 'Y':   case 'Z':
        return true;
    case '0':   case '1':   case '2':   case '3':
    case '4':   case '5':   case '6':   case '7':
    case '8':   case '9':
        return true;
    case VK_OEM_1:              // ';:' for US
    case VK_OEM_PLUS:           // '+' for any country
    case VK_OEM_COMMA:          // ',' for any country
    case VK_OEM_MINUS:          // '-' for any country
    case VK_OEM_PERIOD:         // '.' for any country
    case VK_OEM_2:              // '/?' for US
    case VK_OEM_3:              // '`~' for US
    case VK_OEM_4:              // '[{' for US
    case VK_OEM_5:              // '\|' for US
    case VK_OEM_6:              // ']}' for US
    case VK_OEM_7:              // ''"' for US
        return true;
    default:
        return false;
    }
}

} // namespace terminfo
#undef SS3
#undef CSI

//------------------------------------------------------------------------------
// Use unsigned; WCHAR and unsigned short can give wrong results.
#define IN_RANGE(n1, b, n2)     ((unsigned)((b) - (n1)) <= unsigned((n2) - (n1)))
inline bool is_lead_surrogate(unsigned int ch) { return IN_RANGE(0xD800, ch, 0xDBFF); }



//------------------------------------------------------------------------------
static bool s_verbose_input = false;
void set_verbose_input(bool verbose)
{
    s_verbose_input = verbose;
}



//------------------------------------------------------------------------------
struct keyseq_name : public no_copy
{
    keyseq_name(char* p, short int eqclass, short int order) { s = p; eq = eqclass; o = order; }
    keyseq_name(keyseq_name&& a) { s = a.s; eq = a.eq; o = a.o; a.s = nullptr; }
    ~keyseq_name() { free(s); }
    keyseq_name& operator=(keyseq_name&& a) { s = a.s; eq = a.eq; o = a.o; a.s = nullptr; }

    char* s;
    short int eq;
    short int o;
};

//------------------------------------------------------------------------------
struct keyseq_key : public no_copy
{
    keyseq_key(const char* p, bool find = false) { this->s = p; this->find = find; }
    keyseq_key(keyseq_key&& a) { s = a.s; find = a.find; a.s = nullptr; }
    keyseq_key& operator=(keyseq_key&& a) { s = a.s; find = a.find; a.s = nullptr; }

    const char* s;
    bool find;
};

//------------------------------------------------------------------------------
struct map_cmp_str
{
    bool operator()(keyseq_key const& a, keyseq_key const& b) const
    {
        if (a.find)
        {
            assert(!b.find);
            const char* bs = b.s;
            for (const char* as = a.s; *as; as++, bs++)
            {
                int cmp = int((unsigned char)*as) - int((unsigned char)*bs);
                if (cmp)
                    return cmp < 0;
            }
        }
        else if (b.find)
        {
            assert(!a.find);
            const char* as = a.s;
            for (const char* bs = b.s; *bs; as++, bs++)
            {
                int cmp = int((unsigned char)*as) - int((unsigned char)*bs);
                if (cmp)
                    return cmp < 0;
            }
        }
        else
        {
            const char* as = a.s;
            const char* bs = b.s;
            while (true)
            {
                int cmp = int((unsigned char)*as) - int((unsigned char)*bs);
                if (cmp || !*as)
                    return cmp < 0;
                as++;
                bs++;
            }
        }
        return false;
    }
};
static std::map<keyseq_key, keyseq_name, map_cmp_str> map_keyseq_to_name;

//------------------------------------------------------------------------------
static void add_keyseq_to_name(const char* keyseq, const char* name, str<32>& builder, short int modifier)
{
    if (!keyseq || !*keyseq)
        return;

    int old_len = builder.length();
    builder.concat(name);

    int alloc = builder.length() + 1;
    keyseq_name second((char*)malloc(alloc), modifier, (short int)map_keyseq_to_name.size());
    if (second.s)
    {
        memcpy(second.s, builder.c_str(), alloc);
        keyseq_key first(keyseq);
        map_keyseq_to_name.emplace(std::move(first), std::move(second));
    }

    builder.truncate(old_len);
}

//------------------------------------------------------------------------------
static void ensure_keyseqs_to_names()
{
    if (!map_keyseq_to_name.empty())
        return;

    static const char* const mods[] = { "", "S-", "C-", "C-S-", "A-", "A-S-", "A-C-", "A-C-S-" };
    static_assert(sizeof_array(mods) == sizeof_array(terminfo::kcuu1), "modifier name count must match modified key array sizes");

    str<32> builder;

    add_keyseq_to_name(bindableEsc, "Esc", builder, 0);

    for (int m = 0; m < sizeof_array(terminfo::kcuu1); m++)
    {
        builder = mods[m];
        add_keyseq_to_name(terminfo::kcuu1[m], "Up", builder, m);
        add_keyseq_to_name(terminfo::kcud1[m], "Down", builder, m);
        add_keyseq_to_name(terminfo::kcub1[m], "Left", builder, m);
        add_keyseq_to_name(terminfo::kcuf1[m], "Right", builder, m);
        add_keyseq_to_name(terminfo::khome[m], "Home", builder, m);
        add_keyseq_to_name(terminfo::kend[m], "End", builder, m);
        add_keyseq_to_name(terminfo::kpp[m], "PgUp", builder, m);
        add_keyseq_to_name(terminfo::knp[m], "PgDn", builder, m);
        add_keyseq_to_name(terminfo::kich1[m], "Ins", builder, m);
        add_keyseq_to_name(terminfo::kdch1[m], "Del", builder, m);
        add_keyseq_to_name(terminfo::ktab[m], "Tab", builder, m);
        add_keyseq_to_name(terminfo::kspc[m], "Space", builder, m);
        add_keyseq_to_name(terminfo::kbks[m], "Bkspc", builder, m);
    }

    str<32> fn;
    for (int i = 0; i < sizeof_array(terminfo::kfx); )
    {
        int m = i / 12;
        builder = mods[m];
        for (int j = 0; j < 12; j++, i++)
        {
            fn.format("F%u", j + 1);
            add_keyseq_to_name(terminfo::kfx[i], fn.c_str(), builder, m);
        }
    }
}

//------------------------------------------------------------------------------
void reset_keyseq_to_name_map()
{
    // Settings can affect key sequence processing, so being able to reset the
    // map enables show_rl_help to show accurate key names.
    map_keyseq_to_name.clear();
}

//------------------------------------------------------------------------------
static bool key_name_from_vk(int key_vk, str_base& out)
{
    UINT key_scan = MapVirtualKeyW(key_vk, MAPVK_VK_TO_VSC);
    if (key_scan)
    {
        LONG l = (key_scan & 0x01ff) << 16;
        wchar_t name[16];
        if (GetKeyNameTextW(l, name, sizeof_array(name)))
        {
            to_utf8(out, name);
            return true;
        }
    }

    return false;
}

//------------------------------------------------------------------------------
const char* find_key_name(const char* keyseq, int& len, int& eqclass, int& order)
{
    // '\x00' (Ctrl-@) isn't in the map, so rejecting a seemingly empty string
    // avoids needing to receive the keyseq length.
    if (!keyseq || !*keyseq)
        return nullptr;

    // Look up the sequence in the special key names map.
    ensure_keyseqs_to_names();
    keyseq_key lookup(keyseq, true/*find*/);
    auto const& iter = map_keyseq_to_name.find(lookup);
    if (iter != map_keyseq_to_name.end())
    {
        len = (int)strlen(iter->first.s);
        eqclass = iter->second.eq;
        order = iter->second.o - (int)map_keyseq_to_name.size();
        return iter->second.s;
    }

    // Try to deduce the name if it's an extended XTerm key sequence.
    if (keyseq[0] == 0x1b && keyseq[1] == '[' &&
        keyseq[2] == '2' && keyseq[3] == '7' && keyseq[4] == ';')
    {
        static char static_buffer[256];

        str_base out(static_buffer);
        out.clear();

        int i = 5;
        int mod = 0;
        if (keyseq[i] >= '2' && keyseq[i] <= '8' && keyseq[i+1] == ';')
        {
            mod = keyseq[i] - '0' - 1;
            i += 2;

            static const char* const c_mod_names[] =
            { "", "S-", "A-", "A-S-", "C-", "C-S-", "A-C-", "A-C-S-" };

            eqclass = ((!!(mod & 1) << 0) |
                       (!!(mod & 4) << 1) |
                       (!!(mod & 2) << 2));

            out << c_mod_names[mod];
        }

        int key_vk = 0;
        if (keyseq[i] >= '1' && keyseq[i] <= '9') // Leading '0' not allowed.
        {
            key_vk = keyseq[i++] - '0';
            while (keyseq[i] >= '0' && keyseq[i] <= '9')
            {
                key_vk *= 10;
                key_vk += keyseq[i++] - '0';
            }

            if (keyseq[i] == '~' && terminfo::is_vk_recognized(key_vk))
            {
                i++;
                len = i;
                if (key_name_from_vk(key_vk, out))
                    return out.c_str();
            }
        }
    }

    return nullptr;
}



//------------------------------------------------------------------------------
enum : unsigned char
{
    input_abort_byte    = 0xff,
    input_none_byte     = 0xfe,
    input_timeout_byte  = 0xfd,
};



//------------------------------------------------------------------------------
static unsigned int get_dimensions()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    auto cols = short(csbi.dwSize.X);
    auto rows = short(csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
    return (cols << 16) | rows;
}

//------------------------------------------------------------------------------
static void set_cursor_visibility(bool state)
{
    if (!g_adjust_cursor_style.get())
        return;

    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    GetConsoleCursorInfo(handle, &info);
    info.bVisible = BOOL(state);
    SetConsoleCursorInfo(handle, &info);
}

//------------------------------------------------------------------------------
static bool adjust_cursor_on_resize(COORD prev_position)
{
    // Windows will move the cursor onto a new line when it gets clipped on
    // buffer resize. Other terminals clamp along the X axis.

    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(handle , &csbi);
    if (prev_position.X < csbi.dwSize.X)
        return false;

    COORD fix_position = {
        short(csbi.dwSize.X - 1),
        short(csbi.dwCursorPosition.Y - 1)
    };
    SetConsoleCursorPosition(handle, fix_position);
    return true;
}



//------------------------------------------------------------------------------
void win_terminal_in::begin()
{
    m_buffer_count = 0;
    m_lead_surrogate = 0;
    m_stdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(m_stdin, &m_prev_mode);
    set_cursor_visibility(false);
}

//------------------------------------------------------------------------------
void win_terminal_in::end()
{
    set_cursor_visibility(true);
    SetConsoleMode(m_stdin, m_prev_mode);
    m_stdin = nullptr;
}

//------------------------------------------------------------------------------
void win_terminal_in::select(input_idle* callback)
{
    if (!m_buffer_count)
        read_console(callback);
}

//------------------------------------------------------------------------------
int win_terminal_in::read()
{
    unsigned int dimensions = get_dimensions();
    if (dimensions != m_dimensions)
    {
        m_dimensions = dimensions;
        return terminal_in::input_terminal_resize;
    }

    if (!m_buffer_count)
        return terminal_in::input_none;

    unsigned char c = pop();
    switch (c)
    {
    case input_none_byte:       return terminal_in::input_none;
    case input_timeout_byte:    return terminal_in::input_timeout;
    case input_abort_byte:      return terminal_in::input_abort;
    default:                    return c;
    }
}

//------------------------------------------------------------------------------
key_tester* win_terminal_in::set_key_tester(key_tester* keys)
{
    key_tester* ret = m_keys;
    m_keys = keys;
    return ret;
}

//------------------------------------------------------------------------------
void win_terminal_in::read_console(input_idle* callback)
{
    // Hide the cursor unless we're accepting input so we don't have to see it
    // jump around as the screen's drawn.
    struct cursor_scope {
        cursor_scope()  { set_cursor_visibility(true); }
        ~cursor_scope() { set_cursor_visibility(false); }
    } _cs;

    // Conhost restarts the cursor blink when writing to the console. It restarts
    // hidden which means that if you type faster than the blink the cursor turns
    // invisible. Fortunately, moving the cursor restarts the blink on visible.
    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(stdout_handle, &csbi);
    if (!is_scroll_mode())
        SetConsoleCursorPosition(stdout_handle, csbi.dwCursorPosition);

    // Read input records sent from the terminal (aka conhost) until some
    // input has been buffered.
    unsigned int buffer_count = m_buffer_count;
    while (buffer_count == m_buffer_count)
    {
        DWORD modeIn;
        if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &modeIn))
        {
            // If Lua code uses io.popen():lines() and returns without finishing
            // reading the output, this can be reached with the console mode set
            // wrong.  Compensate.
            assert(!(modeIn & ENABLE_PROCESSED_INPUT));
            if (modeIn & ENABLE_PROCESSED_INPUT)
                SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), modeIn & ~ENABLE_PROCESSED_INPUT);
        }

        while (callback && callback->is_enabled())
        {
            unsigned count = 1;
            HANDLE handles[2] = { m_stdin };

            std::shared_ptr<shared_event> event = callback->get_waitevent();
            if (event)
            {
                HANDLE actual_event = event->get_event();
                if (actual_event)
                    handles[count++] = actual_event;
            }

            DWORD timeout = callback->get_timeout();
            DWORD result = WaitForMultipleObjects(count, handles, false, timeout);
            if (result != WAIT_OBJECT_0 + 1 && result != WAIT_TIMEOUT)
                break;

            callback->on_idle();
        }

        DWORD count;
        INPUT_RECORD record;
        if (!ReadConsoleInputW(m_stdin, &record, 1, &count))
        {
            // Handle's probably invalid if ReadConsoleInput() failed.
            m_buffer_count = 1;
            m_buffer[0] = input_abort_byte;
            return;
        }

        switch (record.EventType)
        {
        case KEY_EVENT:
            {
                auto& key_event = record.Event.KeyEvent;

                // Some times conhost can send through ALT codes, with the
                // resulting Unicode code point in the Alt key-up event.
                if (!key_event.bKeyDown
                    && key_event.wVirtualKeyCode == VK_MENU
                    && key_event.uChar.UnicodeChar)
                {
                    key_event.bKeyDown = TRUE;
                    key_event.dwControlKeyState = 0;
                }

                if (key_event.bKeyDown)
                {
                    process_input(key_event);

                    // If the processed input chord isn't bound, discard it.
                    // Otherwise unbound keys can have the tail part of their
                    // sequence show up as though it were typed input.  The
                    // approach here assumes no more than one key sequence per
                    // input record.
                    if (m_keys)
                    {
                        // If there are unprocessed queued keys, then we don't
                        // know what keymap will be active when this new input
                        // gets processed, so we can't accurately tell whether
                        // the key sequence is bound to anything.
                        assert(buffer_count == 0);

                        const int len = m_buffer_count - buffer_count;
                        if (len > 0)
                        {
                            // m_buffer is circular, so copy the key sequence to
                            // a separate sequential buffer.
                            char chord[sizeof_array(m_buffer) + 1];
                            static const unsigned int mask = sizeof_array(m_buffer) - 1;
                            for (int i = 0; i < len; ++i)
                                chord[i] = m_buffer[(m_buffer_head + i) & mask];

                            // Readline has a bug in rl_function_of_keyseq_len
                            // that looks for nul termination even though it's
                            // supposed to use a length instead.
                            chord[len] = '\0';

                            str<32> new_chord;
                            if (m_keys->translate(chord, len, new_chord))
                            {
                                m_buffer_count = buffer_count;
                                for (unsigned int i = 0; i < new_chord.length(); ++i)
                                    push((unsigned int)new_chord.c_str()[i]);
                            }
                            else if (!m_keys->is_bound(chord, len))
                            {
                                m_buffer_count = buffer_count;
                            }

                            m_keys->set_keyseq_len(m_buffer_count);
                        }
                    }
                }
            }
            break;

        case WINDOW_BUFFER_SIZE_EVENT:
            // Windows will move the cursor onto a new line when it gets clipped
            // on buffer resize.
            reset_wcwidths();
            if (adjust_cursor_on_resize(csbi.dwCursorPosition))
                return;
            break;
        }
    }
}

//------------------------------------------------------------------------------
extern "C" int rl_editing_mode;
void win_terminal_in::process_input(KEY_EVENT_RECORD const& record)
{
    int key_char = record.uChar.UnicodeChar;
    int key_vk = record.wVirtualKeyCode;
    int key_sc = record.wVirtualScanCode;
    int key_flags = record.dwControlKeyState;

    // We filter out Alt key presses unless they generated a character.
    if (key_vk == VK_MENU)
    {
        if (key_char)
            push(key_char);

        return;
    }

    // Early out of unaccompanied ctrl/shift key presses.
    if (key_vk == VK_CONTROL || key_vk == VK_SHIFT)
        return;

    if (s_verbose_input)
    {
        char buf[32];
        buf[0] = 0;
        const char* key_name = key_name_from_vk(key_vk, str_base(buf)) ? buf : "UNKNOWN";
        printf("key event:  %c%c%c %c%c  flags=0x%08.8x  char=0x%04.4x  vk=0x%04.4x  \"%s\"\n",
                (key_flags & SHIFT_PRESSED) ? 'S' : '_',
                (key_flags & LEFT_CTRL_PRESSED) ? 'C' : '_',
                (key_flags & LEFT_ALT_PRESSED) ? 'A' : '_',
                (key_flags & RIGHT_ALT_PRESSED) ? 'A' : '_',
                (key_flags & RIGHT_CTRL_PRESSED) ? 'C' : '_',
                key_flags,
                key_char,
                key_vk,
                key_name);
    }

    // Special treatment for escape.
    if (key_char == 0x1b && (key_vk == VK_ESCAPE || !g_differentiate_keys.get()))
        return push(bindableEsc);

    // Windows supports an AltGr substitute which we check for here. As it
    // collides with Readline mappings Clink's support can be disabled.
    if (key_flags & LEFT_ALT_PRESSED)
    {
        bool altgr_sub = !!(key_flags & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED));
        altgr_sub &= !!key_char;

        if (altgr_sub && !g_use_altgr_substitute.get())
        {
            altgr_sub = false;
            key_char = 0;
        }

        if (!altgr_sub)
            key_flags &= ~(RIGHT_ALT_PRESSED);
        else
            key_flags &= ~(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED);
    }

    // Special treatment for variations of tab and space. Do this before
    // clearing AltGr flags, otherwise ctrl-space gets converted into space.
    if (key_vk == VK_TAB && (key_char == 0x09 || !key_char) && !m_buffer_count)
        return push(terminfo::ktab[terminfo::keymod_index(key_flags)]);
    if (key_vk == VK_SPACE && (key_char == 0x20 || !key_char) && !m_buffer_count)
        return push(terminfo::kspc[terminfo::keymod_index(key_flags)]);

    // If the input was formed using AltGr or LeftAlt-LeftCtrl then things get
    // tricky. But there's always a Ctrl bit set, even if the user didn't press
    // a ctrl key. We can use this and the knowledge that Ctrl-modified keys
    // aren't printable to clear appropriate AltGr flags.
    if ((key_char > 0x1f && key_char != 0x7f) && (key_flags & CTRL_PRESSED))
    {
        key_flags &= ~CTRL_PRESSED;
        if (key_flags & RIGHT_ALT_PRESSED)
            key_flags &= ~RIGHT_ALT_PRESSED;
        else
            key_flags &= ~LEFT_ALT_PRESSED;
    }

    // Special case for ctrl-shift-I (to behave like shift-tab aka. back-tab).
    if (key_char == '\t' && !m_buffer_count && (key_flags & SHIFT_PRESSED) && !g_differentiate_keys.get())
        return push(terminfo::kcbt);

    // Function keys (kf1-kf48 from xterm+pcf2)
    unsigned key_func = key_vk - VK_F1;
    if (key_func <= (VK_F12 - VK_F1))
    {
        int kfx_group = terminfo::keymod_index(key_flags);
        push((terminfo::kfx + (12 * kfx_group) + key_func)[0]);
        return;
    }

    // Include an ESC character in the input stream if Alt is pressed.
    if (key_char)
    {
        bool simple_char;

        assert(key_vk != VK_TAB);
        if (key_vk == 'H' || key_vk == 'I')
            simple_char = !(key_flags & CTRL_PRESSED) || !g_differentiate_keys.get();
        else if (key_vk == 'M')
            simple_char = !((key_flags & CTRL_PRESSED)) || !g_differentiate_keys.get();
        else if (key_char == 0x1b && key_vk != VK_ESCAPE)
            simple_char = terminfo::keymod_index(key_flags) < 2; // Modifiers were resulting in incomplete escape codes.
        else if (key_vk == VK_RETURN || key_vk == VK_BACK)
            simple_char = !(key_flags & (CTRL_PRESSED|SHIFT_PRESSED));
        else
            simple_char = !(key_flags & CTRL_PRESSED) || !(key_flags & SHIFT_PRESSED);

        if (simple_char)
        {
            if (key_flags & ALT_PRESSED)
                push(0x1b);
            return push(key_char);
        }
    }

    // The numpad keys such as PgUp, End, etc. don't come through with the
    // ENHANCED_KEY flag set so we'll infer it here.
    switch (key_vk)
    {
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_HOME:
    case VK_END:
    case VK_INSERT:
    case VK_DELETE:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_BACK:
        key_flags |= ENHANCED_KEY;
        break;
    };

    // Convert enhanced keys to normal mode xterm compatible escape sequences.
    if (key_flags & ENHANCED_KEY)
    {
        static const struct {
            int                 code;
            const char* const*  seqs;
        } sc_map[] = {
            { 'H', terminfo::kcuu1, }, // up
            { 'P', terminfo::kcud1, }, // down
            { 'K', terminfo::kcub1, }, // left
            { 'M', terminfo::kcuf1, }, // right
            { 'R', terminfo::kich1, }, // insert
            { 'S', terminfo::kdch1, }, // delete
            { 'G', terminfo::khome, }, // home
            { 'O', terminfo::kend, },  // end
            { 'I', terminfo::kpp, },   // pgup
            { 'Q', terminfo::knp, },   // pgdn
            { '\x0e', terminfo::kbks, },// bkspc
        };

        for (const auto& iter : sc_map)
        {
            if (iter.code != key_sc)
                continue;

            push(iter.seqs[terminfo::keymod_index(key_flags)]);
            break;
        }

        return;
    }

    // This builds Ctrl-<key> c0 codes. Some of these actually come though in
    // key_char and some don't.
    bool differentiate = false;
    if ((key_flags & CTRL_PRESSED) && !(key_flags & SHIFT_PRESSED))
    {
        bool ctrl_code = true;

        switch (key_vk)
        {
        case 'A':   case 'B':   case 'C':   case 'D':
        case 'E':   case 'F':   case 'G':   case 'H':
        case 'I':   case 'J':   case 'K':   case 'L':
        case 'M':   case 'N':   case 'O':   case 'P':
        case 'Q':   case 'R':   case 'S':   case 'T':
        case 'U':   case 'V':   case 'W':   case 'X':
        case 'Y':   case 'Z':
            if (key_vk == 'H' || key_vk == 'I' || key_vk == 'M')
                differentiate = g_differentiate_keys.get();
            if (differentiate)
                ctrl_code = false;
            else
            {
                key_vk -= 'A' - 1;
                ctrl_code = true;
            }
            break;
        case '2':       key_vk = 0;         break;
        case '6':       key_vk = 0x1e;      break;
        case 0xbd:      key_vk = 0x1f;      break;
        case 0xdb:
            if (g_differentiate_keys.get())
            {
                ctrl_code = false;
                break;
            }
            // fall through
        case 0xdc:
        case 0xdd:
            key_vk -= 0xdb - 0x1b;
            break;
        default:        ctrl_code = false;  break;
        }

        if (ctrl_code)
        {
            if (key_flags & ALT_PRESSED)
                push(0x1b);

            push(key_vk);
            return;
        }
    }

    // Ok, it's a key that doesn't have a "normal" terminal representation.  Can
    // we produce an extended XTerm input sequence for the input?
    if (terminfo::is_vk_recognized(key_vk))
    {
        str<> key_seq;
        int mod = terminfo::xterm_modifier(key_flags);
        if (mod >= 2)
            key_seq.format("\x1b[27;%u;%u~", mod, key_vk);
        else
            key_seq.format("\x1b[27;%u~", key_vk);
        push(key_seq.c_str());
        return;
    }
}

//------------------------------------------------------------------------------
void win_terminal_in::push(const char* seq)
{
    static const unsigned int mask = sizeof_array(m_buffer) - 1;

    assert(!m_lead_surrogate);
    m_lead_surrogate = 0;

    int index = m_buffer_head + m_buffer_count;
    for (; m_buffer_count <= mask && *seq; ++m_buffer_count, ++index, ++seq)
    {
        assert(m_buffer_count < sizeof_array(m_buffer));
        if (m_buffer_count < sizeof_array(m_buffer))
            m_buffer[index & mask] = *seq;
        else
            return;
    }
}

//------------------------------------------------------------------------------
void win_terminal_in::push(unsigned int value)
{
    static const unsigned int mask = sizeof_array(m_buffer) - 1;

    int index = m_buffer_head + m_buffer_count;

    if (value < 0x80)
    {
        assert(!m_lead_surrogate);
        m_lead_surrogate = 0;

        assert(m_buffer_count < sizeof_array(m_buffer));
        if (m_buffer_count < sizeof_array(m_buffer))
        {
            m_buffer[index & mask] = value;
            ++m_buffer_count;
        }
        return;
    }

    if (is_lead_surrogate(value))
    {
        m_lead_surrogate = value;
        return;
    }

    wchar_t wc[3];
    unsigned int len = 0;
    if (m_lead_surrogate)
    {
        wc[len++] = m_lead_surrogate;
        m_lead_surrogate = 0;
    }
    wc[len++] = wchar_t(value);
    wc[len] = 0;

    char utf8[mask + 1];
    unsigned int n = to_utf8(utf8, sizeof_array(utf8), wc);
    for (unsigned int i = 0; i < n; ++i, ++index)
    {
        assert(m_buffer_count < sizeof_array(m_buffer));
        if (m_buffer_count < sizeof_array(m_buffer))
        {
            m_buffer[index & mask] = utf8[i];
            m_buffer_count++;
        }
    }
}

//------------------------------------------------------------------------------
unsigned char win_terminal_in::pop()
{
    if (!m_buffer_count)
        return input_none_byte;

    unsigned char value = m_buffer[m_buffer_head];

    --m_buffer_count;
    m_buffer_head = (m_buffer_head + 1) & (sizeof_array(m_buffer) - 1);

    return value;
}
