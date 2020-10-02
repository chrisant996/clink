// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str_compare.h>
#include <lib/line_editor.h>
#include <lib/match_generator.h>

//------------------------------------------------------------------------------
int testbed(int, char**)
{
    str_compare_scope _(str_compare_scope::relaxed);

    line_editor::desc desc;
    line_editor* editor = line_editor_create(desc);
    editor->add_generator(file_match_generator());

    str<> out;
    while (editor->edit(out));

    line_editor_destroy(editor);
    return 0;
}
