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
    struct word_def
    {
        uint16              offset;
        uint16              length;
    };

public:
                            lua_word_classifications(word_classifications& classifications, uint32 index_command);

protected:
    int32                   classify_word(lua_State* state);
    int32                   apply_color(lua_State* state);
    int32                   set_line_state(lua_State* state);

private:
    word_classifications&   m_classifications;
    const uint32            m_index_command;
    std::vector<word_def>   m_words;
    uint32                  m_command_word_index = 0;
    uint32                  m_shift = 0;
    bool                    m_test = false;

    friend class lua_bindable<lua_word_classifications>;
    static const char* const c_name;
    static const method     c_methods[];
};
