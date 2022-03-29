// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "core/str.h"
#include "lib/word_classifier.h"
#include "lib/word_classifications.h"

class lua_state;

//------------------------------------------------------------------------------
class lua_word_classifier
    : public word_classifier
{
public:
                    lua_word_classifier(lua_state& state);
    virtual void    classify(const line_states& commands, word_classifications& classifications) override;

private:
    lua_state&      m_state;
};
