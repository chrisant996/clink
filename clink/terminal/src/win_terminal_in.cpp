// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "win_terminal_in.h"
#include "scroll.h"
#include "input_idle.h"
#include "key_tester.h"
#include "terminal_helpers.h"
#include "screen_buffer.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/str_hash.h>
#include <core/settings.h>
#include <core/debugheap.h>

#ifdef DEBUG
#include <core/log.h>
#endif

#include <assert.h>
#include <unordered_map>

//------------------------------------------------------------------------------
setting_bool g_terminal_raw_esc(
    "terminal.raw_esc",
    "Esc sends a literal escape character",
    "When disabled (the default), pressing Esc or Alt+[ or Alt+Shift+O send unique\n"
    "key sequences to provide a predictable, reliable, and configurable input\n"
    "experience.  Use 'clink echo' to find the key sequences.\n"
    "\n"
    "When this setting is enabled, then pressing those keys sends the same key\n"
    "sequences as in Unix/etc.  However, they are ambiguous and conflict with the\n"
    "beginning of many other key sequences, leading to surprising or confusing\n"
    "input situations."
    "\n"
    "Changing this only affects future Clink sessions, not the current session.",
    false);

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
extern setting_enum g_default_bindings;

//------------------------------------------------------------------------------
extern "C" void reset_wcwidths();
extern "C" int is_locked_cursor();
extern HANDLE get_recognizer_event();
extern void host_refresh_recognizer();

//------------------------------------------------------------------------------
static HANDLE s_interrupt = NULL;

//------------------------------------------------------------------------------
static const int CTRL_PRESSED = LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED;
static const int ALT_PRESSED = LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED;

// TODO: 0.4.8 keyboard compatibility mode
#define CSI(x) "\x1b[" #x
#define SS3(x) "\x1bO" #x
#define ACSI(x) "\x1b\x1b[" #x
#define ASS3(x) "\x1b\x1bO" #x
#define MOK(x) "\x1b[27;" #x "~"
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
static const char* const kbks[]  = { "\b",    MOK(2;8),  "\x7f",    MOK(6;8),  "\x1b\b",  MOK(4;8),  "\x1b\x7f",MOK(8;8)  }; // bkspc
static const char* const kret[]  = { "\r",    MOK(2;13), MOK(5;13), MOK(6;13), MOK(3;13), MOK(4;13), MOK(7;13), MOK(8;13) }; // enter (return)
static const char* const kcbt    = CSI(Z);
static const char* const kaltO   = CSI(27;4;79~);
static const char* const kaltlb  = CSI(27;3;91~);
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
static const char* const ktab[]  = { "\t",    CSI(Z), MOK(5;9),   MOK(6;9),   "",   "",      "",         ""         }; // TAB
static const char* const kspc[]  = { " ",  MOK(2;32), MOK(5;32),  MOK(6;32),  "",   "",      MOK(7;32),  MOK(8;32)  }; // SPC

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
// Can't use "\x1b\x1b" because Alt+FN gets prefixed with meta Esc and for
// example Alt+F4 becomes "\x1b\x1bOS".  So because of meta-fication ESCESC is
// not a unique sequence.
const char* get_bindable_esc()
{
    // Loading this before settings makes it impossible for the setting to ever
    // take effect, since it takes effect only once during a session.
    assert(settings::get_ever_loaded());

    static const char* const bindableEsc = g_terminal_raw_esc.get() ? nullptr : "\x1b[27;27~";
    return bindableEsc;
}

//------------------------------------------------------------------------------
// Use unsigned; WCHAR and unsigned short can give wrong results.
#define IN_RANGE(n1, b, n2)     ((unsigned)((b) - (n1)) <= unsigned((n2) - (n1)))
inline bool is_lead_surrogate(unsigned int ch) { return IN_RANGE(0xD800, ch, 0xDBFF); }



//------------------------------------------------------------------------------
static char s_verbose_input = false;
void set_verbose_input(int verbose)
{
    s_verbose_input = char(verbose);
}

//------------------------------------------------------------------------------
void interrupt_input()
{
    if (s_interrupt)
        SetEvent(s_interrupt);
}



