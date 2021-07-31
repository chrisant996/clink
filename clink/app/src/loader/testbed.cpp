// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str_compare.h>
#include <core/settings.h>
#include <lib/line_editor.h>
#include <lib/match_generator.h>
#include <lib/terminal_helpers.h>
#include <terminal/terminal.h>
#include <terminal/printer.h>
#include <utils/app_context.h>

//------------------------------------------------------------------------------
int testbed(int, char**)
{
    str_compare_scope _(str_compare_scope::relaxed);

    // Load the settings from disk, since terminal I/O is affected by settings.
    str<280> settings_file;
    app_context::get()->get_settings_path(settings_file);
    settings::load(settings_file.c_str());

    terminal term = terminal_create();
    printer printer(*term.out);
    printer_context prt(term.out, &printer);
    console_config cc(GetStdHandle(STD_INPUT_HANDLE));

    line_editor::desc desc = { term.in, term.out, &printer, nullptr };
    line_editor* editor = line_editor_create(desc);
    editor->add_generator(file_match_generator());

    str<> out;
    while (editor->edit(out));

    line_editor_destroy(editor);
    return 0;
}
