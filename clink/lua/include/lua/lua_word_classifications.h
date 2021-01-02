// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lua_bindable.h"

struct lua_State;
enum class word_class : unsigned char;

//------------------------------------------------------------------------------
class lua_word_classifications
    : public lua_bindable<lua_word_classifications>
{
public:
                            lua_word_classifications(const char* classifications);
    int                     is_word_classified(lua_State* state);
    int                     classify_word(lua_State* state);

    unsigned int            size() const { return m_classifications.length(); }
    bool                    get_word_class(int word_index_zero_based, word_class& wc) const;
    void                    classify_word(int word_index_zero_based, char wc);

private:
    str<16>                 m_classifications;
};
