// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

#include "tib_base.h"
#include "tib_bindings.h"
#include "tib_buffer.h"
#include "tib_colors.h"
#include "tib_commands.h"
#include "tib_context.h"
#include "tib_grapheme.h"
#include "tib_termcap.h"
#include "tib_terminal.h"

namespace tib {

class input_box : public editor_context
{
public:
                        ~input_box() = default;
                        input_box() = default;
};

} // namespace tib
