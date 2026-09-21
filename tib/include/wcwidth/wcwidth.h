// Copyright (c) 2023,2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

typedef int32_t wcwidth_func_t(char32_t);
extern "C" wcwidth_func_t* wcwidth;
extern "C" uint32_t __wcswidth(const char* s, size_t len/*=-1*/);
extern "C" uint32_t __wcswidth_expandctrl(const char* s, size_t len/*=-1*/);
extern "C" uint32_t cell_count(const char* s, size_t len/*=-1*/);
extern "C" void reset_wcwidths();
#if 0
extern "C" void reset_cached_font();
#endif

#ifdef __cplusplus

extern bool g_color_emoji;      // Assume whether the terminal supports color emoji.

#include "str_iter.h"

bool is_variant_selector(char32_t ucs);
bool is_possible_unqualified_half_width(char32_t ucs);
bool is_emoji(char32_t ucs);

#ifdef _WIN32
void detect_ucs2_limitation(bool force=false);
int32_t test_ambiguous_width_char(char32_t ucs, str_iter* iter);
#endif

class combining_mark_width_scope
{
public:
    enum combining_mark_width_mode { mode_normal, mode_emoji };
    combining_mark_width_scope(combining_mark_width_mode mode);
    ~combining_mark_width_scope();
private:
    const int32_t m_old_combining_mark_width;
    const int32_t m_old_fe0f_width;
};

class wcwidth_iter
{
public:
    explicit        wcwidth_iter(const char* s, size_t len=-1);
                    wcwidth_iter(const wcwidth_iter& i);
    char32_t        next();
    void            unnext();
    const char*     character_pointer() const { return m_chr_ptr; }
    uint32_t        character_length() const { return uint32_t(m_chr_end - m_chr_ptr); }
    int16_t         character_wcwidth_signed() const { return m_chr_wcwidth; }
    uint16_t        character_wcwidth_zeroctrl() const { return (m_chr_wcwidth < 0) ? 0 : m_chr_wcwidth; }
    uint16_t        character_wcwidth_onectrl() const { return (m_chr_wcwidth < 0) ? 1 : m_chr_wcwidth; }
    uint16_t        character_wcwidth_twoctrl() const { return (m_chr_wcwidth < 0) ? 2 : m_chr_wcwidth; }
    bool            character_is_emoji() const { return m_emoji; }
    const char*     get_pointer() const;
    void            reset_pointer(const char* s);
    bool            more() const;
    size_t          length() const;

private:
    void            consume_emoji_sequence();

private:
    str_iter        m_iter;
    char32_t        m_next;
    const char*     m_chr_ptr;
    const char*     m_chr_end;
    int16_t         m_chr_wcwidth = 0;
    bool            m_emoji = false;
};

#endif // __cplusplus
