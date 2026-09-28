// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

#include "tib_base.h"

namespace tib {

extern bool g_coalesce_output;

// Use some invalid UTF8 bytes for special meanings.
constexpr uint8_t c_input_terminal_reserved_begin   = 0xfa;
//                                                  = 0xfa;
//                                                  = 0xfb;
//                                                  = 0xfc;
constexpr uint8_t c_input_terminal_eof              = 0xfd;
constexpr uint8_t c_input_terminal_resize           = 0xfe;
// Do not use 0xff; it may be confused with errors. = 0xff;

enum class mouse_input_mode { none, VT200, DRAG, ANY };

class pushed_input;

class terminal_in
{
public:
    virtual             ~terminal_in() = default;
    virtual int32_t     read() noexcept = 0;
    virtual bool        avail(uint32_t timeout=0) noexcept = 0;
    virtual bool        enable_mouse_input(mouse_input_mode mode, bool sgr_encoding) noexcept { return false; }
};

class terminal_out
{
public:
    virtual             ~terminal_out() = default;
    virtual void        write(const char* s, size_t len) noexcept = 0;
    virtual void        ding() noexcept = 0;
};

typedef terminal_in* (*hook_new_terminal_in_func_t)(pushed_input& pushed);
typedef terminal_out* (*hook_new_terminal_out_func_t)();
extern hook_new_terminal_in_func_t hook_new_terminal_in;
extern hook_new_terminal_out_func_t hook_new_terminal_out;

void term_begin();
void term_end();
void term_sigint();
#ifdef _WIN32
void term_sigclose();
#endif

int32_t term_in();
int32_t term_in_peek();
bool term_in_avail(DWORD timeout=0);
// Prepend text to the highest-priority pushed-input queue.
bool term_push_input(const char* text, size_t len=-1);
bool term_push_macro_text(const char* text, size_t len=-1);
bool enable_mouse_input(mouse_input_mode mode, bool sgr_encoding=true);

void term_out(const char* s, size_t len=c_auto_length);
void ding();

size_t fits_in_wcwidth(const char* s, const size_t len, const uint16_t truncate_width, uint16_t* truncated_width);

bool ensure_term_caps();
coord get_terminal_size();

class pushed_input
{
public:
                        ~pushed_input() noexcept;
                        pushed_input() = default;
                        pushed_input(const pushed_input&) = delete;
    pushed_input&       operator=(const pushed_input&) = delete;
    bool                empty() const noexcept { return !m_count; }
    bool                push(uint8_t c) noexcept;
    bool                push(const char* text, size_t len=c_auto_length) noexcept;
    bool                push_front(const char* text, size_t len) noexcept;
#ifdef _WIN32
    int32_t             push_utf16(WCHAR c) noexcept;
    int32_t             push_key_event(const KEY_EVENT_RECORD& record) noexcept;
#endif
    bool                push_invalid() noexcept;
    int32_t             peek() const noexcept;
    int32_t             read() noexcept;

private:
    bool                ensure_capacity(size_t num) noexcept;

    uint8_t*            m_data = nullptr;
    size_t              m_size = 0;
    size_t              m_head = 0;
    size_t              m_count = 0;

#ifdef _WIN32
    WCHAR               m_high_surrogate = 0;
    cstring             m_tmp_utf8;
#endif
};

class display_accumulator
{
    friend void term_out(const char*, size_t);
public:
                        ~display_accumulator();
                        display_accumulator();
    void                end();
    static void         synchronize_output(bool sync) { s_can_synchronize_output = sync; }
    static bool         active() { return s_active; }
    static bool         synchronized_output() { return s_synchronized_output; }
    static void         flush();
private:
    static void         append(const char* s, size_t len);
private:
    static int32_t      s_nested;
    static bool         s_can_synchronize_output;
    static bool         s_active;
    static bool         s_synchronized_output;
};

#ifdef _WIN32
// When the Windows legacy console window's visible area is a subset of the
// console width, then the visible area can jitter around or can accidentally
// clip the region that gets cleared by CSI K (Erase in Line, aka EL).  The
// technique encapsulated in preserve_window_horiz_scroll_position minimizes
// the amount of jitter.
class preserve_window_horiz_scroll_position
{
public:
                        preserve_window_horiz_scroll_position(HANDLE h);
                        ~preserve_window_horiz_scroll_position();
    static void         set_clreol_is_safe(bool safe) { s_safe_clreol_when_horiz_scrolled = safe; }
    static bool         can_use_clreol() { return !s_h || s_safe_clreol_when_horiz_scrolled; }
private:
    static bool         s_safe_clreol_when_horiz_scrolled;
    static int32_t      s_nested;
    static HANDLE       s_h;
    static CONSOLE_SCREEN_BUFFER_INFO s_window;
};

HANDLE is_horizpos_workaround_needed();
#endif

} // namespace tib
