#pragma once

#include <core/base.h>
#include <core/str.h>
#include "matches.h"

enum class match_type : unsigned short;

typedef bool rl_match_display_filter_func_t(char**, matches&);
extern rl_match_display_filter_func_t *rl_match_display_filter_func;

extern const char *_rl_description_color;
extern const char *_rl_filtered_color;
extern const char *_rl_arginfo_color;
extern const char *_rl_selected_color;

void reset_tmpbuf(void);
void mark_tmpbuf(void);
const char* get_tmpbuf_rollback(void);
void rollback_tmpbuf(void);
void append_tmpbuf_char(char c);
void append_tmpbuf_string(const char* s, int32 len);
void append_tmpbuf_string_colorless(const char* s, int32 len);
uint32 calc_tmpbuf_cell_count(void);
void flush_tmpbuf(void);
void append_display(const char* to_print, int32 selected, const char* color);
int32 append_filename(char* to_print, const char* full_pathname, int32 prefix_bytes, int32 can_condense, match_type type, int32 selected, int32* vis_stat_char);
void pad_filename(int32 len, int32 pad_to_width, int32 selected);

int32 printable_len(const char* match, match_type type);
int32 __fnwidth(const char* string);

#define DESC_ONE_COLUMN_THRESHOLD       9

// Flags in the PACKED MATCH FORMAT:
#define MATCH_FLAG_APPEND_DISPLAY       0x01
#define MATCH_FLAG_HAS_SUPPRESS_APPEND  0x02
#define MATCH_FLAG_SUPPRESS_APPEND      0x04

// For display_matches, the matches array must contain specially formatted
// match entries:
//
//  TYPE (unsigned char), when rl_completion_matches_include_type
//  MATCH (nul terminated char string)
//  FLAGS (unsigned char)
//  DISPLAY (nul terminated char string)
//  DESCRIPTION (nul terminated char string)
extern "C" void display_matches(char **matches);

void override_line_state(const char* line, const char* needle, int32 point);
#ifdef DEBUG
bool is_line_state_overridden();
#endif

class override_match_line_state
{
public:
    override_match_line_state() { assert(!is_line_state_overridden()); }
    ~override_match_line_state() { override_line_state(nullptr, nullptr, 0); }
    void override(int32 start, int32 end, const char* needle);
    void override(int32 start, int32 end, const char* needle, char quote_char);
    void fully_qualify(int32 start, int32 end, str_base& needle);
private:
    str_moveable m_line;
};

char need_leading_quote(const char* match);
