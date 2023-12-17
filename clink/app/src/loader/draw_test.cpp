// Copyright (c) 2017 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/usage.h"

#include <core/str_iter.h>
#include <core/settings.h>
#include <lib/line_editor.h>
#include <terminal/terminal.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_out.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>

#include <getopt.h>

//#define INIT_READLINE
//#define LOAD_SETTINGS

#if defined(INIT_READLINE) || defined(LOAD_SETTINGS)
#include <utils/app_context.h>
#endif

extern "C" {
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
};

//------------------------------------------------------------------------------
#define CSI(x) "\x1b[" #x
static const char SPC_msg[] = CSI(0;7m) "SPC" CSI(0m) "=continue, ";
static const char RET_msg[] = CSI(0;7m) "RET" CSI(0m) "=stop";
#undef CSI



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
    bool            done() { return m_done; }

private:
    static DWORD WINAPI thread_proc(void* param);

    terminal        m_terminal;
    printer*        m_printer;
    line_editor*    m_editor;
    handle          m_thread;
    volatile bool   m_done = true;

    printer_context* m_printer_context;
    console_config* m_cc;
};

//------------------------------------------------------------------------------
DWORD WINAPI test_editor::thread_proc(void* param)
{
    auto* self = (test_editor*)param;
    str<> tmp;
    self->m_editor->edit(tmp);
    self->m_done = true;
    return 0;
}

//------------------------------------------------------------------------------
void test_editor::start(const char* prompt)
{
#ifdef DEBUG
    settings::TEST_set_ever_loaded();
#endif

    m_terminal = terminal_create();
    m_printer = new printer(*m_terminal.out);
    m_printer_context = new printer_context(m_terminal.out, m_printer);
    m_cc = new console_config();

#ifdef INIT_READLINE
    // initialise_readline() needs a printer_context to be active.
    str_moveable state_dir;
    str_moveable default_inputrc;
    app_context::get()->get_state_dir(state_dir);
    app_context::get()->get_default_init_file(default_inputrc);
    extern void initialise_readline(const char* shell_name, const char* state_dir, const char* default_inputrc);
    initialise_readline("clink", state_dir.c_str(), default_inputrc.c_str());
#endif

    line_editor::desc desc(m_terminal.in, m_terminal.out, m_printer, nullptr);
    desc.prompt = prompt;
    m_editor = line_editor_create(desc);

    assert(g_printer);
    g_printer->print(RET_msg);
    g_printer->print("\n");

    m_done = false;
    m_thread = CreateThread(nullptr, 0, thread_proc, this, 0, nullptr);
}

//------------------------------------------------------------------------------
void test_editor::end()
{
    if (!m_done)
        press_keys("\n");
    WaitForSingleObject(m_thread, INFINITE);
    line_editor_destroy(m_editor);
    delete m_cc;
    delete m_printer_context;
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
    while (int32 c = *keys++)
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
    void    resize(int32 columns) { resize(columns, m_rows); }
    void    resize(int32 columns, int32 rows);
    int32   get_columns() const { return m_columns; }
    int32   get_rows() const    { return m_rows; }

private:
    handle  m_handle;
    HANDLE  m_prev_handle;
    int32   m_columns;
    int32   m_rows;
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
void test_console::resize(int32 columns, int32 rows)
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
                    stepper(int32 timeout_ms);
                    ~stepper();
    void            init(bool pause=false);
    void            reset();
    bool            paused();
    bool            done();
    bool            step();

private:
    enum state
    {
        state_running,
        state_step,
        state_paused,
        state_quit,
    };

    static DWORD WINAPI thunk(void* param);
    void            run_input_thread();
    handle          m_input_thread;
    volatile state  m_state;
    int32           m_timeout_ms;
};

//------------------------------------------------------------------------------
DWORD WINAPI stepper::thunk(void* param)
{
    auto* self = (stepper*)param;
    self->run_input_thread();
    return 0;
}

//------------------------------------------------------------------------------
stepper::stepper(int32 timeout_ms)
: m_input_thread(nullptr)
, m_state(state_running)
, m_timeout_ms(timeout_ms)
{
}

//------------------------------------------------------------------------------
stepper::~stepper()
{
    reset();
}

//------------------------------------------------------------------------------
void stepper::init(bool pause)
{
    if (!m_input_thread)
    {
        m_state = pause ? state_paused : state_running;
        m_input_thread = CreateThread(nullptr, 0, thunk, this, 0, nullptr);
    }
}

//------------------------------------------------------------------------------
void stepper::reset()
{
    if (m_input_thread)
    {
        TerminateThread(m_input_thread, 0);
        m_input_thread = nullptr;
        m_state = state_running;
    }
}

//------------------------------------------------------------------------------
void stepper::run_input_thread()
{
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
        case VK_ESCAPE:
        case VK_RETURN:
            m_state = state_quit;
            break;
        case VK_SPACE:
            m_state = state_step;
            break;
        }
    }
}

//------------------------------------------------------------------------------
bool stepper::paused()
{
    return m_state == state_paused;
}

//------------------------------------------------------------------------------
bool stepper::done()
{
    return m_state == state_quit;
}

//------------------------------------------------------------------------------
bool stepper::step()
{
    switch (m_state)
    {
    case state_paused:  while (m_state == state_paused) Sleep(10); break;
    case state_running: Sleep(m_timeout_ms); break;
    }

    if (m_state == state_step)
        m_state = state_paused;

    return (m_state != state_quit);
}



