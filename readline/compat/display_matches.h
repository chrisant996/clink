#pragma once

struct match_display_filter_entry
{
    short visible_display;      // Visible characters, not counting ANSI escape codes.
    short visible_description;  // Visible characters, not counting ANSI escape codes.
    const char* match;          // Match string (pointer into buffer).
    const char* display;        // Display string (pointer into buffer).
    const char* description;    // Description string (pointer into buffer).
    unsigned char type;
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

extern const char *_rl_filtered_color;

extern void free_filtered_matches(match_display_filter_entry** filtered_matches);
extern int printable_len(const char* match);
extern void display_matches(char **matches);
