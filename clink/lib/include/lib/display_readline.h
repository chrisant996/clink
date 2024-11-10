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

extern char* rl_rprompt;
void rl_set_rprompt(const char* rprompt);

extern "C" void reset_display_readline(void);
void refresh_terminal_size();
void clear_to_end_of_screen_on_next_display();
void display_readline();
void set_history_expansions(history_expansion* list=nullptr);
void force_comment_row(const char* text);
void resize_readline_display(const char* prompt, const line_buffer& buffer, const char* _prompt, const char* _rprompt);
bool translate_xy_to_readline(uint32 x, uint32 y, int32& pos, bool clip=false);
COORD measure_readline_display(const char* prompt=nullptr, const char* buffer=nullptr, uint32 len=-1);

void clear_comment_row();
void defer_clear_lines(uint32 prompt_lines, bool transient);

extern bool g_display_manager_no_comment_row;

//------------------------------------------------------------------------------
#ifdef USE_SUGGESTION_HINT_INLINE
#define DOC_HYPERLINK_AUTOSUGGEST "https://chrisant996.github.io/clink/clink.html#gettingstarted_autosuggest"
#endif

//------------------------------------------------------------------------------
#ifdef USE_SUGGESTION_HINT_INLINE
#define STR_SUGGESTION_HINT_INLINE      "    Right=Insert Suggestion"
#define IDX_SUGGESTION_KEY_BEGIN        (-23)
#define IDX_SUGGESTION_KEY_END          (-18)
#define IDX_SUGGESTION_LINK_TEXT        (-17)
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
#define FACE_INVALID        ((char)1)
#define FACE_SPACE          ' '
#define FACE_NORMAL         '0'
#define FACE_STANDOUT       '1'

// WARNING:  PRE-DEFINED FACE IDS MUST BE IN 1..127; THE RANGE 128..255 IS FOR
// CUSTOM LUA CLASSIFICATION FACE IDS.

#define FACE_INPUT          '2'
#define FACE_MODMARK        '*'
#define FACE_MESSAGE        '('
#define FACE_SCROLL         '<'
#define FACE_SELECTION      '#'
#define FACE_HISTEXPAND     '!'
#define FACE_SUGGESTION     '-'
#ifdef USE_SUGGESTION_HINT_INLINE
#define FACE_SUGGESTIONKEY  char(0x1a)  // In OEM 437 codepage, 0x1a is a right-arrow character.
#define FACE_SUGGESTIONLINK char(0x15)  // In OEM 437 codepage, 0x15 is a section symbol, which looks similar to a link.
#endif

#define FACE_OTHER          'o'
#define FACE_UNRECOGNIZED   'u'
#define FACE_EXECUTABLE     'x'
#define FACE_COMMAND        'c'
#define FACE_ALIAS          'd'
#define FACE_ARGMATCHER     'm'
#define FACE_ARGUMENT       'a'
#define FACE_FLAG           'f'
#define FACE_NONE           'n'

//------------------------------------------------------------------------------
// The display_accumulator can be disabled:
// In release builds with `set CLINK_NO_DISPLAY_ACCUMULATOR=1`.
// Or in debug builds also with `set DEBUG_NO_DISPLAY_ACCUMULATOR=1`.
class display_accumulator
{
public:
                    display_accumulator();
                    ~display_accumulator();
    static void     flush();
private:
    static void     fwrite_proc(FILE*, const char*, int32);
    static void     fflush_proc(FILE*);
    static void (*s_saved_fwrite)(FILE*, const char*, int32);
    static void (*s_saved_fflush)(FILE*);
    static int32    s_nested;
    static bool     s_active;
};
