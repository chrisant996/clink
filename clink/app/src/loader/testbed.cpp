// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "dll/dll.h"
#include "loader/loader.h"

#include <core/os.h>
#include <core/str_compare.h>
#include <core/settings.h>
#include <lib/line_editor.h>
#include <lib/match_generator.h>
#include <lib/recognizer.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_state.h>
#include <lua/lua_task_manager.h>
#include <terminal/terminal.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>
#include <utils/app_context.h>
#include <utils/usage.h>
#include <getopt.h>

extern "C" {
#include <readline/history.h>
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
}

//------------------------------------------------------------------------------
extern void init_readline_funmap();

//------------------------------------------------------------------------------
typedef const char* two_strings[2];
static void bind_keyseq_list(const two_strings* list, Keymap map)
{
    for (int32 i = 0; list[i][0]; ++i)
        rl_bind_keyseq_in_map(list[i][0], rl_named_function(list[i][1]), map);
}

//------------------------------------------------------------------------------
static void init_readline_testbed()
{
    // Install signal handlers so that Readline doesn't trigger process exit
    // in response to Ctrl+C or Ctrl+Break.
    rl_catch_signals = 1;
    _rl_echoctl = 1;
    _rl_intr_char = CTRL('C');

    // Override some defaults.
    _rl_bell_preference = VISIBLE_BELL;     // Because audible is annoying.
    rl_complete_with_tilde_expansion = 1;   // Since CMD doesn't understand tilde.

    // Some basic key bindings.
    static constexpr const char* const general_key_binds[][2] = {
        // Help.
        { "\\M-h",          "clink-show-help" },         // alt-h
        { "\\e?",           "clink-what-is" },           // alt-? (alt-shift-/)
        { "\\e[27;8;191~",  "clink-show-help" },         // ctrl-alt-? (ctrl-alt-shift-/)
        // Special commands.
        { "\\e[27;5;32~",   "clink-select-complete" },   // ctrl-space
        { "\\e[1;7A",       "clink-popup-history" },     // alt-ctrl-up
        // Navigation.
        { "\\C-f",          "forward-char" },            // ctrl-f (because of suggestions)
        { "\\e[1;3C",       "forward-word" },            // alt-right
        { "\\e[1;5D",       "backward-word" },           // ctrl-left
        { "\\e[1;5C",       "forward-word" },            // ctrl-right
        { "\\e[C",          "forward-char" },            // right
        { "\\e[F",          "end-of-line" },             // end
        { "\\e[H",          "beginning-of-line" },       // home
        { "\\e[1;2A",       "cua-previous-screen-line" },// shift-up
        { "\\e[1;2B",       "cua-next-screen-line" },    // shift-down
        { "\\e[1;2D",       "cua-backward-char" },       // shift-left
        { "\\e[1;2C",       "cua-forward-char" },        // shift-right
        { "\\e[1;6D",       "cua-backward-word" },       // ctrl-shift-left
        { "\\e[1;6C",       "cua-forward-word" },        // ctrl-shift-right
        { "\\e[1;2H",       "cua-beg-of-line" },         // shift-home
        { "\\e[1;2F",       "cua-end-of-line" },         // shift-end
        // Diagnostics.
        { "\\C-x\\C-f",     "clink-dump-functions" },    // ctrl+x,ctrl+f
        { "\\C-x\\C-m",     "clink-dump-macros" },       // ctrl+x,ctrl+m
        { "\\C-x\\e[27;5;77~", "clink-dump-macros" },    // ctrl+x,ctrl+m (differentiated)
        { "\\C-x\\e\\C-f",  "dump-functions" },          // ctrl+x,alt+ctrl+f
        { "\\C-x\\e[27;7;77~", "dump-macros" },          // ctrl+x,alt+ctrl+m (differentiated)
        { "\\C-x\\e\\C-v",  "dump-variables" },          // ctrl+x,alt+ctrl+v
        { "\\C-x\\C-z",     "clink-diagnostics" },       // ctrl-x,ctrl-z
        { "\\C-x\\C-z",     "clink-diagnostics" },       // ctrl-x,ctrl-z
        {}
    };

    static constexpr const char* const emacs_key_binds[][2] = {
        { "\\C-c",          "clink-ctrl-c" },            // ctrl-c
        { "\\C-e",          "end-of-line" },             // ctrl-e (because of suggestions)
        { "\\C-v",          "clink-paste" },             // ctrl-v
        { "\\C-z",          "undo" },                    // ctrl-z
        { "\\M-f",          "forward-word" },            // alt-f (because of suggestions)
        { "\\M-g",          "glob-complete-word" },      // alt-g
        { "\\M-\\C-e",      "clink-expand-line" },       // alt-ctrl-e
        { "\\e^",           "clink-expand-history" },    // alt-^
        { "\\e[2~",         "overwrite-mode" },          // ins
        { "\\e[3~",         "delete-char" },             // del
        { "\\e[2;5~",       "cua-copy" },                // ctrl-ins
        { "\\e[2;2~",       "clink-paste" },             // shift-ins
        { "\\e[3;2~",       "cua-cut" },                 // shift-del
        { "\\e[3;5~",       "kill-word" },               // ctrl-del
        { "\\d",            "backward-kill-word" },      // ctrl-backspace
        { "\\eOP",          "win-cursor-forward" },      // F1
        { "\\eOQ",          "win-copy-up-to-char" },     // F2
        { "\\eOR",          "win-copy-up-to-end" },      // F3
        { "\\eOS",          "win-delete-up-to-char" },   // F4
        { "\\e[15~",        "previous-history" },        // F5
        { "\\e[17~",        "win-insert-eof" },          // F6
        { "\\e[18~",        "win-history-list" },        // F7
        { "\\e[19~",        "history-search-backward" }, // F8
        { "\\e[20~",        "win-copy-history-number" }, // F9
        {}
    };

    static constexpr const char* const vi_insertion_key_binds[][2] = {
        { "\\M-\\C-i",      "tab-insert" },              // alt-ctrl-i
        { "\\M-\\C-j",      "emacs-editing-mode" },      // alt-ctrl-j
        { "\\M-\\C-m",      "emacs-editing-mode" },      // alt-ctrl-m
        { "\\C-_",          "vi-undo" },                 // ctrl--
        { "\\M-0",          "vi-arg-digit" },            // alt-0
        { "\\M-1",          "vi-arg-digit" },            // alt-1
        { "\\M-2",          "vi-arg-digit" },            // alt-2
        { "\\M-3",          "vi-arg-digit" },            // alt-3
        { "\\M-4",          "vi-arg-digit" },            // alt-4
        { "\\M-5",          "vi-arg-digit" },            // alt-5
        { "\\M-6",          "vi-arg-digit" },            // alt-6
        { "\\M-7",          "vi-arg-digit" },            // alt-7
        { "\\M-8",          "vi-arg-digit" },            // alt-8
        { "\\M-9",          "vi-arg-digit" },            // alt-9
        { "\\M-[",          "arrow-key-prefix" },        // arrow key prefix
        { "\\d",            "backward-kill-word" },      // ctrl-backspace
        {}
    };

    static constexpr const char* const vi_movement_key_binds[][2] = {
        { " ",              "forward-char" },            // space (because of suggestions)
        { "$",              "end-of-line" },             // end (because of suggestions)
        { "l",              "forward-char" },            // l (because of suggestions)
        { "\\M-\\C-j",      "emacs-editing-mode" },      // alt-ctrl-j
        { "\\M-\\C-m",      "emacs-editing-mode" },      // alt-ctrl-m
        {}
    };

    const char* bindableEsc = get_bindable_esc();
    if (bindableEsc)
    {
        rl_unbind_key_in_map(27/*alt-ctrl-[*/, emacs_meta_keymap);
        rl_unbind_key_in_map(27, vi_insertion_keymap);
        rl_bind_keyseq_in_map("\\e[27;7;219~"/*alt-ctrl-[*/, rl_named_function("complete"), emacs_standard_keymap);
        rl_bind_keyseq_in_map(bindableEsc, rl_named_function("clink-reset-line"), emacs_standard_keymap);
        rl_bind_keyseq_in_map(bindableEsc, rl_named_function("vi-movement-mode"), vi_insertion_keymap);
    }

    rl_unbind_key_in_map(' ', emacs_meta_keymap);
    bind_keyseq_list(general_key_binds, emacs_standard_keymap);
    bind_keyseq_list(emacs_key_binds, emacs_standard_keymap);

    bind_keyseq_list(general_key_binds, vi_insertion_keymap);
    bind_keyseq_list(general_key_binds, vi_movement_keymap);
    bind_keyseq_list(vi_insertion_key_binds, vi_insertion_keymap);
    bind_keyseq_list(vi_movement_key_binds, vi_movement_keymap);
}