//------------------------------------------------------------------------------
struct keyseq_name : public no_copy
{
    keyseq_name(char* p, short int eqclass, short int order) { s = p; eq = eqclass; o = order; }
    keyseq_name(keyseq_name&& a) { s = a.s; eq = a.eq; o = a.o; a.s = nullptr; }
    ~keyseq_name() { free(s); }
    keyseq_name& operator=(keyseq_name&& a) { s = a.s; eq = a.eq; o = a.o; a.s = nullptr; return *this; }

    char* s;
    short int eq;
    short int o;
};

//------------------------------------------------------------------------------
struct keyseq_key : public no_copy
{
    keyseq_key(const char* p, unsigned int find_len=0) { this->s = p; this->find_len = find_len; }
    keyseq_key(keyseq_key&& a) { s = a.s; find_len = a.find_len; a.s = nullptr; }
    keyseq_key& operator=(keyseq_key&& a) { s = a.s; find_len = a.find_len; a.s = nullptr; return *this; }

    const char* s;
    unsigned int find_len;
};

//------------------------------------------------------------------------------
struct keyseq_hasher
{
    size_t operator()(const keyseq_key& lookup) const
    {
        unsigned int find_len = lookup.find_len;
        if (!find_len)
            find_len = static_cast<unsigned int>(strlen(lookup.s));
        return str_hash(lookup.s, find_len);
    }
};

//------------------------------------------------------------------------------
struct map_cmp_str
{
    bool operator()(keyseq_key const& a, keyseq_key const& b) const
    {
        if (a.find_len)
        {
            assert(!b.find_len);
            const char* bs = b.s;
            unsigned int len = a.find_len;
            for (const char* as = a.s; len--; as++, bs++)
            {
                int cmp = int((unsigned char)*as) - int((unsigned char)*bs);
                if (cmp)
                    return false;
            }
        }
        else if (b.find_len)
        {
            assert(!a.find_len);
            const char* as = a.s;
            unsigned int len = b.find_len;
            for (const char* bs = b.s; len--; as++, bs++)
            {
                int cmp = int((unsigned char)*as) - int((unsigned char)*bs);
                if (cmp)
                    return false;
            }
        }
        else
        {
            const char* as = a.s;
            const char* bs = b.s;
            while (true)
            {
                int cmp = int((unsigned char)*as) - int((unsigned char)*bs);
                if (cmp)
                    return false;
                if (!*as)
                    break;
                as++;
                bs++;
            }
        }
        return true;
    }
};
static std::unordered_map<keyseq_key, keyseq_name, keyseq_hasher, map_cmp_str> map_keyseq_to_name;
static char map_keyseq_differentiate = -1;
static int map_default_bindings = -1;

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
static void remove_keyseq_from_name(const char* keyseq)
{
    if (!keyseq || !*keyseq)
        return;

    keyseq_key first(keyseq);
    map_keyseq_to_name.erase(first);
}

