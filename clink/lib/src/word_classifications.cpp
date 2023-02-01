// Copyright (c) 2020-2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_state.h"
#include "word_classifications.h"
#include "display_readline.h"

#include <core/base.h>
#include <core/str.h>

#include <assert.h>

//------------------------------------------------------------------------------
const size_t face_base = 128;
const size_t face_max = 100;

//------------------------------------------------------------------------------
word_classifications::~word_classifications()
{
    free(m_faces);
}

//------------------------------------------------------------------------------
word_classifications::word_classifications(word_classifications&& other)
{
    m_info = std::move(other.m_info);
    m_face_definitions = std::move(other.m_face_definitions);
    m_faces = other.m_faces;
    m_length = other.m_length;
    m_face_map = std::move(m_face_map);

    other.m_faces = nullptr;    // Transferred ownership above.
    other.clear();
}

//------------------------------------------------------------------------------
void word_classifications::clear()
{
    free(m_faces);

    m_info.clear();
    m_face_definitions.clear();
    m_faces = nullptr;
    m_length = 0;
    m_face_map.clear();
}

//------------------------------------------------------------------------------
void word_classifications::init(size_t line_length, const word_classifications* face_defs)
{
    clear();

    if (face_defs)
    {
        for (auto const& def : face_defs->m_face_definitions)
        {
            char face = char(face_base + m_face_definitions.size());
            m_face_definitions.emplace_back(def.c_str());
            m_face_map.emplace(m_face_definitions.back().c_str(), face);
        }
    }

    if (line_length)
    {
        m_faces = static_cast<char*>(malloc(line_length));
        if (m_faces)
        {
            m_length = static_cast<unsigned int>(line_length);
            // Space means not classified; use default color.
            memset(m_faces, FACE_SPACE, line_length);
        }
    }
}

//------------------------------------------------------------------------------
unsigned int word_classifications::add_command(const line_state& line)
{
    unsigned int index = static_cast<unsigned int>(m_info.size());

    const std::vector<word>& words = line.get_words();
    for (const auto& word : words)
    {
        m_info.emplace_back();
        auto& info = m_info.back();
        info.start = word.offset;
        info.end = info.start + word.length;
        info.word_class = word_class::invalid;
        info.argmatcher = false;
    }

    return index;
}

//------------------------------------------------------------------------------
void word_classifications::set_word_has_argmatcher(unsigned int index)
{
    if (index < m_info.size())
        m_info[index].argmatcher = true;
}

//------------------------------------------------------------------------------
void word_classifications::finish(bool show_argmatchers)
{
    static const char c_faces[] =
    {
        FACE_OTHER,         // other
        FACE_UNRECOGNIZED,  // unrecognized
        FACE_EXECUTABLE,    // executable
        FACE_COMMAND,       // command
        FACE_ALIAS,         // doskey
        FACE_ARGUMENT,      // arg
        FACE_FLAG,          // flag
        FACE_NONE,          // none
    };
    static_assert(_countof(c_faces) == int(word_class::max), "c_faces and word_class don't agree!");

    for (const auto& info : m_info)
    {
        const size_t end = min<unsigned int>(info.end, m_length);
        for (size_t pos = info.start; pos < end; ++pos)
        {
            if (m_faces[pos] == FACE_SPACE)
            {
                if (info.argmatcher && show_argmatchers)
                    m_faces[pos] = FACE_ARGMATCHER;
                else if (info.word_class < word_class::max)
                    m_faces[pos] = c_faces[int(info.word_class)];
            }
        }
    }
}

//------------------------------------------------------------------------------
bool word_classifications::equals(const word_classifications& other) const
{
    if (!m_faces && !other.m_faces)
        return true;
    if (!m_faces || !other.m_faces)
        return false;

    if (m_face_definitions.size() != other.m_face_definitions.size())
        return false;
    if (m_length != other.m_length)
        return false;
    if (strncmp(m_faces, other.m_faces, m_length) != 0)
        return false;

    for (size_t ii = m_face_definitions.size(); ii--;)
    {
        if (!m_face_definitions[ii].equals(other.m_face_definitions[ii].c_str()))
            return false;
    }

    return true;
}

//------------------------------------------------------------------------------
bool word_classifications::get_word_class(unsigned int index, word_class& wc) const
{
    if (index >= m_info.size())
        return false;

    wc = m_info[index].word_class;
    return (wc < word_class::max);
}

//------------------------------------------------------------------------------
char word_classifications::get_face(unsigned int pos) const
{
    if (pos < 0 || pos >= m_length)
        return ' ';
    return m_faces[pos];
}

//------------------------------------------------------------------------------
const char* word_classifications::get_face_output(char face) const
{
    unsigned int index = static_cast<unsigned char>(face) - 128;
    if (index >= m_face_definitions.size())
        return nullptr;
    return m_face_definitions[index].c_str();
}

//------------------------------------------------------------------------------
char word_classifications::ensure_face(const char* sgr)
{
    const auto entry = m_face_map.find(sgr);
    if (entry != m_face_map.end())
        return entry->second;

    static_assert(face_base >= 128, "face base must be >= 128");
    static_assert(face_base + face_max <= 256, "the max number of faces must fit in a char");
    if (m_face_definitions.size() >= face_max)
        return '\0';

    char face = char(face_base + m_face_definitions.size());
    m_face_definitions.emplace_back(sgr);
    m_face_map.emplace(m_face_definitions.back().c_str(), face);
    return face;
}

//------------------------------------------------------------------------------
void word_classifications::apply_face(unsigned int start, unsigned int length, char face, bool overwrite)
{
    while (length > 0 && start < m_length)
    {
        if (overwrite || m_faces[start] == ' ')
            m_faces[start] = face;
        start++;
        length--;
    }
}

//------------------------------------------------------------------------------
void word_classifications::classify_word(unsigned int index, char wc, bool overwrite)
{
    assert(index < m_info.size());
    if (overwrite || !is_word_classified(index))
        m_info[index].word_class = to_word_class(wc);
}

//------------------------------------------------------------------------------
bool word_classifications::is_word_classified(unsigned int word_index)
{
    return (word_index < m_info.size() && m_info[word_index].word_class < word_class::max);
}
