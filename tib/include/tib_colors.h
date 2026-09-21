// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

#include "tib_base.h"

#include <memory>

namespace tib {

enum class color_element
{
    base,
    border,
    message,
    input,
    input_selection,
    input_mark,
    input_scroller,
    suggestion,
    MAX
};

class color_table : public std::enable_shared_from_this<color_table>
{
public:
                        ~color_table() = default;
                        color_table() = default;

    const char*         get_color(color_element color) const; // Safe when this == nullptr.
    void                set_color(color_element color, const char* sgr_params);

    bool                append_color(cstring& out, color_element color, color_element overlay=color_element::MAX) const;

private:
    cstring             m_colors[size_t(color_element::MAX)];
};

} // namespace tib
