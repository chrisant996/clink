#pragma once

struct match_display_filter_entry
{
    short visible_display;      // Visible characters, not counting ANSI escape codes.
    short visible_description;  // Visible characters, not counting ANSI escape codes.
    const char* match;          // Match string (pointer into buffer).
    const char* display;        // Display string (pointer into buffer).
    const char* description;    // Description string (pointer into buffer).
    unsigned char type;         // IMPORTANT: Must immediately preceed buffer for compatibility with rl_completion_matches_include_type.
    char buffer[1];             // Variable length buffer containing match, display, and description.
};
typedef struct match_display_filter_entry match_display_filter_entry;

// Match display filter entry [0] is a placeholder and is ignored except in two
// ways:
//  1.  If the entry is nullptr, the list is empty.
//  2.  If its visible_len is negative, then force the list to be displayed in a
//      single column.
typedef match_display_filter_entry** rl_match_display_filter_func_t(char**);
extern rl_match_display_filter_func_t *rl_match_display_filter_func;

extern const char *_rl_description_color;
extern const char *_rl_filtered_color;
extern const char *_rl_selected_color;

extern void reset_tmpbuf(void);
extern void mark_tmpbuf(void);
extern void rollback_tmpbuf(void);
extern void append_tmpbuf_char(char c);
extern void append_tmpbuf_string(const char* s, int len);
extern void flush_tmpbuf(void);
extern void append_display(const char* to_print, int selected);
// type is ignored when rl_completion_matches_include_type is set.
extern int append_filename(char* to_print, const char* full_pathname, int prefix_bytes, unsigned char type, int selected);
extern void pad_filename(int len, int pad_to_width, int selected);

extern void free_filtered_matches(match_display_filter_entry** filtered_matches);
extern int printable_len(const char* match);
extern void display_matches(char **matches);
