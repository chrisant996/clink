// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <tib.h>

class clink_input_box : public tib::editor_context
{
    typedef tib::editor_context base;

public:
    int32_t dispatch(const tib::cstring& sequence, int32_t key,
                     const tib::binding_target* binding,
                     const tib::binding_params* params) noexcept override;

    int32_t start_kbd_macro() noexcept;
    int32_t end_kbd_macro() noexcept;
    int32_t call_last_kbd_macro() noexcept;

private:
    int32_t play_kbd_macro(int32_t count) noexcept;

    tib::cstring m_recording;
    tib::cstring m_last_macro;
    bool m_recording_macro = false;
    bool m_has_last_macro = false;
};
