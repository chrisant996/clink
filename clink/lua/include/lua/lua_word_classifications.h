// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lua_bindable.h"

struct lua_State;
enum class word_class : unsigned char;
class word_classifications;

//------------------------------------------------------------------------------
// word_classifications collects the coloring info for the whole input line.
// lua_word_classifications wraps it to apply coloring info only to the words
// for a given line_state (group of words for a command) from the input line.
class lua_word_classifications
    : public lua_bindable<lua_word_classifications>
{
public:
                            lua_word_classifications(word_classifications& classifications, unsigned int index_offset, unsigned int command_word_index, unsigned int num_words);
    int                     classify_word(lua_State* state);
    int                     apply_color(lua_State* state);
    int                     shift(lua_State* state);

    bool                    get_word_class(int word_index_zero_based, word_class& wc) const;

private:
    word_classifications&   m_classifications;
    const unsigned int      m_index_offset;
    const unsigned int      m_num_words;
    unsigned int            m_command_word_index;
    unsigned int            m_shift = 0;

    friend class lua_bindable<lua_word_classifications>;
    static const char* const c_name;
    static const method     c_methods[];
};
