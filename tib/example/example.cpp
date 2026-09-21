// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include <stdio.h>

#include "maybe_windows.h"
#include "tib.h"
#include "tib_host.h"
#include <wcwidth.h>
#include <assert.h>

static const char c_long_usage[] =
"Flags:\n"
"  --single          Single line input mode (default).\n"
"  --multiline       Multiple line input mode (max height 3 rows).\n"
"  --fixed           Fixed height input mode (default).\n"
"  --variable        Variable height input mode (implies --multiline).\n"
"  --full-width      Use the full terminal width (default is 40).\n"
"  --no-border       No border (default; same as '--border none').\n"
"  --border STYLE    Use border style (STYLE == light, padding, bar-padding,\n"
"                      first-line, none).\n"
"  --left TEXT       Display TEXT at the left of the first line.\n"
"  --right TEXT      Display TEXT at the right of the first line.\n"
"  --origin X        Set origin X coordinate (1-based).\n"
"  --height ROWS     Set max height to ROWS (default is 3).\n"
"  --rainbow         Apply rainbow colors to words.\n"
"  --show-keys       Show input key sequences.\n"
"  --custom-vt       Use custom VT input driver (always on Win8.1 and lower).\n"
"  --mouse MODE      Mouse VT input mode (MODE == none, vt200, drag, any).\n"
"  --enc MODE        Mouse VT encoding mode (MODE == default, sgr).\n"
"  --show-stats      Show statistics about display manager.\n"
"  --suggest         Show suggestions based on words present.\n"
;

static tib_host::auto_terminal_init s_auto_terminal_init;

#pragma region Example customizations.
static bool s_use_rainbow_faces = false;
static bool s_show_keys = false;

static const char* const c_bar_text_color = "0;38;2;180;140;33";
static const char* const c_border_text_color = "0;38;2;33;33;33";
static const char* const c_border_back_color = "0;48;2;33;33;33";
static const char* const c_border_first_color = "38;2;33;204;33";
constexpr char FACE_CTRL = '^';
constexpr char FACE_RAINBOW = '\x80';

static const tib::border_definition c_padding_border =
{
    nullptr,    "▄",    nullptr,
    "██",               "██",
    nullptr,    "▀",    nullptr,

    0,          1,      0,
    2,                  2,
    0,          1,      0,
};

struct bar_padding_border_definition : public tib::border_definition
{
    bar_padding_border_definition(const char* first_left=nullptr, const char* first_right=nullptr)
    {
        make_bar("▗▄", custom_top_left, top_left, top_left_width);
        make_bar("▐█", custom_left, left, left_width);
        make_bar("▝▀", custom_bottom_left, bottom_left, bottom_left_width);

        top_right = "▖";        top_right_width = 1;
        right = "█▌";           right_width = 2;
        bottom_right = "▘";     bottom_right_width = 1;

        top = "▄";              top_width = 1;
        bottom = "▀";           bottom_width = 1;

        if (first_left)
        {
            const uint32_t first_width = __wcswidth(first_left, strlen(first_left));
            custom_left_2 = custom_left;
            custom_left_2.append_color(c_border_back_color);
            custom_left_2.append_spaces(first_width);
            left_2 = custom_left_2.c_str();
            custom_left.append_color(c_border_back_color);
            custom_left.append_color(c_border_first_color);
            custom_left.append(first_left);
            left_width += first_width;
        }
        if (first_right)
        {
            const uint32_t first_width = __wcswidth(first_right, strlen(first_right));
            custom_right_2.append_color(c_border_back_color);
            custom_right_2.append_spaces(first_width);
            custom_right_2.append_color(c_border_text_color);
            custom_right_2.append(right);
            right_2 = custom_right_2.c_str();
            custom_right.append_color(c_border_back_color);
            custom_right.append_color(c_border_first_color);
            custom_right.append(first_right);
            custom_right.append_color(c_border_text_color);
            custom_right.append(right);
            right = custom_right.c_str();
            right_width += first_width;
        }

#if 0
        assert(is_valid());
#endif
    };

protected:
    static void make_bar(const char* in, tib::cstring& out, const char*& dst, int8_t& dst_width)
    {
        wcwidth_iter iter(in);
        iter.next();
        out.printf("\x1b[%sm", c_bar_text_color);
        out.append(iter.character_pointer(), iter.character_length());
        iter.next();
        out.printf("\x1b[%sm", c_border_text_color);
        out.append(iter.character_pointer(), iter.character_length());
        dst = out.c_str();
        dst_width = 2;
    }

private:
    tib::cstring custom_top_left;
    tib::cstring custom_left;
    tib::cstring custom_left_2;
    tib::cstring custom_right;
    tib::cstring custom_right_2;
    tib::cstring custom_bottom_left;
};

