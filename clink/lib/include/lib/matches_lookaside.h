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
    unsigned char   flags;
};

//------------------------------------------------------------------------------
class match_details
{
public:
                            match_details(const char* match, const match_extra* extra);
    operator                bool() const { return m_match; }
    match_type              get_type() const { return m_extra->type; }
    const char*             get_match() const { return m_match; }
    const char*             get_display() const { return m_match + m_extra->display_offset; }
    const char*             get_description() const { return m_match + m_extra->description_offset; }
    unsigned char           get_flags() const { return m_extra->flags; }
private:
    const char*             m_match;
    const match_extra*      m_extra;
};

//------------------------------------------------------------------------------
// Each match must conform to the following format:
//  - N bytes:  nul terminated match string.
//  - 1 byte:  match_type.
//  - 1 byte:  flags.
//  - N bytes:  nul terminated display string.
//  - N bytes:  nul terminated description string.
match_details lookup_match(const char* match);
bool create_matches_lookaside(char** matches);
bool destroy_matches_lookaside(char** matches);
void set_matches_lookaside_oneoff(const char* match, match_type type);

extern "C" int lookup_match_type(const char* match);
#ifdef DEBUG
extern "C" int has_matches_lookaside(char** matches);
#endif
