// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "line_editor.h"

//------------------------------------------------------------------------------
line_editor*    create_rl_line_editor(const line_editor::desc& desc);
void            destroy_rl_line_editor(line_editor* editor);
