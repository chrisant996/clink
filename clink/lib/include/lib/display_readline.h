#pragma once

#include <core/str.h>
#include <vector>

class line_buffer;
typedef struct _history_expansion history_expansion;

extern void reset_readline_display();
extern void refresh_terminal_size();
extern void display_readline();
extern void set_history_expansions(history_expansion* list=nullptr);
extern void resize_readline_display(const char* prompt, const line_buffer& buffer, const char* _prompt, const char* _rprompt);
extern unsigned int get_readline_display_top_offset();

//------------------------------------------------------------------------------
#define BIT_PROMPT_PROBLEM          (0x01)
#define BIT_PROMPT_MAYBE_PROBLEM    (0x02)
struct prompt_problem_details
{
    int             type;
    str_moveable    code;
    int             offset;
};
extern int prompt_contains_problem_codes(const char* prompt, std::vector<prompt_problem_details>* out=nullptr);

//------------------------------------------------------------------------------
#define FACE_INVALID        ((char)1)
#define FACE_SPACE          ' '
#define FACE_NORMAL         '0'
#define FACE_STANDOUT       '1'

#define FACE_INPUT          '2'
#define FACE_MODMARK        '*'
#define FACE_MESSAGE        '('
#define FACE_SCROLL         '<'
#define FACE_SELECTION      '#'
#define FACE_SUGGESTION     '-'
#define FACE_HISTEXPAND     '!'

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
class display_accumulator
{
public:
                    display_accumulator();
                    ~display_accumulator();
    void            split();
    void            flush();
private:
    void            restore();
    static void     fwrite_proc(FILE*, const char*, int);
    static void     fflush_proc(FILE*);
    void (*m_saved_fwrite)(FILE*, const char*, int) = nullptr;
    void (*m_saved_fflush)(FILE*) = nullptr;
    bool            m_active = false;
    static int      s_nested;
};
