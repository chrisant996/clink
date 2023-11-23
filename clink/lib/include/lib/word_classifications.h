// Copyright (c) 2020-2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>
#include <core/str.h>
#include <core/str_map.h>

#include <vector>

class line_state;

//------------------------------------------------------------------------------
enum class word_class : uint8
{
    other = 0,
    unrecognized,   // 'u'
    executable,     // 'x'
    command,        // 'c'
    doskey,         // 'd'
    arg,            // 'a'
    flag,           // 'f'
    none,           // 'n'
    max,
    invalid = 255
};

//------------------------------------------------------------------------------
word_class to_word_class(char wc);

//------------------------------------------------------------------------------
struct word_class_info
{
    uint32          start : 16;
    uint32          end : 16;
    word_class      word_class;
    bool            argmatcher;
    bool            unbreak;
};

//------------------------------------------------------------------------------
class word_classifications : public no_copy
{
    typedef str_map_caseless<char>::type faces_map;

public:
                    word_classifications() = default;
                    ~word_classifications();
                    word_classifications(word_classifications&& other);

    void            clear();
    void            init(size_t line_length, const word_classifications* face_defs);
    uint32          add_command(const line_state& line);
    void            set_word_has_argmatcher(uint32 index);
    void            finish(bool show_argmatchers);

    uint32          size() const { return uint32(m_info.size()); }
    uint32          length() const { return m_length; }
    const word_class_info* operator[](uint32 index) const { return &m_info[index]; }
    bool            equals(const word_classifications& other) const;

    bool            get_word_class(uint32 index, word_class& wc) const;
    char            get_face(uint32 pos) const;
    const char*     get_face_output(char face) const;

    char            ensure_face(const char* sgr);
    void            apply_face(uint32 start, uint32 len, char face, bool overwrite=true);
    void            classify_word(uint32 index, char wc, bool overwrite=true);
    bool            is_word_classified(uint32 index);

    void            unbreak(uint32 index, uint32 length, bool skip_word);
    void            flush_unbreak();

private:
    std::vector<word_class_info> m_info;
    std::vector<str_moveable> m_face_definitions;
    char*           m_faces = nullptr;
    uint32          m_length = 0;
    faces_map       m_face_map;             // Points into m_face_definitions.
};
