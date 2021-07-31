// Copyright (c) 2017 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str_iter.h>
#include <lib/line_editor.h>
#include <terminal/terminal.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_out.h>
#include <terminal/printer.h>

#include <Windows.h>
#include <xmmintrin.h>

//------------------------------------------------------------------------------
struct handle
{
           handle()                 = default;
           handle(const handle&)    = delete;
           handle(const handle&&)   = delete;
           handle(HANDLE h)         { value = (h == INVALID_HANDLE_VALUE) ? nullptr : h; }
           ~handle()                { close(); }
           operator HANDLE () const { return value; }
    void   operator = (HANDLE h)    { close(); value = h; }
    void   close()                  { if (value != nullptr) CloseHandle(value); }
    HANDLE value = nullptr;
};



//------------------------------------------------------------------------------
class test_editor
{
public:
    void            start(const char* prompt);
    void            end();
    void            press_keys(const char* keys);

private:
    terminal        m_terminal;
    printer*        m_printer;
    line_editor*    m_editor;
    handle          m_thread;
};

//------------------------------------------------------------------------------
void test_editor::start(const char* prompt)
{
    m_terminal = terminal_create();
    m_printer = new printer(*m_terminal.out);

    line_editor::desc desc(m_terminal.in, m_terminal.out, m_printer, nullptr);
    desc.prompt = prompt;
    m_editor = line_editor_create(desc);

    auto thread = [] (void* param) -> DWORD {
        auto* self = (test_editor*)param;
        str<> tmp;
        self->m_editor->edit(tmp);
        return 0;
    };

    m_thread = CreateThread(nullptr, 0, thread, this, 0, nullptr);
}

//------------------------------------------------------------------------------
void test_editor::end()
{
    press_keys("\n");
    WaitForSingleObject(m_thread, INFINITE);
    line_editor_destroy(m_editor);
    delete m_printer;
    m_printer = nullptr;
    terminal_destroy(m_terminal);
}

//------------------------------------------------------------------------------
void test_editor::press_keys(const char* keys)
{
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD written;

    INPUT_RECORD input_record = { KEY_EVENT };
    KEY_EVENT_RECORD& key_event = input_record.Event.KeyEvent;
    key_event.bKeyDown = TRUE;
    while (int c = *keys++)
    {
        key_event.uChar.UnicodeChar = wchar_t(c);
        WriteConsoleInput(handle, &input_record, 1, &written);
    }
}



//------------------------------------------------------------------------------
class test_console
{
public:
            test_console();
            ~test_console();
    void    resize(int columns) { resize(columns, m_rows); }
    void    resize(int columns, int rows);
    int     get_columns() const { return m_columns; }
    int     get_rows() const    { return m_rows; }

private:
    handle  m_handle;
    HANDLE  m_prev_handle;
    int     m_columns;
    int     m_rows;
};

//------------------------------------------------------------------------------
test_console::test_console()
: m_prev_handle(GetStdHandle(STD_OUTPUT_HANDLE))
{
    m_handle = CreateConsoleScreenBuffer(GENERIC_WRITE|GENERIC_READ, 3, nullptr,
        CONSOLE_TEXTMODE_BUFFER, nullptr);

    COORD cursor_pos = { 0, 0 };
    SetConsoleCursorPosition(m_handle, cursor_pos);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle , &csbi);
    m_columns = csbi.dwSize.X;
    m_rows = csbi.dwSize.Y;

    SetConsoleActiveScreenBuffer(m_handle);
    SetStdHandle(STD_OUTPUT_HANDLE, m_handle);
}

//------------------------------------------------------------------------------
test_console::~test_console()
{
    SetStdHandle(STD_OUTPUT_HANDLE, m_prev_handle);
}

//------------------------------------------------------------------------------
void test_console::resize(int columns, int rows)
{
    SMALL_RECT rect = { 0, 0, short(columns - 1), short(rows - 1) };
    COORD size = { short(columns), short(rows) };

    SetConsoleWindowInfo(m_handle, TRUE, &rect);
    SetConsoleScreenBufferSize(m_handle, size);
    SetConsoleWindowInfo(m_handle, TRUE, &rect);

    m_columns = columns;
    m_rows = rows;
}



