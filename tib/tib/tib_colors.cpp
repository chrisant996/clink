// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "pch.h"
#include "maybe_windows.h"
#include "tib_base.h"
#include "tib_colors.h"
#include <vector>
#include <assert.h>

namespace tib {

const char* color_table::get_color(color_element color) const
{
    if (!this)
    {
        static const char* const c_default_colors[] =
        {
            "",
            "",
            "",
            "",
            "96;7",
            "7",
            "1",
            "90",
        };
        static_assert(std::size(c_default_colors) == size_t(color_element::MAX));
        return c_default_colors[size_t(color)];
    }
    return m_colors[size_t(color)].c_str();
}

void color_table::set_color(color_element color, const char* sgr_params)
{
    m_colors[size_t(color)].set(sgr_params);
}

bool color_table::append_color(cstring& out, color_element color, color_element overlay) const
{
    const char* sgr_overlay = (overlay == color_element::MAX) ? nullptr : get_color(overlay);
    if (!(sgr_overlay && sgr_overlay[0] == '0') && !out.append_color(get_color(color)))
        return false;
    if (overlay == color_element::MAX)
        return true;
    if (!sgr_overlay || !*sgr_overlay)
        return true;
    return out.append_color(sgr_overlay);
}

} // namespace tib
