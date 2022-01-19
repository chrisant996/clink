// Copyright (c) 2020-2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>
#include <core/str.h>
#include <core/str_map.h>

#include <vector>

class line_state;

//------------------------------------------------------------------------------
enum class word_class : unsigned char
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
    unsigned int    start : 16;
    unsigned int    end : 16;
    word_class      word_class;
    bool            argmatcher;
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
    unsigned int    add_command(const line_state& line);
    void            set_word_has_argmatcher(unsigned int index);
    void            finish(bool show_argmatchers);

    unsigned int    size() const { return static_cast<unsigned int>(m_info.size()); }
    const word_class_info* operator[](unsigned int index) const { return &m_info[index]; }
    bool            equals(const word_classifications& other) const;

    bool            get_word_class(unsigned int index, word_class& wc) const;
    char            get_face(unsigned int pos) const;
    const char*     get_face_output(char face) const;

    char            ensure_face(const char* sgr);
    void            apply_face(unsigned int start, unsigned int len, char face, bool overwrite=true);
    void            classify_word(unsigned int index, char wc, bool overwrite=true);
    bool            is_word_classified(unsigned int index);

private:
    std::vector<word_class_info> m_info;
    std::vector<str_moveable> m_face_definitions;
    char*           m_faces = nullptr;
    unsigned int    m_length = 0;
    faces_map       m_face_map;             // Points into m_face_definitions.
};