//------------------------------------------------------------------------------
static void ensure_keyseqs_to_names()
{
    if (!map_keyseq_to_name.empty() &&
        map_keyseq_differentiate == !!g_differentiate_keys.get() &&
        map_default_bindings == g_default_bindings.get())
        return;

    dbg_ignore_scope(snapshot, "Key names");

    static const char* const mods[] = { "", "S-", "C-", "C-S-", "A-", "A-S-", "A-C-", "A-C-S-" };
    static_assert(sizeof_array(mods) == sizeof_array(terminfo::kcuu1), "modifier name count must match modified key array sizes");

    str<32> builder;

    map_keyseq_differentiate = !!g_differentiate_keys.get();
    map_default_bindings = g_default_bindings.get();

    const char* bindableEsc = get_bindable_esc();
    if (bindableEsc)
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
        add_keyseq_to_name(terminfo::kbks[m], "Bkspc", builder, m);

        if (m > 0)
            add_keyseq_to_name(terminfo::kret[m], "Enter", builder, m);
    }

    if (map_keyseq_differentiate || map_default_bindings == 1/*windows*/)
    {
        builder = mods[0];
        add_keyseq_to_name("\x0d", "Enter", builder, 0);
    }

    if (!g_terminal_raw_esc.get())
    {
        add_keyseq_to_name("\x1b[27;3;91~", "A-[", builder, 0);
        add_keyseq_to_name("\x1b[27;4;79~", "A-S-O", builder, 0);
    }

    if (!map_keyseq_differentiate)
    {
        remove_keyseq_from_name(terminfo::kbks[4]);
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
static bool key_name_from_vk(int key_vk, str_base& out, int scan=0)
{
    UINT key_scan = scan ? scan : MapVirtualKeyW(key_vk, MAPVK_VK_TO_VSC);
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

    // Look up the sequence in the special key names map.  Finds the longest
    // matching key name (in case of non-unique names existing).
    ensure_keyseqs_to_names();
    for (unsigned int find_len = min<unsigned int>(16, static_cast<unsigned int>(strlen(keyseq))); find_len; --find_len)
    {
        keyseq_key lookup(keyseq, find_len);
        auto const& iter = map_keyseq_to_name.find(lookup);
        if (iter != map_keyseq_to_name.end())
        {
            len = (int)strlen(iter->first.s);
            eqclass = iter->second.eq;
            order = iter->second.o - (int)map_keyseq_to_name.size();
            return iter->second.s;
        }
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
unsigned int win_terminal_in::get_dimensions()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_stdout, &csbi);
    auto cols = short(csbi.dwSize.X);
    auto rows = short(csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
    return (cols << 16) | rows;
}



//------------------------------------------------------------------------------
void win_terminal_in::begin()
{
    if (!s_interrupt)
        s_interrupt = CreateEvent(nullptr, false, false, nullptr);

    m_buffer_count = 0;
    m_lead_surrogate = 0;
    m_stdin = GetStdHandle(STD_INPUT_HANDLE);
    m_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    m_dimensions = get_dimensions();
    GetConsoleMode(m_stdin, &m_prev_mode);
    show_cursor(false);

    m_prev_mouse_button_state = 0;
    if (GetKeyState(VK_LBUTTON) < 0)
        m_prev_mouse_button_state |= FROM_LEFT_1ST_BUTTON_PRESSED;
    if (GetKeyState(VK_RBUTTON) < 0)
        m_prev_mouse_button_state |= RIGHTMOST_BUTTON_PRESSED;
}

//------------------------------------------------------------------------------
void win_terminal_in::end()
{
    show_cursor(true);
    SetConsoleMode(m_stdin, m_prev_mode);
    m_stdin = nullptr;
    m_stdout = nullptr;
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
void win_terminal_in::fix_console_input_mode()
{
    DWORD modeIn;
    if (GetConsoleMode(m_stdin, &modeIn))
    {
        DWORD mode = modeIn;

        // Compensate when this is reached with the console mode set wrong.
        // For example, this can happen when Lua code uses io.popen():lines()
        // and returns without finishing reading the output, or uses
        // os.execute() in a coroutine.
        mode &= ~ENABLE_PROCESSED_INPUT;
#ifdef DEBUG
        if (modeIn & ENABLE_PROCESSED_INPUT)
            LOG("CONSOLE MODE: console input is in ENABLE_PROCESSED_INPUT mode (0x%x)", modeIn);
#endif

        mode = select_mouse_input(mode);
        if (mode & ENABLE_MOUSE_INPUT)
            console_config::fix_quick_edit_mode(mode);

        if (mode != modeIn)
            SetConsoleMode(m_stdin, mode);
    }
}

//------------------------------------------------------------------------------
static void fix_console_output_mode(HANDLE h, DWORD modeExpected)
{
    DWORD modeActual;
    if (GetConsoleMode(h, &modeActual) && modeActual != modeExpected)
    {
#ifdef DEBUG
        LOG("CONSOLE MODE: console output mode changed (expected 0x%x, actual 0x%x)", modeExpected, modeActual);
#endif
        SetConsoleMode(h, modeExpected);
    }
}

//------------------------------------------------------------------------------
void win_terminal_in::read_console(input_idle* callback)
{
    // Hide the cursor unless we're accepting input so we don't have to see it
    // jump around as the screen's drawn.
    struct cursor_scope {
        cursor_scope()  { show_cursor(true); }
        ~cursor_scope() { show_cursor(false); }
    } _cs;

    // Conhost restarts the cursor blink when writing to the console. It restarts
    // hidden which means that if you type faster than the blink the cursor turns
    // invisible. Fortunately, moving the cursor restarts the blink on visible.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_stdout, &csbi);
    if (!is_scroll_mode())
        SetConsoleCursorPosition(m_stdout, csbi.dwCursorPosition);

    // Reset interrupt detection (allow Ctrl+Break to cancel input).
    if (s_interrupt)
        ResetEvent(s_interrupt);

    // Read input records sent from the terminal (aka conhost) until some
    // input has been buffered.
    const unsigned int buffer_count = m_buffer_count;
    while (buffer_count == m_buffer_count)
    {
        DWORD modeExpected;
        const bool has_mode = !!GetConsoleMode(m_stdout, &modeExpected);

        fix_console_input_mode();

        while (true)
        {
            unsigned count = 1;
            HANDLE handles[4] = { m_stdin };
            DWORD recognizer_waited = WAIT_FAILED;

            if (s_interrupt)
                handles[count++] = s_interrupt;

            if (callback)
            {
                if (void* event = get_recognizer_event())
                {
                    recognizer_waited = WAIT_OBJECT_0 + count;
                    handles[count++] = event;
                }
                if (void* event = callback->get_waitevent())
                    handles[count++] = event;
            }

            fix_console_input_mode();

            const DWORD timeout = callback ? callback->get_timeout() : INFINITE;
            const DWORD waited = WaitForMultipleObjects(count, handles, false, timeout);
            if (waited != WAIT_TIMEOUT)
            {
                if (waited <= WAIT_OBJECT_0 || waited >= WAIT_OBJECT_0 + count)
                    break;
                if (waited == WAIT_OBJECT_0 + 1 && s_interrupt)
                {
                    m_buffer_head = 0;
                    m_buffer_count = 1;
                    m_buffer[0] = input_abort_byte;
                    return;
                }
            }

            if (callback)
            {
                if (waited == recognizer_waited)
                    host_refresh_recognizer();
                else
                    callback->on_idle();
            }

            if (has_mode)
                fix_console_output_mode(m_stdout, modeExpected);
        }

        if (has_mode)
            fix_console_output_mode(m_stdout, modeExpected);

        DWORD count;
        INPUT_RECORD record;
        if (!ReadConsoleInputW(m_stdin, &record, 1, &count))
        {
            // Handle's probably invalid if ReadConsoleInput() failed.
            m_buffer_head = 0;
            m_buffer_count = 1;
            m_buffer[0] = input_abort_byte;
            return;
        }

        switch (record.EventType)
        {
        case KEY_EVENT:
            process_input(record.Event.KeyEvent);
            filter_unbound_input(buffer_count);
            break;

        case MOUSE_EVENT:
            process_input(record.Event.MouseEvent);
            break;

        case WINDOW_BUFFER_SIZE_EVENT:
            // Windows can move the cursor onto a new line as a result of line
            // wrapping adjustments.  If the width changes then return to give
            // editor modules a chance to respond to the width change.
            reset_wcwidths();

            {
                CONSOLE_SCREEN_BUFFER_INFO csbiNew;
                GetConsoleScreenBufferInfo(m_stdout, &csbiNew);
                if (csbi.dwSize.X != csbiNew.dwSize.X)
                    return;
                csbi = csbiNew; // Update for next time.
            }
            break;
        }
    }
}

//------------------------------------------------------------------------------
static void verbose_input(KEY_EVENT_RECORD const& record)
{
    int key_char = record.uChar.UnicodeChar;
    int key_vk = record.wVirtualKeyCode;
    int key_sc = record.wVirtualScanCode;
    int key_flags = record.dwControlKeyState;

    char buf[32];
    buf[0] = 0;
    str_base tmps(buf);
    const char* key_name = key_name_from_vk(key_vk, tmps, key_sc) ? buf : "UNKNOWN";

    const char* dead = "";
#if defined(USE_TOUNICODE_FOR_DEADKEYS)
    // NOT VIABLE:
    //  - Is destructive; it alters the internal keyboard state.
    //  - It doesn't detect dead keys anyway.
    str<> tmp;
    static const char* const maybe_newline = "";
    int tu = 0;
    char tmp2[33];
    WCHAR wbuf[33];
    BYTE keystate[256];
    tmp2[0] = '\0';
    if (GetKeyboardState(keystate))
    {
        wbuf[0] = '\0';
        tu = ToUnicode(key_vk, key_sc, keystate, wbuf, sizeof_array(wbuf) - 1, 2);
        if (tu >= 0)
            wbuf[tu] = '\0';
        to_utf8(str_base(tmp2), wbuf);
    }
    if (tu == 0)
        tmp << "  (unknown key)";
    else if (tu < 0)
        tmp << "  dead key \"" << tmp2 << "\"";
    else
        tmp << "  ToUnicode \"" << tmp2 << "\"";
    dead = tmp.c_str();
#elif defined(USE_MAPVIRTUALKEY_FOR_DEADKEYS)
    // CLOSE...BUT NOT VIABLE:
    //  - Always uses the keyboard input layout from when the process
    //    started, even when using the Ex version of the API and using
    //    GetKeyboardLayout(GetCurrentThreadId()) to get the current layout.
    //  - Always reports accent/etc characters as dead keys, even when
    //    they're not.  E.g. pressing Grave Accent twice calls both of them
    //    dead keys, when in reality the second one is not dead and gets
    //    translated to the actual grave accent character.
    //
    // The second issue could be accommodated by only checking if the input
    // was not translatable into text.  But it doesn't use the current
    // keyboard layout, so it's unreliable.
    str<> tmp;
    if (INT(MapVirtualKeyW(key_vk, MAPVK_VK_TO_CHAR)) < 0)
        tmp << "  (dead key)";
    dead = tmp.c_str();
#endif

    const char* pro = (s_verbose_input > 1) ? "\x1b[s\x1b[H" : "";
    const char* epi = (s_verbose_input > 1) ? "\x1b[K\x1b[u" : "\n";

    printf("%skey event:  %c%c%c %c%c  flags=0x%08.8x  char=0x%04.4x  vk=0x%04.4x  scan=0x%04.4x  \"%s\"%s%s",
            pro,
            (key_flags & SHIFT_PRESSED) ? 'S' : '_',
            (key_flags & LEFT_CTRL_PRESSED) ? 'C' : '_',
            (key_flags & LEFT_ALT_PRESSED) ? 'A' : '_',
            (key_flags & RIGHT_ALT_PRESSED) ? 'A' : '_',
            (key_flags & RIGHT_CTRL_PRESSED) ? 'C' : '_',
            key_flags,
            key_char,
            key_vk,
            key_sc,
            key_name,
            dead,
            epi);
}

//------------------------------------------------------------------------------
void win_terminal_in::process_input(KEY_EVENT_RECORD const& record)
{
    int key_char = record.uChar.UnicodeChar;
    int key_vk = record.wVirtualKeyCode;
    int key_sc = record.wVirtualScanCode;
    int key_flags = record.dwControlKeyState;

    // Only respond to key down events.
    if (!record.bKeyDown)
    {
        // Some times conhost can send through ALT codes, with the resulting
        // Unicode code point in the Alt key-up event.
        if (key_vk == VK_MENU && key_char)
            key_flags = 0;
        else
            return;
    }

    // We filter out Alt key presses unless they generated a character.
    if (key_vk == VK_MENU)
    {
        if (key_char)
        {
            if (s_verbose_input)
                verbose_input(record);
            push(key_char);
        }

        return;
    }

    // Early out of unaccompanied Ctrl/Shift/Windows key presses.
    if (key_vk == VK_CONTROL || key_vk == VK_SHIFT || key_vk == VK_LWIN || key_vk == VK_RWIN)
        return;

    if (s_verbose_input)
        verbose_input(record);

    // Special treatment for escape.
    if (key_char == 0x1b && (key_vk == VK_ESCAPE || !g_differentiate_keys.get()))
    {
        const char* bindableEsc = get_bindable_esc();
        if (bindableEsc)
            return push(bindableEsc);
    }

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

    // Special treatment for enter + modifiers.
    if (key_vk == VK_RETURN && key_flags && !m_buffer_count)
        return push(terminfo::kret[terminfo::keymod_index(key_flags)]);

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

        if (key_char == 'O' && !g_terminal_raw_esc.get() && !(key_flags & CTRL_PRESSED) && (key_flags & SHIFT_PRESSED) && (key_flags & ALT_PRESSED))
        {
            push(terminfo::kaltO);
            return;
        }
        if (key_char == '[' && !g_terminal_raw_esc.get() && !(key_flags & (CTRL_PRESSED|SHIFT_PRESSED)) && (key_flags & ALT_PRESSED))
        {
            push(terminfo::kaltlb);
            return;
        }

        assert(key_vk != VK_TAB);
        if (key_vk == 'H' || key_vk == 'I')
            simple_char = !(key_flags & CTRL_PRESSED) || !g_differentiate_keys.get();
        else if (key_vk == 'M')
            simple_char = !((key_flags & CTRL_PRESSED)) || (!g_differentiate_keys.get() && g_default_bindings.get() != 1/*windows*/);
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

    const char* const* seqs = nullptr;
    switch (key_vk)
    {
    case VK_UP:     seqs = terminfo::kcuu1; break;  // up
    case VK_DOWN:   seqs = terminfo::kcud1; break;  // down
    case VK_LEFT:   seqs = terminfo::kcub1; break;  // left
    case VK_RIGHT:  seqs = terminfo::kcuf1; break;  // right
    case VK_HOME:   seqs = terminfo::khome; break;  // insert
    case VK_END:    seqs = terminfo::kend; break;   // delete
    case VK_INSERT: seqs = terminfo::kich1; break;  // home
    case VK_DELETE: seqs = terminfo::kdch1; break;  // end
    case VK_PRIOR:  seqs = terminfo::kpp; break;    // pgup
    case VK_NEXT:   seqs = terminfo::knp; break;    // pgdn
    case VK_BACK:   seqs = terminfo::kbks; break;   // bkspc
    }
    if (seqs)
    {
        push(seqs[terminfo::keymod_index(key_flags)]);
        return;
    }

    // This builds Ctrl-<key> c0 codes. Some of these actually come though in
    // key_char and some don't.
    if (key_flags & CTRL_PRESSED)
    {
        bool ctrl_code = false;

        if (!(key_flags & SHIFT_PRESSED) || key_vk == '2' || key_vk == '6')
        {
            ctrl_code = true;

            switch (key_vk)
            {
            case 'A':   case 'B':   case 'C':   case 'D':
            case 'E':   case 'F':   case 'G':   case 'H':
            case 'I':   case 'J':   case 'K':   case 'L':
            case 'M':   case 'N':   case 'O':   case 'P':
            case 'Q':   case 'R':   case 'S':   case 'T':
            case 'U':   case 'V':   case 'W':   case 'X':
            case 'Y':   case 'Z':
                if ((key_vk == 'H' || key_vk == 'I') && g_differentiate_keys.get())
                    goto not_ctrl;
                if ((key_vk == 'M') && (g_differentiate_keys.get() || g_default_bindings.get() == 1/*windows*/))
                    goto not_ctrl;
                key_vk -= 'A' - 1;
                ctrl_code = true;
                break;
            case '2':
                if (g_differentiate_keys.get() && !(key_flags & SHIFT_PRESSED))
                    goto not_ctrl;
                key_vk = 0;
                break;
            case '6':
                if (g_differentiate_keys.get() && !(key_flags & SHIFT_PRESSED))
                    goto not_ctrl;
                key_vk = 0x1e;
                break;
            case VK_OEM_MINUS:          // 0xbd, - in any country.
                key_vk = 0x1f;
                break;
            default:
                // Can't use VK_OEM_4, VK_OEM_5, and VK_OEM_6 for detecting ^[,
                // ^\, and ^] because OEM key mapping differ by keyboard/locale.
                // However, the OS/OEM keyboard driver produces enough details
                // to make it possible to identify what's really going on, at
                // least for these specific keys (but not for VK_OEM_MINUS, 2,
                // or 6).  Ctrl makes the bracket and backslash keys produce the
                // needed control code in key_char, so we can simply use that.
                switch (key_char)
                {
                case 0x1b:
                    if (g_differentiate_keys.get())
                        goto not_ctrl;
                    // fall thru
                case 0x1c:
                case 0x1d:
                    key_vk = key_char;
                    break;
                default:
not_ctrl:
                    ctrl_code = false;
                    break;
                }
                break;
            }
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
static void verbose_input(MOUSE_EVENT_RECORD const& record)
{
    auto key_flags = record.dwControlKeyState;

    printf("mouse event:  %u,%u  %c%c%c %c%c  ctrlkeystate=0x%08.8x  buttonstate=0x%04.4x  eventflags=0x%04.4x\n",
            record.dwMousePosition.X,
            record.dwMousePosition.Y,
            (key_flags & SHIFT_PRESSED) ? 'S' : '_',
            (key_flags & LEFT_CTRL_PRESSED) ? 'C' : '_',
            (key_flags & LEFT_ALT_PRESSED) ? 'A' : '_',
            (key_flags & RIGHT_ALT_PRESSED) ? 'A' : '_',
            (key_flags & RIGHT_CTRL_PRESSED) ? 'C' : '_',
            key_flags,
            record.dwButtonState,
            record.dwEventFlags);
}

//------------------------------------------------------------------------------
void win_terminal_in::process_input(MOUSE_EVENT_RECORD const& record)
{
    // Remember the button state, to differentiate press vs release.
    const auto prv = m_prev_mouse_button_state;
    m_prev_mouse_button_state = record.dwButtonState;

    // In a race condition, both left and right click may happen simultaneously.
    // Only respond to one; left has priority over right.
    const auto btn = record.dwButtonState;
    const bool left_click = (!(prv & FROM_LEFT_1ST_BUTTON_PRESSED) && (btn & FROM_LEFT_1ST_BUTTON_PRESSED));
    const bool right_click = !left_click && (!(prv & RIGHTMOST_BUTTON_PRESSED) && (btn & RIGHTMOST_BUTTON_PRESSED));
    const bool double_click = left_click && (record.dwEventFlags & DOUBLE_CLICK);
    const bool wheel = !left_click && !right_click && (record.dwEventFlags & MOUSE_WHEELED);
    const bool hwheel = !left_click && !right_click && !wheel && (record.dwEventFlags & MOUSE_HWHEELED);
    const bool drag = (btn & FROM_LEFT_1ST_BUTTON_PRESSED) && !left_click && !right_click && !wheel && !hwheel && (record.dwEventFlags & MOUSE_MOVED);

    const mouse_input_type mask = (left_click ? mouse_input_type::left_click :
                                   right_click ? mouse_input_type::right_click :
                                   double_click ? mouse_input_type::double_click :
                                   wheel ? mouse_input_type::wheel :
                                   hwheel ? mouse_input_type::hwheel :
                                   drag ? mouse_input_type::drag :
                                   mouse_input_type::none);

    if (mask == mouse_input_type::none)
        return;

    if (s_verbose_input)
        verbose_input(record);

    // If the caller isn't prepared to handle the mouse input, then handle
    // certain universal behaviors here.
    if (!m_keys || !m_keys->accepts_mouse_input(mask))
    {
        DWORD mode;
        if (GetConsoleMode(m_stdin, &mode) && !(mode & ENABLE_QUICK_EDIT_MODE))
        {
            if (right_click)
            {
                HWND hwndConsole = GetConsoleWindow();
                if (IsWindowVisible(hwndConsole) &&
                    (GetWindowLongW(hwndConsole, GWL_STYLE) & WS_VISIBLE))
                {
                    POINT pt;
                    GetCursorPos(&pt);
                    LPARAM lParam = MAKELPARAM(pt.x, pt.y);
                    SendMessage(hwndConsole, WM_CONTEXTMENU, 0, lParam);
                }
            }
            else if (wheel)
            {
                // Windows Terminal does NOT support programmatic scrolling.
                // ConEmu and plain Conhost DO support programmatic scrolling.
                int direction = (0 - short(HIWORD(record.dwButtonState))) / 120;
                UINT wheel_scroll_lines = 3;
                SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &wheel_scroll_lines, false);
                ScrollConsoleRelative(m_stdout, direction * int(wheel_scroll_lines), SCR_BYLINE);
            }
        }
        return;
    }

    // Left or right click, or drag.
    if (left_click || right_click || drag)
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(m_stdout, &csbi);

        str<16> tmp;
        const char code = (drag ? 'M' :
                           right_click ? 'R' :
                           record.dwEventFlags & DOUBLE_CLICK ? 'D' : 'L');
        tmp.format("\x1b[$%u;%u%c", record.dwMousePosition.X - csbi.srWindow.Left, record.dwMousePosition.Y - csbi.srWindow.Top, code);
        push(tmp.c_str());
        return;
    }

    // Mouse wheel.
    if (wheel)
    {
        // Windows Terminal does NOT support programmatic scrolling.
        // ConEmu and plain Conhost DO support programmatic scrolling.
        int direction = (0 - short(HIWORD(record.dwButtonState))) / 120;
        UINT wheel_scroll_lines = 3;
        SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &wheel_scroll_lines, false);

        str<16> tmp;
        const char code = (direction < 0 ? 'A' : 'B');
        if (direction < 0)
            direction = 0 - direction;
        tmp.format("\x1b[$%u%c", direction * int(wheel_scroll_lines), code);
        push(tmp.c_str());
        return;
    }

    // Mouse horizontal wheel.
    if (hwheel)
    {
        int direction = (short(HIWORD(record.dwButtonState))) / 32;
        UINT hwheel_distance = 1;

        str<16> tmp;
        const char code = (direction < 0 ? '<' : '>');
        if (direction < 0)
            direction = 0 - direction;
        tmp.format("\x1b[$%u%c", direction * int(hwheel_distance), code);
        push(tmp.c_str());
        return;
    }
}

//------------------------------------------------------------------------------
void win_terminal_in::filter_unbound_input(unsigned int buffer_count)
{
    // If the processed input chord isn't bound, discard it.  Otherwise unbound
    // keys can have the tail part of their sequence show up as though it were
    // typed input.  The approach here assumes no more than one key sequence per
    // input record.
    if (!m_keys)
        return;

    // If there are unprocessed queued keys, then we don't know what keymap will
    // be active when this new input gets processed, so we can't accurately tell
    // whether the key sequence is bound to anything.
    assert(buffer_count == 0);

    const int len = m_buffer_count - buffer_count;
    if (len <= 0)
        return;

    // m_buffer is circular, so copy the key sequence to a separate sequential
    // buffer.  And also because Readline has a bug in rl_function_of_keyseq_len
    // that looks for nul termination even though it's supposed to use a length
    // instead; copying to a separate buffer makes that easier to mitigate.
    char chord[sizeof_array(m_buffer) + 1];
    static const unsigned int mask = sizeof_array(m_buffer) - 1;
    for (int i = 0; i < len; ++i)
        chord[i] = m_buffer[(m_buffer_head + i) & mask];
    chord[len] = '\0'; // Word around rl_function_of_keyseq_len bug.

    str<32> new_chord;
    if (m_keys->translate(chord, len, new_chord))
    {
        // Reset buffer and push translated chord.
        m_buffer_count = buffer_count;
        for (unsigned int i = 0; i < new_chord.length(); ++i)
            push((unsigned int)new_chord.c_str()[i]);
    }
    else if (!m_keys->is_bound(chord, len))
    {
        // Reset buffer, discarding the chord.
        m_buffer_count = buffer_count;
    }

    m_keys->set_keyseq_len(m_buffer_count);
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
