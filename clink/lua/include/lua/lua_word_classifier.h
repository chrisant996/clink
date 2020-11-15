// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "lib/word_classifier.h"

class lua_state;

//------------------------------------------------------------------------------
class lua_word_classifier
    : public word_classifier
{
public:
                    lua_word_classifier(lua_state& state);
    virtual void    classify(const line_state& line, word_classifications& classifications) const override;

private:
    void            print_error(const char* error) const;
    lua_state&      m_state;
};