//------------------------------------------------------------------------------
static int32 editline()
{
    str_compare_scope _(str_compare_scope::relaxed, false/*fuzzy_accent*/);

    // Load the settings from disk, since terminal I/O is affected by settings.
    str<280> settings_file;
    str<280> default_settings_file;
    app_context::get()->get_settings_path(settings_file);
    app_context::get()->get_default_settings_file(default_settings_file);
    settings::load(settings_file.c_str(), default_settings_file.c_str());

    terminal term = terminal_create();
    printer printer(*term.out);
    printer_context prt(term.out, &printer);
    console_config cc;

    lua_state lua;
    lua_match_generator lua_generator(lua);

    line_editor::desc desc = { term.in, term.out, &printer, nullptr };
    line_editor* editor = line_editor_create(desc);
    editor->set_generator(lua_generator);

    init_readline_funmap();
    init_readline_testbed();

    str<> out;
    while (true)
    {
        reset_display_readline();

        out.clear();
        editor->edit(out);

        if (out.equals("exit"))
            break;

        if (!out.empty())
            add_history(out.c_str());
    }

    line_editor_destroy(editor);

    shutdown_recognizer();
    shutdown_task_manager(true/*final*/);

    return 0;
}

//------------------------------------------------------------------------------
static int32 hookline(app_context::desc& app_desc)
{
    // Get function in host exe.
    FARPROC worker = GetProcAddress(nullptr, "testbed_hook_loop");
    if (!worker)
    {
        fputs("Unable to find exported testbed function in exe.", stderr);
        return 1;
    }

    SetEnvironmentVariableW(L"prompt", L"clink $ ");

    // Simulate injection.
    delete app_context::get();
    if (!initialise_clink(app_desc))
        return 1;

    // Call function in host exe.
    worker();

    return 0;
}

