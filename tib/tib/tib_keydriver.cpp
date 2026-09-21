// Copyright (c) 2026 Christopher Antos
// Portions Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "maybe_windows.h"
#include "tib_base.h"
#include "tib_terminal.h"
#include "tib_termcap.h"
#include <memory>
#include <conio.h>
#include <assert.h>

namespace tib {

static bool s_use_raw_esc = true;
static bool s_differentiate_keys = false;
static bool s_use_altgr_substitute = false;

static const int32_t CTRL_PRESSED = LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED;
static const int32_t ALT_PRESSED = LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED;

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
static const char* const kbks[]  = { "\x7f",  MOK(2;8),  "\b",      MOK(6;8),  "\x1b\b",  MOK(4;8),  "\x1b\x7f",MOK(8;8)  }; // bkspc
static const char* const kret[]  = { "\r",    MOK(2;13), MOK(5;13), MOK(6;13), MOK(3;13), MOK(4;13), MOK(7;13), MOK(8;13) }; // enter (return)
static const char* const kcbt    = CSI(Z); // back tab key
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

static int32_t xterm_modifier(int32_t key_flags)
{
    // Calculate Xterm's modifier number.
    int32_t i = 0;
    i |= !!(key_flags & SHIFT_PRESSED);
    i |= !!(key_flags & ALT_PRESSED) << 1;
    i |= !!(key_flags & CTRL_PRESSED) << 2;
    return i + 1;
}

static int32_t keymod_index(int32_t key_flags)
{
    // Calculate key sequence table modifier index.
    int32_t i = 0;
    i |= !!(key_flags & SHIFT_PRESSED);
    i |= !!(key_flags & CTRL_PRESSED) << 1;
    i |= !!(key_flags & ALT_PRESSED) << 2;
    return i;
}

static bool is_vk_recognized(int32_t key_vk)
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

// Can't use "\x1b\x1b" because Alt+FN gets prefixed with meta Esc and for
// example Alt+F4 becomes "\x1b\x1bOS".  So because of meta-fication ESCESC is
// not a unique sequence.
const char* get_bindable_esc()
{
    static const char* const bindableEsc = s_use_raw_esc ? nullptr : "\x1b[27;27~";
    return bindableEsc;
}

// Use uint32_t; WCHAR and uint16_t can give wrong results.
#define IN_RANGE(n1, b, n2)     ((uint32_t)((b) - (n1)) <= uint32_t((n2) - (n1)))
inline bool is_lead_surrogate(uint32_t ch) { return IN_RANGE(0xD800, ch, 0xDBFF); }

static bool key_name_from_vk(int32_t key_vk, cstring& out, int32_t scan=0)
{
    UINT key_scan = scan ? scan : MapVirtualKeyW(key_vk, MAPVK_VK_TO_VSC);
    if (key_scan)
    {
        LONG l = (key_scan & 0x01ff) << 16;
        wchar_t name[16];
        if (GetKeyNameTextW(l, name, int(std::size(name))))
        {
            to_utf8(name, -1, out);
            return true;
        }
    }

    return false;
}

// Try to handle Alt-Ctrl-[, Alt-Ctrl-], Alt-Ctrl-\ better, at least in keyboard
// layouts where the [, ], or \ is the regular (unshifted) name of the key.
static bool translate_ctrl_bracket(int32_t& key_vk, int32_t key_sc)
{
    cstring tmps;

    // Can't apply caching here, because software keyboard layouts can be
    // changed dynamically.
    if (!key_name_from_vk(key_vk, tmps, key_sc))
        return false;

    const char* key_name = tmps.c_str();
    if (key_name[1])
        return false;

    switch (key_name[0])
    {
    case '[':
        if (!s_use_raw_esc)
        {
            // Must avoid this because it would produce "\e\e" which is the
            // prefix for some Fn keys (e.g. Alt-F4 is "\e\eOS").  But raw Esc
            // mode explicitly requests that behavior.
            return false;
        }
        break;
    case ']':
    case '\\':
        break;
    default:
        return false;
    }

    key_vk = key_name[0] - '@';
    return true;
}

void custom_vt_driver(const KEY_EVENT_RECORD& record, pushed_input& pushed)
{
    assert(pushed.empty());

    int32_t key_char = record.uChar.UnicodeChar;
    int32_t key_vk = record.wVirtualKeyCode;
    int32_t key_sc = record.wVirtualScanCode;
    int32_t key_flags = record.dwControlKeyState;

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
            pushed.push_utf16(key_char);
        return;
    }

    // Early out of unaccompanied Ctrl/Shift/Windows key presses.
    if (key_vk == VK_CONTROL || key_vk == VK_SHIFT || key_vk == VK_LWIN || key_vk == VK_RWIN)
        return;

    // Special treatment for escape.
    if (key_char == 0x1b && (key_vk == VK_ESCAPE || !s_differentiate_keys))
    {
        if (!s_use_raw_esc)
        {
            const char* bindableEsc = get_bindable_esc();
            if (bindableEsc)
            {
                pushed.push(bindableEsc);
                return;
            }
        }
    }