//------------------------------------------------------------------------------
class stepper
{
public:
                    stepper(int timeout_ms);
                    ~stepper();
    bool            step();

private:
    enum state
    {
        state_running,
        state_step,
        state_paused,
        state_quit,
    };

    void            run_input_thread();
    handle          m_input_thread;
    volatile state  m_state;
    int             m_timeout_ms;
};

//------------------------------------------------------------------------------
stepper::stepper(int timeout_ms)
: m_state(state_running)
, m_timeout_ms(timeout_ms)
{
    auto thunk = [] (void* param) -> DWORD {
        auto* self = (stepper*)param;
        self->run_input_thread();
        return 0;
    };

    m_input_thread = CreateThread(nullptr, 0, thunk, this, 0, nullptr);
}

//------------------------------------------------------------------------------
stepper::~stepper()
{
    TerminateThread(m_input_thread, 0);
}

//------------------------------------------------------------------------------
void stepper::run_input_thread()
{
/*
    HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    while (true)
    {
        DWORD count;
        INPUT_RECORD record;
        if (!ReadConsoleInputW(stdin_handle, &record, 1, &count))
        {
            m_state = state_quit;
            break;
        }

        if (record.EventType != KEY_EVENT)
            continue;

        const auto& key_event = record.Event.KeyEvent;
        if (!key_event.bKeyDown)
            continue;

        switch (key_event.wVirtualKeyCode)
        {
        case VK_ESCAPE: m_state = state_quit; break;
        case VK_SPACE:  m_state = state_step; break;
        }
    }
*/
}

//------------------------------------------------------------------------------
bool stepper::step()
{
    switch (m_state)
    {
    case state_paused:  while (m_state == state_paused) Sleep(10); break;
    case state_running: Sleep(m_timeout_ms); break;
    case state_step:    m_state = state_paused; break;
    }

    return (m_state != state_quit);
}



//------------------------------------------------------------------------------
class runner
{
public:
                        runner();
    void                go();

private:
    bool                step();
    void                ecma48_test();
    void                line_test();
    test_console        m_console;
    stepper             m_stepper;
};

//------------------------------------------------------------------------------
runner::runner()
: m_stepper(200)
{
    srand(0xa9e);
    m_console.resize(80, 25);
}

//------------------------------------------------------------------------------
void runner::go()
{
    ecma48_test();
    line_test();
}

//------------------------------------------------------------------------------
bool runner::step()
{
    return m_stepper.step();
}

//------------------------------------------------------------------------------
void runner::ecma48_test()
{
#define CSI(x) "\x1b[" #x
    terminal terminal = terminal_create();
    terminal_out& output = *terminal.out;
    output.begin();

    // Clear screen after
    output.write(CSI(41m) CSI(3;3H) "X" CSI(J), -1);
    if (!step())
        return;

    // Clear screen before
    output.write(CSI(42m) CSI(5;3H) "X\b\b" CSI(1J), -1);
    if (!step())
        return;

    // Clear screen all
    output.write(CSI(43m) CSI(2J), -1);
    if (!step())
        return;

    // Clear line after
    output.write(CSI(44m) CSI(4;4H) "X" CSI(K), -1);
    if (!step())
        return;

    // Clear line before
    output.write(CSI(45m) CSI(5;4H) "X\b\b" CSI(1K), -1);
    if (!step())
        return;

    // All line
    output.write("\n" CSI(46m) CSI(2K), -1);
    if (!step())
        return;

    output.write(CSI(0m) CSI(1;1H) CSI(J), -1);
    output.end();
    terminal_destroy(terminal);
#undef CSI
}

//------------------------------------------------------------------------------
void runner::line_test()
{
    test_editor editor;

#if 1
    editor.start("prompt\n->$ ");
#else
    editor.start("prompt$ ");
#endif // 0
    const char word[] = "9876543210";
    for (int i = 0; i < 100;)
    {
        editor.press_keys("_");
        int j = rand() % (sizeof(word) - 2);
        editor.press_keys(word + j);
        i += sizeof(word) - j;
    }

    int columns = m_console.get_columns();
    for (; columns > 16 && step(); --columns)
        m_console.resize(columns);

    for (; columns < 60 && step(); ++columns)
        m_console.resize(columns);

    step();
    editor.end();
}



//------------------------------------------------------------------------------
int draw_test(int, char**)
{
    runner().go();
    return 0;
}
