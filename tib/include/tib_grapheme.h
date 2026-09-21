// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

namespace tib {

extern const char c_replacement_character[];
extern const uint32_t c_replacement_character_length;

uint32_t backward_one_grapheme(const char* s, size_t len, uint32_t pos, uint16_t* width=nullptr);
uint32_t forward_one_grapheme(const char* s, size_t len, uint32_t pos, uint16_t* width=nullptr);

struct grapheme_info
{
    uint32_t        index;
    uint32_t        length;
    uint16_t        width;
};

size_t parse_graphemes(const char* s, size_t len, uint32_t pos, std::vector<grapheme_info>& out);

}