//------------------------------------------------------------------------------
int32 testbed(int32 argc, char** argv)
{
    static const char* help_usage = "Usage: testbed [options]\n";

    static const struct option options[] = {
        { "hook",        no_argument,        nullptr, 'd' },
        { "scripts",     required_argument,  nullptr, 's' },
        { "profile",     required_argument,  nullptr, 'p' },
        { "help",        no_argument,        nullptr, 'h' },
        { nullptr, 0, nullptr, 0 }
    };

    static const char* const help[] = {
        "-d, --hook",           "Hook and use ReadConsoleW.",
        "-s, --scripts <path>", "Alternative path to load .lua scripts from.",
        "-p, --profile <path>", "Specifies an alternative path for profile data.",
        "-h, --help",           "Shows this help text.",
        nullptr
    };

    // Parse arguments
    bool hook = false;
    app_context::desc app_desc;
    int32 i;
    int32 ret = 1;
    while ((i = getopt_long(argc, argv, "?hp:s:d", options, nullptr)) != -1)
    {
        switch (i)
        {
        case 's':
            {
                str<> arg(optarg);
                arg.trim();
                str_base script_path(app_desc.script_path);
                os::get_current_dir(script_path);
                path::append(script_path, arg.c_str());
                path::normalise(script_path);
            }
            break;

        case 'p':
            {
                str<> arg(optarg);
                arg.trim();
                str_base state_dir(app_desc.state_dir);
                get_profile_path(arg.c_str(), state_dir);
            }
            break;

        case 'd':
            hook = true;
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
            puts("By default this starts a loop of calling editor->edit() directly.  Run\n"
                 "'clink testbed --hook' to hook the ReadConsoleW API and start a loop of\n"
                 "calling that, as though it were injected in cmd.exe (editing works, but\n"
                 "of course nothing happens with the input since the host isn't cmd.exe).");
            return ret;
        }
    }

    if (hook)
    {
        app_desc.id = GetCurrentProcessId();
        app_desc.force = true; // Skip the usual cmd.exe check.
        ret = hookline(app_desc);
    }
    else
    {
        ret = editline();
    }

    shutdown_recognizer();

    return ret;
}
