// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str_compare.h>
#include <lib/line_editor.h>
#include <lib/match_generator.h>
#include <terminal/terminal.h>
#include <terminal/printer.h>

//------------------------------------------------------------------------------
int testbed(int, char**)
{
    str_compare_scope _(str_compare_scope::relaxed);

    terminal term = terminal_create();
    printer printer(*term.out);

    line_editor::desc desc = { term.in, term.out, &printer };
    line_editor* editor = line_editor_create(desc);
    editor->add_generator(file_match_generator());

    str<> out;
    while (editor->edit(out));

    line_editor_destroy(editor);
    return 0;
}