static const bar_padding_border_definition c_bar_padding_border;
static const bar_padding_border_definition c_first_line_border("> ", " HH:MM");
#pragma endregion // Example customizations.

std::shared_ptr<tib::key_table_list> s_normal_bindings;
std::shared_ptr<tib::key_table_list> s_movement_bindings;

int32_t insert_newline(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    ctx.insert_char('\n');
    return 0;
}

int32_t normal_mode(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    ctx.set_bindings(s_normal_bindings);
    return 0;
}

int32_t movement_mode(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    ctx.set_bindings(s_movement_bindings);
    return 0;
}

void make_key_tables()
{
    tib::editor_context::register_command("insert-newline", insert_newline);
    tib::editor_context::register_command("normal-mode", normal_mode);
    tib::editor_context::register_command("movement-mode", movement_mode);

    s_normal_bindings = tib::make_default_key_table(true/*numeric_argument*/);
    {
        auto t = std::make_shared<tib::key_table>();
        t->add("\033", tib::binding_target_func("del-line"));           // ESC by itself

        t->add("\021", tib::binding_target_func("quoted-insert"));      // Ctrl-Q
        t->add("\022", tib::binding_target_func("lorem-ipsum"));        // Ctrl-R
        t->add("\025", tib::binding_target_func("universal-argument")); // Ctrl-U
        t->add("\033m", tib::binding_target_func("insert-newline"));    // Alt-M
        t->add("\033T", tib::binding_target_macro("Macro Text"));       // Alt-Shift-T
        t->add("\033\022", tib::binding_target_func("lorem-ipsum"));    // Alt-Ctrl-R

        t->add("\030\001", tib::binding_target_func("movement-mode"));              // Ctrl-X,Ctrl-A
        t->add("\030\030", tib::binding_target_func("exchange-caret-and-mark"));    // Ctrl-X,Ctrl-X

        s_normal_bindings->emplace_back(t);
    }

    s_movement_bindings = std::make_shared<tib::key_table_list>();
    {
        auto t = std::make_shared<tib::key_table>();
        t->add("\033", tib::binding_target_func("normal-mode"));        // ESC by itself

        t->add("\001", tib::binding_target_func("begin-of-line"));      // Ctrl-A
        t->add("\002", tib::binding_target_func("backward-char"));      // Ctrl-B
        t->add("\004", tib::binding_target_func("del-char-right"));     // Ctrl-D
        t->add("\005", tib::binding_target_func("end-of-line"));        // Ctrl-E
        t->add("\006", tib::binding_target_func("forward-char"));       // Ctrl-F
        t->add("\010", tib::binding_target_func("del-char-left"));      // Ctrl-H
        t->add("\r", tib::binding_target_func("accept-line"));          // Ctrl-M / Enter
        t->add("\021", tib::binding_target_func("screen-line-up"));     // Ctrl-Q
        t->add("\032", tib::binding_target_func("screen-line-down"));   // Ctrl-Z

        t->add("b", tib::binding_target_func("backward-char"));
        t->add("B", tib::binding_target_func("backward-word"));
        t->add("d", tib::binding_target_func("del-char-right"));
        t->add("D", tib::binding_target_func("del-word-right"));
        t->add("f", tib::binding_target_func("forward-char"));
        t->add("F", tib::binding_target_func("forward-word"));
        t->add("h", tib::binding_target_func("del-char-left"));
        t->add("H", tib::binding_target_func("del-word-left"));
        t->add("q", tib::binding_target_func("screen-line-up"));
        t->add("z", tib::binding_target_func("screen-line-down"));

        t->add("\000", 1, tib::binding_target_func("set-mark"));        // Ctrl-@ (Ctrl-2)
        t->add("\001", tib::binding_target_func("select-all"));         // Ctrl-A
        t->add("\003", tib::binding_target_func("copy"));               // Ctrl-C
        t->add("\007", tib::binding_target_func("abort"));              // Ctrl-G
        t->add("\027", tib::binding_target_func("select-word"));        // Ctrl-W

        t->add("\033[H", tib::binding_target_func("begin-of-line"));    // Home
        t->add("\033[F", tib::binding_target_func("end-of-line"));      // End
        t->add("\033[D", tib::binding_target_func("backward-char"));    // Left
        t->add("\033[C", tib::binding_target_func("forward-char"));     // Right
        t->add("\033[1;5D", tib::binding_target_func("backward-word")); // Ctrl-Left
        t->add("\033[1;5C", tib::binding_target_func("forward-word"));  // Ctrl-Right
        t->add("\033[B", tib::binding_target_func("screen-line-down")); // Down
        t->add("\033[A", tib::binding_target_func("screen-line-up"));   // Up

        t->add("\033[1;2H", tib::binding_target_func("cua-begin-of-line"));     // Shift-Home
        t->add("\033[1;2F", tib::binding_target_func("cua-end-of-line"));       // Shift-End
        t->add("\033[1;2D", tib::binding_target_func("cua-backward-char"));     // Shift-Left
        t->add("\033[1;2C", tib::binding_target_func("cua-forward-char"));      // Shift-Right
        t->add("\033[1;6D", tib::binding_target_func("cua-backward-word"));     // Shift-Ctrl-Left
        t->add("\033[1;6C", tib::binding_target_func("cua-forward-word"));      // Shift-Ctrl-Right
        t->add("\033[1;2B", tib::binding_target_func("cua-screen-line-down"));  // Shift-Down
        t->add("\033[1;2A", tib::binding_target_func("cua-screen-line-up"));    // Shift-Up

        t->add("\033[<%#;%#;%#M", tib::binding_target_func("mouse-input"), true); // Mouse press
        t->add("\033[<%#;%#;%#m", tib::binding_target_func("mouse-input"), true); // Mouse release

        for (char seq[3] = { '\033', '0', 0 }; seq[1] <= '9'; ++seq[1])
            t->add(seq, tib::binding_target_func("digit-argument"));
        t->add("-", tib::binding_target_func("digit-argument"));

        t->add("\030\001", tib::binding_target_func("normal-mode"));    // Ctrl-X,Ctrl-A

        s_movement_bindings->emplace_back(t);
    }
}

