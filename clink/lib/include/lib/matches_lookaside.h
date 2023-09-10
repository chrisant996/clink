// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "matches.h"

//------------------------------------------------------------------------------
struct match_extra
{
    unsigned short  display_offset;
    unsigned short  description_offset;
    match_type      type;
    char            append_char;
    uint8           flags;
};

//------------------------------------------------------------------------------
class match_details
{
public:
                            match_details(const char* match, const match_extra* extra);
    operator                bool() const { return m_match; }
    match_type              get_type() const { return m_extra->type; }
    const char*             get_match() const { return m_match; }
    const char*             get_display() const { return m_match ? m_match + m_extra->display_offset : nullptr; }
    const char*             get_description() const { return m_match ? m_match + m_extra->description_offset : nullptr; }
    char                    get_append_char() const { return m_extra->append_char; }
    uint8                   get_flags() const { return m_extra->flags; }
private:
    const char*             m_match;
    const match_extra*      m_extra;
    static const match_extra s_empty_extra;
};

//------------------------------------------------------------------------------
// Each match in 'matches' must conform to the PACKED MATCH FORMAT (except the
// lcd entry in [0], which is omitted from the lookaside table).
match_details lookup_match(const char* match);
int32 create_matches_lookaside(char** matches);
int32 destroy_matches_lookaside(char** matches);
void set_matches_lookaside_oneoff(const char* match, match_type type, char append_char, uint8 flags);
void clear_matches_lookaside_oneoff();

const char* append_string_into_buffer(char*& buffer, const char* match, bool allow_tabs=false);
size_t calc_packed_size(const char* match, const char* display, const char* description);
bool pack_match(char* buffer, size_t packed_size,
                const char* match, match_type type,
                const char* display, const char* description,
                char append_char, uint8 flags);

extern "C" int32 lookup_match_type(const char* match);
extern "C" void override_match_append(const char* match);
#ifdef DEBUG
extern "C" int32 has_matches_lookaside(char** matches);
#endif
