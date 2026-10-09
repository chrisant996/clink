#pragma once

#include <core/base.h>
#include <core/str.h>
#include <vector>

// Define USE_SUGGESTION_HINT_INLINE to show "[Right]=Insert Suggestion" (with a hyperlink)
// inline when there's suggestion text.
// Define RIGHT_ALIGN_SUGGESTION_HINT to show the hint right aligned, dropping
// down a line if it doesn't fit.
#define USE_SUGGESTION_HINT_INLINE
#define RIGHT_ALIGN_SUGGESTION_HINT

class line_buffer;
typedef struct _history_expansion history_expansion;

void fixup_prompt(str_moveable& prompt);
void fixup_rprompt(str_moveable& rprompt);
bool has_modmark();

extern "C" void init_display_readline(void);
extern "C" void uninit_display_readline(void);
extern "C" int32 is_display_readline_initialized(void);

#ifdef DEBUG
void ignore_column_in_uninit_display_readline();
#endif

extern "C" void reset_display_readline(void);
void move_to_caret_position(bool force_column=false);
extern "C" void move_to_end_of_display(int cr);
int get_input_height();
int get_relative_cursor_row();
int get_relative_cursor_column();
void refresh_terminal_size();
void clear_to_end_of_screen_on_next_display();
void display_readline();
void want_redisplay_readline();
void maybe_redisplay_readline();
void force_redisplay_readline();
void set_history_expansions(history_expansion* list=nullptr);
void force_comment_row(const char* text);
void resize_readline_display(const char* prompt, const line_buffer& buffer, const char* _prompt, const char* _rprompt);
bool translate_xy_to_readline(uint32 x, uint32 y, int32& pos, bool clip=false);
COORD measure_readline_display(const char* prompt=nullptr, const char* buffer=nullptr, uint32 len=-1);
SHORT calc_max_y_scroll_pos(SHORT y);

extern "C" void clear_comment_row();
int32 count_prompt_lines(const char* prompt_prefix);
void defer_clear_lines(uint32 prompt_lines, bool transient);

extern bool g_display_manager_no_comment_row;

//------------------------------------------------------------------------------
#ifdef USE_SUGGESTION_HINT_INLINE
#define DOC_HYPERLINK_AUTOSUGGEST "https://chrisant996.github.io/clink/clink.html#gettingstarted_autosuggest"
#endif

//------------------------------------------------------------------------------
#define BIT_PROMPT_PROBLEM          (0x01)
#define BIT_PROMPT_MAYBE_PROBLEM    (0x02)
struct prompt_problem_details
{
    int32           type;
    str_moveable    code;
    int32           offset;
};
int32 prompt_contains_problem_codes(const char* prompt, std::vector<prompt_problem_details>* out=nullptr);

//------------------------------------------------------------------------------
constexpr char FACE_INVALID         = ((char)1);
constexpr char FACE_SPACE           = ' ';
constexpr char FACE_NORMAL          = '0';
constexpr char FACE_STANDOUT        = '1';

// WARNING:  PRE-DEFINED FACE IDS MUST BE IN 1..127; THE RANGE 128..255 IS FOR
// CUSTOM LUA CLASSIFICATION FACE IDS.

constexpr char FACE_INPUT           = '2';
constexpr char FACE_MODMARK         = '*';
constexpr char FACE_MESSAGE         = '(';
constexpr char FACE_SCROLL          = '<';
constexpr char FACE_SELECTION       = '#';
constexpr char FACE_HISTEXPAND1     = '!';
constexpr char FACE_HISTEXPAND2     = '?';
constexpr char FACE_SUGGESTION      = '-';
#ifdef USE_SUGGESTION_HINT_INLINE
constexpr char FACE_SUGGESTIONKEY   = char(0x1a); // In OEM 437 codepage, 0x1a is a right-arrow character.
constexpr char FACE_SUGGESTIONLINK  = char(0x15); // In OEM 437 codepage, 0x15 is a section symbol, which looks similar to a link.
#endif

constexpr char FACE_OTHER           = 'o';
constexpr char FACE_UNRECOGNIZED    = 'u';
constexpr char FACE_EXECUTABLE      = 'x';
constexpr char FACE_COMMAND         = 'c';
constexpr char FACE_ALIAS           = 'd';
constexpr char FACE_ARGMATCHER      = 'm';
constexpr char FACE_ARGUMENT        = 'a';
constexpr char FACE_FLAG            = 'f';
constexpr char FACE_NONE            = 'n';

//------------------------------------------------------------------------------
// The display_accumulator can be disabled:
// In release builds with `set CLINK_NO_DISPLAY_ACCUMULATOR=1`.
// Or in debug builds also with `set DEBUG_NO_DISPLAY_ACCUMULATOR=1`.
void init_display_accumulator();

//------------------------------------------------------------------------------
extern FILE* const thunk_null_stream;
extern FILE* const thunk_in_stream;
extern FILE* const thunk_out_stream;
void terminal_fwrite_thunk(FILE* stream, const char* chars, int32 char_count);
void terminal_log_fwrite_thunk(FILE* stream, const char* chars, int32 char_count);
void terminal_fflush_thunk(FILE* stream);
void init_rl_terminal_thunks();

//------------------------------------------------------------------------------
// Transient prompt context.
class transient_prompt_context
{
public:
                    transient_prompt_context(bool is_transient);
                    ~transient_prompt_context() = default;
private:
    rollback<bool>  m_rollback;
};
