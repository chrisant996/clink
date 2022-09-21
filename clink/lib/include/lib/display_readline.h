#pragma once

class line_buffer;

extern void reset_readline_display();
extern void refresh_terminal_size();
extern void display_readline();
extern void resize_readline_display(const char* prompt, const line_buffer& buffer, const char* _prompt, const char* _rprompt);
extern unsigned int get_readline_display_top_offset();

//------------------------------------------------------------------------------
#define FACE_NORMAL     '0'
#define FACE_STANDOUT   '1'
#define FACE_INVALID    ((char)1)

//------------------------------------------------------------------------------
class display_accumulator
{
public:
                    display_accumulator();
                    ~display_accumulator();
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
