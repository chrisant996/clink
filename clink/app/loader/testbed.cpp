// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl/rl_backend.h"

#include <core/str_compare.h>
#include <lib/classic_match_ui.h>
#include <lib/line_editor.h>
#include <lib/match_generator.h>
#include <terminal/win_terminal.h>

//------------------------------------------------------------------------------
int testbed(int, char**)
{
    str_compare_scope _(str_compare_scope::relaxed);

    win_terminal terminal;

    classic_match_ui ui;
    rl_backend backend("testbed");

    line_editor::desc desc = {};
    desc.prompt = "testbed $ ";
    desc.quote_pair = "\"";
    desc.word_delims = " \t=";
    desc.partial_delims = "\\/:";
    desc.terminal = &terminal;
    desc.backend = &backend;
    desc.buffer = &backend;
    line_editor* editor = line_editor_create(desc);
    editor->add_backend(ui);
    editor->add_generator(file_match_generator());

    char out[64];
    while (editor->edit(out, sizeof_array(out)));

    line_editor_destroy(editor);
    return 0;
}
