#pragma once

extern void reset_readline_display();
extern void display_readline();
extern unsigned int get_readline_display_top();

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