    // Windows supports an AltGr substitute which we check for here. For
    // compatibility purposes it can be disabled here. The On-Screen Keyboard
    // in Windows sends both Left- and Right- Alt flags when using AltGr (e.g.
    // in the Swedish keyboard layout). When both Left- and Right- Alt are
    // pressed, treat it as real AltGr. It makes more sense anyway, and it
    // also lets the OSK input \ successfully.
    if ((key_flags & (LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED)) == LEFT_ALT_PRESSED)
    {
        bool altgr_sub = !!(key_flags & (LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED));
        altgr_sub &= !!key_char;

        if (altgr_sub && !s_use_altgr_substitute)
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
    if (key_vk == VK_TAB && (key_char == 0x09 || !key_char) /*&& !m_buffer_count*/)
    {
        pushed.push(terminfo::ktab[terminfo::keymod_index(key_flags)]);
        return;
    }
    if (key_vk == VK_SPACE && (key_char == 0x20 || !key_char) /*&& !m_buffer_count*/)
    {
        pushed.push(terminfo::kspc[terminfo::keymod_index(key_flags)]);
        return;
    }

    // Special treatment for enter + modifiers.
    if (key_vk == VK_RETURN && key_flags /*&& !m_buffer_count*/)
    {
        pushed.push(terminfo::kret[terminfo::keymod_index(key_flags)]);
        return;
    }

    // If the input was formed using AltGr or LeftAlt-LeftCtrl then things get
    // tricky. But there's always a Ctrl bit set, even if the user didn't press
    // a ctrl key. We can use this and the knowledge that Ctrl-modified keys
    // aren't printable to clear appropriate AltGr flags.
    if ((key_char > 0x1f && key_char != 0x7f) && (key_flags & CTRL_PRESSED))
        key_flags &= ~(CTRL_PRESSED|ALT_PRESSED);

    // Special case for ctrl-shift-I (to behave like shift-tab aka. back-tab).
    if (key_char == '\t' /*&& !m_buffer_count*/ && (key_flags & SHIFT_PRESSED) && !s_differentiate_keys)
    {
        pushed.push(terminfo::kcbt);
        return;
    }

    // Function keys (kf1-kf48 from xterm+pcf2)
    unsigned key_func = key_vk - VK_F1;
    if (key_func <= (VK_F12 - VK_F1))
    {
        int32_t kfx_group = terminfo::keymod_index(key_flags);
        pushed.push((terminfo::kfx + (12 * kfx_group) + key_func)[0]);
        return;
    }

    // Include an ESC character in the input stream if Alt is pressed.
    if (key_char)
    {
        bool simple_char;

        if (key_char == 'O' && !s_use_raw_esc && !(key_flags & CTRL_PRESSED) && (key_flags & SHIFT_PRESSED) && (key_flags & ALT_PRESSED))
        {
            pushed.push(terminfo::kaltO);
            return;
        }
        if (key_char == '[' && !s_use_raw_esc && !(key_flags & (CTRL_PRESSED|SHIFT_PRESSED)) && (key_flags & ALT_PRESSED))
        {
            pushed.push(terminfo::kaltlb);
            return;
        }

        assert(key_vk != VK_TAB);
        if (key_vk == 'H' || key_vk == 'I' || key_vk == 'M')
            simple_char = !(key_flags & CTRL_PRESSED) || !s_differentiate_keys;
        else if (key_char == 0x1b && key_vk != VK_ESCAPE)
            simple_char = terminfo::keymod_index(key_flags) < 2; // Modifiers were resulting in incomplete escape codes.
        else if (key_vk == VK_BACK)
            simple_char = false;
        else if (key_vk == VK_RETURN)
            simple_char = !(key_flags & (CTRL_PRESSED|SHIFT_PRESSED));
        else
            simple_char = !(key_flags & CTRL_PRESSED) || !(key_flags & SHIFT_PRESSED);

        if (simple_char)
        {
            if (key_flags & ALT_PRESSED)
                pushed.push(0x1b);
            pushed.push_utf16(key_char);
            return;
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
        pushed.push(seqs[terminfo::keymod_index(key_flags)]);
        return;
    }

    // This builds Ctrl-<key> c0 codes. Some of these actually come though in
    // key_char and some don't.
    if (key_flags & CTRL_PRESSED)
    {
        bool ctrl_code = false;

        if (!(key_flags & SHIFT_PRESSED) || key_vk == '2' || key_vk == '6' || key_vk == VK_OEM_MINUS)
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
                if (s_differentiate_keys && (key_vk == 'H' || key_vk == 'I' || key_vk == 'M'))
                    goto not_ctrl;
                else if (key_vk == 'H')
                    key_vk = 0x7f;
                else
                    key_vk -= 'A' - 1;
                ctrl_code = true;
                break;
            case '2':
                if (s_differentiate_keys && !(key_flags & SHIFT_PRESSED))
                    goto not_ctrl;
                key_vk = 0;
                break;
            case '6':
                if (s_differentiate_keys && !(key_flags & SHIFT_PRESSED))
                    goto not_ctrl;
                key_vk = 0x1e;
                break;
            case VK_OEM_MINUS:          // 0xbd, - in any country.
                if (!(key_flags & SHIFT_PRESSED))
                    ctrl_code = false;
                else
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
                    if (s_differentiate_keys)
                        goto not_ctrl;
                    // fall thru
                case 0x1c:
                case 0x1d:
                    key_vk = key_char;
                    break;
                default:
not_ctrl:
                    if (!translate_ctrl_bracket(key_vk, key_sc))
                        ctrl_code = false;
                    break;
                }
                break;
            }
        }

        if (ctrl_code)
        {
            if (key_flags & ALT_PRESSED)
                pushed.push(0x1b);

            assert(uint8_t(key_vk) == uint32_t(key_vk));
            pushed.push(uint8_t(key_vk));
            return;
        }
    }

    // Ok, it's a key that doesn't have a "normal" terminal representation.  Can
    // we produce an extended XTerm input sequence for the input?
    if (terminfo::is_vk_recognized(key_vk))
    {
        cstring key_seq;
        int32_t mod = terminfo::xterm_modifier(key_flags);
        if (mod >= 2)
            key_seq.printf("\x1b[27;%u;%u~", mod, key_vk);
        else
            key_seq.printf("\x1b[27;%u~", key_vk);
        pushed.push(key_seq.c_str());
        return;
    }
}

} // namespace tib