//------------------------------------------------------------------------------
class runner
{
public:
                        runner(int32 width=0, int32 timeout_ms=0);
    void                go(bool pause=false);

private:
    bool                step();
    bool                ecma48_test();
    bool                line_test();
    test_console        m_console;
    stepper             m_stepper;
};

//------------------------------------------------------------------------------
runner::runner(int32 width, int32 timeout_ms)
: m_stepper((timeout_ms <= 0) ? 200 : timeout_ms)
{
    srand(0xa9e);
    width = (width <= 0) ? 80 : min<int32>(max<int32>(width, 20), 80);
    m_console.resize(width, 25);
}

//------------------------------------------------------------------------------
void runner::go(bool pause)
{
    m_stepper.init(pause);

    if (!ecma48_test())
        return;

    m_stepper.reset();

    if (!line_test())
        return;
}

//------------------------------------------------------------------------------
bool runner::step()
{
    return m_stepper.step();
}

//------------------------------------------------------------------------------
bool runner::ecma48_test()
{
#define CSI(x) "\x1b[" #x
    terminal terminal = terminal_create();
    terminal_out& output = *terminal.out;
    output.begin();

    if (m_stepper.paused())
        output.write(SPC_msg);
    output.write(RET_msg);

    if (!step())
        return false;

    // Clear screen after
    output.write(CSI(3;3H) "CSI J -> " CSI(97;41m) "X" CSI(J));
    if (!step())
        return false;

    // Clear screen before
    output.write(CSI(97;42m) CSI(5;3H) "X\b\b" CSI(1J) CSI(2C) " <- CSI 1J");
    if (!step())
        return false;

    // Clear screen all
    output.write(CSI(30;43m) CSI(2J) CSI(2;4H) "CSI 2J");
    if (!step())
        return false;

    // Clear line after
    output.write(CSI(4;4H) "CSI K -> " CSI(97;44m) "X" CSI(K));
    if (!step())
        return false;

    // Clear line before
    output.write(CSI(97;45m) CSI(5;4H) "X\b\b" CSI(1K) CSI(2C) " <- CSI 1K");
    if (!step())
        return false;

    // All line
    output.write(CSI(0m) "\n\n" CSI(30;43m) CSI(4G) "CSI 2K (next line)\n" CSI(30;46m) CSI(2K));
    if (!step())
        return false;

    output.write(CSI(0m) CSI(1;1H) CSI(J));
    output.end();
    terminal_destroy(terminal);
    return true;
#undef CSI
}

//------------------------------------------------------------------------------
bool runner::line_test()
{
    test_editor editor;

    editor.start("\x1b[m\n"
                 "\x1b[32;7mprompt\x1b[27m "
                 "\x1b[44;96mcontext\x1b[m "
                 "\x1b[1m\xe2\x80\xa2\x1b[m "
                 "\x1b[3;35minformation\x1b[m "
                 "\x1b[1;7m$\x1b[m ");

    const char word[] = "9876543210";
    for (int32 i = 0; i < 100;)
    {
        editor.press_keys("_");
        int32 j = rand() % (sizeof(word) - 2);
        editor.press_keys(word + j);
        i += sizeof(word) - j;
    }

    int32 columns = m_console.get_columns();
    for (; columns > 16 && !editor.done() && step(); --columns)
        m_console.resize(columns);

    for (; columns < 60 && !editor.done() && step(); ++columns)
        m_console.resize(columns);

    bool ret = !editor.done() && step();

    editor.end();
    return ret;
}



//------------------------------------------------------------------------------
int32 draw_test(int32 argc, char** argv)
{
    static const char* help_usage = "Usage: drawtest [options]\n";

    static const struct option options[] = {
        { "pause",      no_argument,        nullptr, 'p' },
        { "speed",      required_argument,  nullptr, 's' },
        { "width",      required_argument,  nullptr, 'w' },
        { "emulation",  required_argument,  nullptr, 'e' },
        { "help",       no_argument,        nullptr, 'h' },
        {}
    };

    static const char* const help[] = {
        "-p, --pause",              "Pause before starting.",
        "-s, --speed <ms>",         "Step speed in milliseconds.",
        "-w, --width <cols>",       "Initial width for the terminal.",
        "-e, --emulation <mode>",   "Emulation mode (native, emulate, auto).",
        "-h, --help",               "Shows this help text.",
        nullptr
    };

    bool pause = false;
    int32 timeout_ms = 0;
    int32 width = 0;
    str_moveable emulation("emulate");

    int32 i;
    int32 ret = 1;
    while ((i = getopt_long(argc, argv, "?hps.w.e.", options, nullptr)) != -1)
    {
        switch (i)
        {
        case 'p':
            pause = true;
            break;

        case 's':
            timeout_ms = atoi(optarg);
            break;

        case 'w':
            width = atoi(optarg);
            break;

        case 'e':
            emulation = optarg;
            break;

        case '?':
        case 'h':
            ret = 0;
            // fall through
        default:
            puts_clink_header();
            puts(help_usage);
            puts("Options:");
            puts_help(help);
            puts("This starts some drawing tests.");
            return ret;
        }
    }

    ret = 0;

#ifdef LOAD_SETTINGS
    str_moveable settings_file;
    str_moveable default_settings_file;
    app_context::get()->get_settings_path(settings_file);
    app_context::get()->get_default_settings_file(default_settings_file);
    settings::load(settings_file.c_str(), default_settings_file.c_str());
#endif

    settings::find("terminal.emulation")->set("auto");
    settings::find("terminal.emulation")->set(emulation.c_str());

    runner(width, timeout_ms).go(pause);
    return ret;
}
