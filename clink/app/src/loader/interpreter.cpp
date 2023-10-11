// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/usage.h"

#include <core/os.h>
#include <core/log.h>
#include <core/settings.h>
#include <lua/lua_state.h>
#include <terminal/terminal.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>
#include <getopt.h>
#include "version.h"

extern "C" {
#define lua_c
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
};

#include <signal.h>
#include <vector>

//------------------------------------------------------------------------------
#if !defined(LUA_PROMPT)
#define LUA_PROMPT      "> "
#define LUA_PROMPT2     ">> "
#endif

#if !defined(LUA_PROGNAME)
#define LUA_PROGNAME    "lua"
#endif

#if !defined(LUA_MAXINPUT)
#define LUA_MAXINPUT    512
#endif

#if !defined(LUA_INIT)
#define LUA_INIT        "LUA_INIT"
#endif

#define LUA_INITVERSION  \
    LUA_INIT "_" LUA_VERSION_MAJOR "_" LUA_VERSION_MINOR

//------------------------------------------------------------------------------
static char const *progname = LUA_PROGNAME;

#define lua_stdin_is_tty()  _isatty(_fileno(stdin))

#define lua_readline(L,b,p) \
        ((void)L, fputs(p, stdout), fflush(stdout),  /* show prompt */ \
        fgets(b, LUA_MAXINPUT, stdin) != NULL)  /* get line */
#define lua_saveline(L,idx)	{ (void)L; (void)idx; }
#define lua_freeline(L,b)	{ (void)L; (void)b; }

//------------------------------------------------------------------------------
static lua_State *globalL = nullptr;

static void lstop (lua_State *L, lua_Debug *ar) {
  (void)ar;  /* unused arg. */
  lua_sethook(L, NULL, 0, 0);
  luaL_error(L, "interrupted!");
}

