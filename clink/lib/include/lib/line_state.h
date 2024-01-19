// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/object.h>
#include <core/str_iter.h>

#include <vector>

//------------------------------------------------------------------------------
struct word
{
    word() : is_merged_away(false) {} // Everything else is intentionally uninitialized.
    word(uint32 _offset, uint32 _length, bool _command_word, bool _is_alias, bool _is_redir_arg, bool _quoted, uint8 _delim);
    uint32              offset : 16;
    uint32              length : 16;
    bool                command_word : 1;
    bool                is_alias : 1;
    bool                is_redir_arg : 1;
    bool                is_merged_away : 1;
    bool                quoted;
    uint8               delim;
};

//------------------------------------------------------------------------------
class line_state
#ifdef USE_DEBUG_OBJECT
: public object
#endif
{
public:
                        line_state(const char* line, uint32 length, uint32 cursor, uint32 command_offset, uint32 range_offset, uint32 range_length, const std::vector<word>& words);
    const char*         get_line() const;
    uint32              get_length() const;
    uint32              get_cursor() const;
    uint32              get_command_offset() const;
    uint32              get_command_word_index() const;
    uint32              get_end_word_offset() const;
    uint32              get_range_offset() const;
    uint32              get_range_length() const;
    const std::vector<word>& get_words() const;
    uint32              get_word_count() const;
    bool                get_word(uint32 index, str_base& out) const;    // MAY STRIP quotes, except during getworkbreakinfo().
    str_iter            get_word(uint32 index) const;                   // Never strips quotes.
    bool                get_end_word(str_base& out) const;              // MAY STRIP quotes, except during getworkbreakinfo().
    str_iter            get_end_word() const;                           // Never strips quotes.

    bool                overwrite_from(const line_state* other);

    static void         set_can_strip_quotes(bool can);

private:
    const std::vector<word>& m_words;
    const char*         m_line;
    uint32              m_length;
    uint32              m_cursor;
    uint32              m_command_offset;
    uint32              m_range_offset;
    uint32              m_range_length;
};

//------------------------------------------------------------------------------
class line_states : public std::vector<line_state>
{
public:
                        line_states() = default;
};
