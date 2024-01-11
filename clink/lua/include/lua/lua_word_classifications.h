// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lua_bindable.h"

struct lua_State;
enum class word_class : uint8;
class word_classifications;

//------------------------------------------------------------------------------
// word_classifications collects the coloring info for the whole input line.
// lua_word_classifications wraps it to apply coloring info only to the words
// for a given line_state (group of words for a command) from the input line.
class lua_word_classifications
    : public lua_bindable<lua_word_classifications>
{
public:
                            lua_word_classifications(word_classifications& classifications, uint32 index_offset, uint32 command_word_index, uint32 num_words);

    bool                    get_word_class(int32 word_index_zero_based, word_class& wc) const;

protected:
    int32                   classify_word(lua_State* state);
    int32                   apply_color(lua_State* state);
    int32                   shift(lua_State* state);
    int32                   reset_shift(lua_State* state);
    int32                   unbreak(lua_State* state);

private:
    word_classifications&   m_classifications;
    const uint32            m_index_offset;
    const uint32            m_num_words;
    uint32                  m_command_word_index;
    uint32                  m_shift = 0;
    const uint32            m_original_command_word_index;

    friend class lua_bindable<lua_word_classifications>;
    static const char* const c_name;
    static const method     c_methods[];
};