class custom_input_box : public tib::input_box, protected tib::editor_callbacks
{
public:
                        ~custom_input_box() = default;
                        custom_input_box();

protected:
                        // Methods on the tib::editor_callbacks interface.
    void                provide_faces(const tib::input_buffer& buffer, tib::cstring& faces);
};

custom_input_box::custom_input_box()
{
    tib::ensure_term_caps();
    set_callbacks(this);
}

#pragma region Example customizations.
struct color_t
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

static const color_t c_colors[] =
{
    { 0xcc, 0x00, 0x00 },
    { 0xcc, 0x99, 0x00 },
    { 0xcc, 0xcc, 0x00 },
    { 0x00, 0xcc, 0x00 },
    { 0x00, 0xcc, 0xcc },
    { 0x00, 0x66, 0xcc },
    { 0xcc, 0x00, 0xcc },
};

void custom_input_box::provide_faces(const tib::input_buffer& buffer, tib::cstring& faces)
{
    if (s_use_rainbow_faces)
    {
        uint8_t c = 0;
        bool space = true;
        const tib::cstring& text = buffer.get_text();
        const char* s = text.c_str();
        const size_t len = text.length();
        assert(len == faces.length());
        for (size_t i = 0; i < text.length(); ++i)
        {
            if (s[i] >= 0 && s[i] < ' ')
            {
                faces.set_at(i, FACE_CTRL);
            }
            else
            {
                if (!space && s[i] == ' ')
                    c = (c + 1) % std::size(c_colors);
                space = (s[i] == ' ');

                if (!space)
                    faces.set_at(i, FACE_RAINBOW + c);
            }
        }
    }
}

void join_colors(tib::cstring& s, std::shared_ptr<tib::color_table>& colors, tib::color_element a, const char* b)
{
    s.append_color(colors->get_color(a));
    if (_strnicmp(b, "0;", 2) == 0)
        b += 2;
    s.append_color(b);
}