static void laction (int32 i) {
  signal(i, SIG_DFL); /* if another SIGINT happens before lstop,
                              terminate process (default action) */
  lua_sethook(globalL, lstop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
}

static int32 docall (lua_State *L, int32 narg, int32 nres) {
  int32 status;
  int32 base = lua_gettop(L) - narg;  /* function index */
  lua_getglobal(L, "_error_handler");
  lua_insert(L, base);  /* put it under chunk and args */
  globalL = L;  /* to be available to 'laction' */
  signal(SIGINT, laction);
  status = lua_pcall(L, narg, nres, base);
  signal(SIGINT, SIG_DFL);
  lua_remove(L, base);  /* remove _error_handler function */
  return status;
}

//------------------------------------------------------------------------------
// Various functions lifted verbatim from lua.c.
#include "interpreter_lua_c.h"

//------------------------------------------------------------------------------
int32 interpreter(int32 argc, char** argv)
{
    static const char* help_usage = "Usage: interpreter [options] [script]\n";

    static const struct option options[] = {
        { "debug",      required_argument,  nullptr, 'D' },
        { "log",        required_argument,  nullptr, 'L' },
        { "version",    no_argument,        nullptr, 'v' },
        { "help",       no_argument,        nullptr, 'h' },
        { nullptr, 0, nullptr, 0 }
    };

    static const char* const help[] = {
        "-e <stat>",            "Execute string \"stat\".",
        "-i",                   "Enter interactive mode after executing script.",
        "-l <name>",            "Require library \"name\".",
        "-v, --version",        "Show Lua version information.",
        "-D, --debug",          "Enable Lua debugging (-DD to break on errors).",
        "-E",                   "Ignore environment variables.",
        "-L, --log <file>",     "Write log output to the specified file.",
        "-h, --help",           "Shows this help text.",
        nullptr
    };

    struct run_arg {
        int32 opt;
        const char* arg;
    };
    std::vector<run_arg> run_args;

    // Parse arguments
    int32 enable_debugging = 0;
    bool ignore_env = false;
    bool go_interactive = false;
    bool show_version = false;
    bool execute_string = false;
    const char* log_file = nullptr;
    int32 i;
    int32 ret = 1;
    while ((i = getopt_long(argc, argv, "+?hDEL:ive:l:", options, nullptr)) != -1)
    {
        if (i == '\0')
        {
            // Interpret "-" by itself as a filename and stop parsing options.
            // The "-" filename will end up reading from stdin.
            optind--;
            break;
        }

        switch (i)
        {
        case 'D':
            enable_debugging++;
            break;
        case 'E':
            ignore_env = true;
            break;
        case 'i':
            go_interactive = true;
            break;
        case 'v':
            show_version = true;
            break;
        case 'e':
        case 'l':
            run_args.emplace_back(run_arg {i, optarg});
            break;
        case 'L':
            log_file = optarg;
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
            // puts("By default this starts a loop of calling editor->edit() directly.  Run\n"
            //      "'clink testbed --hook' to hook the ReadConsoleW API and start a loop of\n"
            //      "calling that, as though it were injected in cmd.exe (editing works, but\n"
            //      "of course nothing happens with the input since the host isn't cmd.exe).");
            return ret;
        }
    }

    if (log_file && *log_file)
    {
        new file_logger(log_file);

        SYSTEMTIME now;
        GetLocalTime(&now);
        LOG("---- %04u/%02u/%02u %02u:%02u:%02u.%03u -------------------------------------------------",
            now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

        LOG("Clink version %s (%s)", CLINK_VERSION_STR, AS_STR(ARCHITECTURE_NAME));
        LOG(LUA_COPYRIGHT);
    }

    settings::find("lua.traceback_on_error")->set("true");
    if (enable_debugging)
    {
        extern bool g_force_load_debugger;
        g_force_load_debugger = true;
        if (enable_debugging > 1)
            settings::find("lua.break_on_error")->set("true");
    }

    settings::load("nul");

    terminal term = terminal_create(nullptr, false/*cursor_visibility*/);
    printer printer(*term.out);
    printer_context prt(term.out, &printer);
    term.in->begin();

    extern void init_standalone_textlist(terminal& term);
    init_standalone_textlist(term);

    int32 status = LUA_OK;
    lua_state_flags flags = lua_state_flags::interpreter;
    if (ignore_env)
        flags |= lua_state_flags::no_env;
    lua_state lua(flags);
    lua_State *L = lua.get_state();

    if (show_version)
    {
        printf("Clink version %s (%s)", CLINK_VERSION_STR, AS_STR(ARCHITECTURE_NAME));
        puts("");
    }
    if (show_version || go_interactive)
    {
        print_version();
    }

    if (!ignore_env)
        status = handle_luainit(L);

    for (size_t i = 0; status == LUA_OK && i < run_args.size(); ++i)
    {
        const run_arg& r(run_args[i]);
        execute_string |= (r.opt == 'e');
        if (r.opt == 'e')
            status = dostring(L, r.arg, "=(command line)");
        else if (r.opt == 'l')
            status = dolibrary(L, r.arg);
    }

    const int32 script = (argv[optind]) ? optind : 0;
    if (status == LUA_OK)
    {
        if (script)
        {
            const char *fname;
            int32 narg = getargs(L, argv, script); /* collect arguments */
            lua_setglobal(L, "arg");
            fname = argv[script];
            if (strcmp(fname, "-") == 0 && strcmp(argv[script - 1], "--") != 0)
                fname = nullptr; /* stdin */
            status = luaL_loadfile(L, fname);
            lua_insert(L, -(narg + 1));
            if (status == LUA_OK)
                status = docall(L, narg, LUA_MULTRET);
            else
                lua_pop(L, narg);
            report(L, status);
        }
    }

    if (status == LUA_OK)
    {
        ret = 0;
        if (go_interactive)
            dotty(L);
        else if (!script && !execute_string && !show_version)
        {
            if (lua_stdin_is_tty())
            {
                print_version();
                dotty(L);
            }
            else
            {
                // Execute stdin as a file.
                dofile(L, nullptr);
            }
        }
    }

    term.in->end();

    return ret;
}
