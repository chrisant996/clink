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
struct word_class_info
{
    bool            argmatcher = false;
    word_class      word_class = word_class::invalid;
};

//------------------------------------------------------------------------------
typedef std::vector<word_class_info> word_class_infos;

//------------------------------------------------------------------------------
word_class to_word_class(char wc);

//------------------------------------------------------------------------------
class word_classifications : public no_copy
{
    typedef str_map_caseless<char>::type faces_map;

public:
                    word_classifications() = default;
                    ~word_classifications();
                    word_classifications(word_classifications&& other);

    void            clear();
    void            init(size_t line_length, const word_classifications* face_defs, bool argmatchers);
    void            finish();

    uint32          length() const { return m_length; }
    bool            equals(const word_classifications& other) const;

    char            get_face(uint32 pos) const;
    const char*     get_face_output(char face) const;

    char            ensure_face(const char* sgr);
    void            apply_face(bool words, uint32 start, uint32 len, char face, bool overwrite);
    bool            can_show_argmatchers() const { return m_argmatchers; }

    void            classify_word(uint32 index_command, uint32 index_word, char wc, bool argmatcher, bool overwrite=true);
    const word_class_infos* get_test_infos() const;

private:
    std::vector<str_moveable> m_face_definitions;
    bool            m_argmatchers = false;
    char*           m_faces = nullptr;
    char*           m_word_faces = nullptr;
    uint32          m_length = 0;
    faces_map       m_face_map;             // Points into m_face_definitions.

    mutable word_class_infos* m_coalesced_test_infos = nullptr;
    mutable std::vector<word_class_infos>* m_test_infos = nullptr;
};
