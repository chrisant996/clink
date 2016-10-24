// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str_compare.h>
#include <lib/line_editor.h>
#include <lib/match_generator.h>
#include <terminal/win_terminal_in.h>
#include <terminal/win_terminal_out.h>

//------------------------------------------------------------------------------
int testbed(int, char**)
{
    str_compare_scope _(str_compare_scope::relaxed);

    win_terminal_in input;
    win_terminal_out output;

    editor_module* ui = classic_match_ui_create();

    line_editor::desc desc;
    desc.input = &input;
    desc.output = &output;

    line_editor* editor = line_editor_create(desc);
    editor->add_module(*ui);
    editor->add_generator(file_match_generator());

    char out[64];
    while (editor->edit(out, sizeof_array(out)));

    line_editor_destroy(editor);
    classic_match_ui_destroy(ui);
    return 0;
}
