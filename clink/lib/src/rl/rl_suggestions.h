// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
enum class suggestion_action : unsigned char
{
    insert_to_end,
    insert_next_word,
};

//------------------------------------------------------------------------------
bool insert_suggestion(suggestion_action action);