void join_colors(tib::cstring& s, std::shared_ptr<tib::color_table>& colors, tib::color_element a, tib::color_element b)
{
    const char* c = colors->get_color(b);
    join_colors(s, colors, a, c);
}
#pragma endregion // Example customizations.

#pragma region Show custom feedback.
static void add_feedback_line(const char* color, const char* text, std::vector<tib::additional_display_line>& addl)
{
    tib::additional_display_line line;

    line.text.clear();
    line.text.append_color(color);
    const size_t begin_len = line.text.length();

    line.text.append(text);

    const size_t end_len = line.text.length();
    line.text.append_color("");

    line.width = uint16_t(end_len - begin_len);
    line.bounded = true;

    addl.emplace_back(std::move(line));
}

static void display_feedback(tib::editor_context& ctx, const tib::cstring& show_sequence)
{
    tib::cstring tmp;
    std::vector<tib::additional_display_line> additional;

    if (ctx.get_bindings() == s_movement_bindings)
    {
        add_feedback_line("96;44", "^A=begline  ^B=left  ^E=endline  ^F=right  ^Q=up  ^Z=down", additional);
        add_feedback_line("96;44", "b/B=left-char/word  f/F=right-char/word  q/z=line-up/down", additional);
        add_feedback_line("96;44", "d/D=del-right-char/word  h/H=del-left-char/word          ", additional);
    }

    if (s_show_keys && show_sequence.length())
    {
        tmp.set("  keys:  ");
        tmp.append(show_sequence.c_str(), show_sequence.length());
        add_feedback_line("36", tmp.c_str(), additional);
    }

    if (additional.size())
        ctx.set_additional_lines(additional);
    else
        ctx.clear_additional_lines();
}
#pragma endregion // Show custom feedback.

int main(int argc, const char** argv)
{
    --argc, ++argv;

    {
        tib::cstring v;
        if (tib::getenv("TIB_NO_COALESCE_OUTPUT", v) && !v.empty())
        {
            tib::g_coalesce_output = !(atoi(v.c_str()) > 0);
            tib::g_show_hide_cursor = !(atoi(v.c_str()) > 0);
        }
        if (tib::getenv("TIB_SHOW_STATISTICS", v) && !v.empty())
        {
            if (atoi(v.c_str()) > 0)
                tib::show_display_manager_statistics(true);
        }
    }

#ifdef _WIN32
    tib_host::set_crt_locale_utf8();
#endif

    tib::term_begin();

    std::shared_ptr<custom_input_box> tib = std::make_shared<custom_input_box>();
    make_key_tables();  // BUGBUG: should not require an editor_context to have been created.
    tib->set_bindings(s_normal_bindings);
    tib->set_max_width(40);

#pragma region Example customizations.
    const char c_norm_base[] = "0";
    const char c_padding_base[] = "0;48;2;33;33;33";

    std::shared_ptr<tib::color_table> colors = std::make_shared<tib::color_table>();
    colors->set_color(tib::color_element::base, c_norm_base);
    colors->set_color(tib::color_element::border, "0;38;2;33;33;33");
    colors->set_color(tib::color_element::message, "0;48;2;0;80;0;38;2;204;204;204");
    colors->set_color(tib::color_element::input, c_norm_base);
    colors->set_color(tib::color_element::input_selection, "0;30;48;2;232;204;0");
    colors->set_color(tib::color_element::input_mark, "0;7");
    colors->set_color(tib::color_element::input_scroller, "0;7;36");
    colors->set_color(tib::color_element::suggestion, "0;90");

    tib::face_definitions face_defs;
    face_defs.emplace(FACE_CTRL, "0;36;44");

    const tib::border_definition* border = nullptr;
    tib::cstring left_text;
    tib::cstring right_text;
    uint16_t left_width = 0;
    uint16_t right_width = 0;
    tib::mouse_input_mode mode = tib::mouse_input_mode::none;
    bool sgr_encoding = true;
    bool use_custom_vt_driver = false;
    bool set_mouse_input_mode = false;
    bool show_suggestions = false;
    int32_t origin_x = -1;

    for (int i = 0; i < argc; ++i)
    {
        if (_stricmp(argv[i], "--single") == 0)
        {
            tib->set_max_height(1);
            tib->set_variable_height(false);
        }
        else if (_stricmp(argv[i], "--multiline") == 0)
        {
            tib->set_max_height(3);
        }
        else if (_stricmp(argv[i], "--fixed") == 0)
        {
            tib->set_variable_height(false);
        }
        else if (_stricmp(argv[i], "--variable") == 0)
        {
            tib->set_max_height(3);
            tib->set_variable_height(true);
        }
        else if (_stricmp(argv[i], "--full-width") == 0)
        {
            tib->set_max_width(tib::int16_max);
        }
        else if (_stricmp(argv[i], "--no-border") == 0)
        {
no_border:
            tib->set_empty_face(tib::FACE_EMPTY);
            colors->set_color(tib::color_element::base, c_norm_base);
            border = nullptr;
        }
        else if (_stricmp(argv[i], "--border") == 0)
        {
            ++i;
            if (i < argc)
            {
                if (_stricmp(argv[i], "light") == 0)
                {
                    tib->set_empty_face(tib::FACE_DEFAULT);
                    colors->set_color(tib::color_element::base, c_norm_base);
                    border = &tib::c_light_border;
                }
                else if (_stricmp(argv[i], "padding") == 0)
                {
                    tib->set_empty_face(tib::FACE_DEFAULT);
                    colors->set_color(tib::color_element::base, c_padding_base);
                    border = &c_padding_border;
                }
                else if (_stricmp(argv[i], "bar-padding") == 0)
                {
                    tib->set_empty_face(tib::FACE_DEFAULT);
                    colors->set_color(tib::color_element::base, c_padding_base);
                    border = &c_bar_padding_border;
                }
                else if (_stricmp(argv[i], "first-line") == 0)
                {
                    tib->set_empty_face(tib::FACE_DEFAULT);
                    colors->set_color(tib::color_element::base, c_padding_base);
                    border = &c_first_line_border;
                }
                else if (_stricmp(argv[i], "none") == 0)
                {
                    goto no_border;
                }
                else
                {
                    fputs("Unrecognized border style.\n", stderr);
                    return 1;
                }
            }
            else
            {
                fputs("Missing border style.\n", stderr);
                return 1;
            }
        }
        else if (_stricmp(argv[i], "--rainbow") == 0)
        {
            s_use_rainbow_faces = true;
        }
        else if (_stricmp(argv[i], "--left") == 0)
        {
            ++i;
            if (i < argc)
            {
                left_text = argv[i];
                left_width = uint16_t(__wcswidth(left_text.c_str(), left_text.length()));
            }
            else
            {
                fputs("Missing left text.\n", stderr);
                return 1;
            }
        }
        else if (_stricmp(argv[i], "--right") == 0)
        {
            ++i;
            if (i < argc)
            {
                right_text = argv[i];
                right_width = uint16_t(__wcswidth(right_text.c_str(), right_text.length()));
            }
            else
            {
                fputs("Missing right text.\n", stderr);
                return 1;
            }
        }
        else if (_stricmp(argv[i], "--origin") == 0)
        {
            ++i;
            if (i < argc)
            {
                origin_x = atoi(argv[i]);
                if (origin_x <= 0)
                {
                    fputs("Invalid origin coordinate.\n", stderr);
                    return 1;
                }
            }
            else
            {
                fputs("Missing origin coordinate.\n", stderr);
                return 1;
            }
        }
        else if (_stricmp(argv[i], "--height") == 0)
        {
            ++i;
            if (i < argc)
            {
                const int height = atoi(argv[i]);
                if (height <= 0)
                {
                    fputs("Invalid height.\n", stderr);
                    return 1;
                }
                tib->set_max_height(tib::min<uint16_t>(height, 1024));
            }
            else
            {
                fputs("Missing height.\n", stderr);
                return 1;
            }
        }
        else if (_stricmp(argv[i], "--show-keys") == 0)
        {
            s_show_keys = true;
        }
        else if (_stricmp(argv[i], "--mouse") == 0)
        {
            ++i;
            if (i < argc)
            {
                set_mouse_input_mode = true;
                if (_stricmp(argv[i], "none") == 0)
                    mode = tib::mouse_input_mode::none;
                else if (_stricmp(argv[i], "vt200") == 0)
                    mode = tib::mouse_input_mode::VT200;
                else if (_stricmp(argv[i], "drag") == 0)
                    mode = tib::mouse_input_mode::DRAG;
                else if (_stricmp(argv[i], "any") == 0)
                    mode = tib::mouse_input_mode::ANY;
                else
                {
                    fputs("Unrecognized mouse input mode.\n", stderr);
                    return 1;
                }
            }
            else
            {
                fputs("Missing mouse input mode.\n", stderr);
                return 1;
            }
        }
        else if (_stricmp(argv[i], "--enc") == 0)
        {
            ++i;
            if (i < argc)
            {
                set_mouse_input_mode = true;
                if (_stricmp(argv[i], "default") == 0)
                    sgr_encoding = false;
                else if (_stricmp(argv[i], "sgr") == 0)
                    sgr_encoding = true;
                else
                {
                    fputs("Unrecognized mouse input encoding.\n", stderr);
                    return 1;
                }
            }
            else
            {
                fputs("Missing mouse input encoding.\n", stderr);
                return 1;
            }
        }
        else if (_stricmp(argv[i], "--custom-vt") == 0)
        {
            use_custom_vt_driver = true;
        }
        else if (_stricmp(argv[i], "--show-stats") == 0)
        {
            tib::show_display_manager_statistics(true);
        }
        else if (_stricmp(argv[i], "--suggest") == 0)
        {
            show_suggestions = true;
        }
        else if (_stricmp(argv[i], "-?") == 0 ||
                 _stricmp(argv[i], "--help") == 0)
        {
            fprintf(stdout, "%s", c_long_usage);
            return 0;
        }
        else
        {
            fprintf(stderr, "Unrecognized %s '%s'.\n", (argv[i][0] == '-') ? "flag" : "argument", argv[i]);
            return 1;
        }
    }

#ifdef _WIN32
    if (!use_custom_vt_driver)
        tib_host::set_console_vt_input();
#endif

    if (set_mouse_input_mode)
    {
        tib::enable_mouse_input(mode, sgr_encoding);
        // tib::term_out("\x1b[?1000h");   // VT200 Protocol for mouse input.
        // tib::term_out("\x1b[?1000l");
        // tib::term_out("\x1b[?1006h");   // SGR Encoding for mouse input.
        // tib::term_out("\x1b[?1006l");
    }

    tib::cstring border_face_scroller;
    tib::cstring face_suggestion;
    if (border)
    {
        join_colors(border_face_scroller, colors, tib::color_element::base, tib::color_element::input_scroller);
        face_defs[tib::FACE_SCROLLER] = border_face_scroller.c_str();

        join_colors(face_suggestion, colors, tib::color_element::base, tib::color_element::suggestion);
        face_defs[tib::FACE_SUGGESTION] = face_suggestion.c_str();

        if (left_text.length())
        {
            tib::cstring tmp;
            join_colors(tmp, colors, tib::color_element::base, c_bar_text_color);
            tmp.append(left_text.c_str());
            left_text = std::move(tmp);
        }
        if (right_text.length())
        {
            tib::cstring tmp;
            join_colors(tmp, colors, tib::color_element::base, "93");
            tmp.append(right_text.c_str());
            right_text = std::move(tmp);
        }
    }

    std::vector<tib::cstring> rainbow_colors;
    if (s_use_rainbow_faces)
    {
        for (uint8_t i = 0; i < std::size(c_colors); ++i)
        {
            tib::cstring tmp;
            tmp.set(colors->get_color(tib::color_element::base));
            if (!tmp.empty())
                tmp.append(";", 1);
            tmp.printf("38;2;%u;%u;%u", c_colors[i].r, c_colors[i].g, c_colors[i].b);
            static_assert(std::is_nothrow_move_constructible_v<tib::cstring>);
            static_assert(std::is_nothrow_move_assignable_v<tib::cstring>);
            rainbow_colors.emplace_back(std::move(tmp));
        }
        for (uint8_t i = 0; i < std::size(c_colors); ++i)
        {
            face_defs[FACE_RAINBOW + i] = rainbow_colors[i].c_str();
        }
    }

    tib->set_color_table(colors);
    tib->set_face_defs(&face_defs);
    tib->set_border(border);
    tib->set_left_text(left_text.c_str(), left_width);
    tib->set_right_text(right_text.c_str(), right_width);

    if (origin_x > 0)
        tib->set_origin(origin_x);
#pragma endregion // Example customizations.

    tib->initialize("hello world");
    tib->set_selection(0, uint16_t(tib->get_text().length()));

#pragma region Show custom feedback.
    tib::cstring tmp;
    tib::cstring sequence;
    tib::cstring show_sequence;
    tib::coord old_extent = tib->get_extent();
    double last_clock = tib::clock();

    auto add_feedback_to_display = [&]()
    {
        if (show_suggestions)
        {
            tib::cstring word;
            tib::cstring suggestion;
            const char* const text = tib->get_text().c_str();

            // If caret at end, get last word.
            tib::textpos_t begin_word = tib->get_caret();
            if (!tib->get_selection_state().has_selection() &&
                tib->get_caret() == tib->get_text().length())
            {
                while (begin_word > 0 && isalnum(uint8_t(text[begin_word - 1])))
                    --begin_word;
                word.set(text + begin_word);
            }

            // If last word, find first matching word.
            if (!word.empty())
            {
                size_t match = 0;
                while (match < begin_word)
                {
                    size_t len = 0;
                    while (match + len < begin_word && isalnum(uint8_t(text[match + len])))
                        ++len;
                    if (len && len > word.length())
                    {
                        if (_strnicmp(text + match, word.c_str(), word.length()) == 0)
                        {
                            suggestion.set(text + match + word.length(), len - word.length());
                            break;
                        }
                    }
                    match += len + 1;
                }
            }

            if (suggestion.empty())
            {
                tib->set_usage_text(nullptr, 0);
                tib->set_suggestion_text(nullptr);
            }
            else
            {
                tib::cstring usage;
                const bool pad = !border;
                join_colors(usage, colors, tib::color_element::base, tib::color_element::suggestion);
                usage.append("\x1b[7mN/A\x1b[27m=Insert");
                if (pad)
                    usage.append_spaces(1);
                tib->set_usage_text(usage.c_str(), pad ? 11 : 10);
                tib->set_suggestion_text(suggestion.c_str(), suggestion.length());
            }
        }

        display_feedback(*tib, show_sequence);
    };

    auto update_sequence_before_step = [&](int32_t c)
    {
        if (!s_show_keys)
            return;
        const double now = tib::clock();
        if (now - last_clock >= 0.1)
            sequence.clear();
        last_clock = now;
        if (c > 0x20 && c < 0x7f)
            sequence.printf("%c ", char(c));
        else
            sequence.printf("0x%02.2x ", c);
        while (sequence.length() > 25)
        {
            const char* p = sequence.c_str();
            while (*p && *p != ' ')
                ++p;
            while (*p && *p == ' ')
                ++p;
            tmp.set(p);
            sequence = std::move(tmp);
        }
        show_sequence.set(sequence.c_str(), sequence.length());
    };

    auto update_sequence_after_step = [&](tib::dispatch_outcome outcome)
    {
        if (!s_show_keys)
            return;
        switch (outcome)
        {
        case tib::dispatch_outcome::self_insert:
        case tib::dispatch_outcome::quoted_insert:
            if (sequence.length() == 1/*c*/ + 1/*space*/)
                sequence.clear();
            break;
        case tib::dispatch_outcome::match:
            sequence.clear();
            break;
        }

        old_extent = tib->get_extent();
    };
#pragma endregion // Show custom feedback.

    tib::binding_resolver resolver;                         // Required.
    resolver.add_target(tib);                               // Required.

    while (!tib->done())                                    // Required.
    {
                /*Custom*/  add_feedback_to_display();

        tib->display();                                     // Required.

        const int32_t c = tib::term_in();                   // Required.
        if (c < 0 || c == tib::c_input_terminal_eof)
            break;

                /*Custom*/  update_sequence_before_step(c);

        auto resolved = resolver.step(c);                   // Required.

        // An ambiguous sequence contains a complete fallback, but can still
        // become a longer binding.  The host owns this timeout policy.
        if (resolved.ambiguous() && !tib::term_in_avail(500))
            resolved = resolver.resolve_pending();

        resolved.dispatch();                                // Required.

                /*Custom*/  update_sequence_after_step(resolved.outcome);
    }

#pragma region Show custom feedback.
    if (s_show_keys)
        tib->clear_additional_lines();
#pragma endregion // Show custom feedback.

    tib->clear_additional_lines();
    tib->end_display_lf();

    return 0;
}
