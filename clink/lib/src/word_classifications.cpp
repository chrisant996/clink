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
    free(m_word_faces);

    delete m_coalesced_test_infos;
    delete m_test_infos;
}

//------------------------------------------------------------------------------
word_classifications::word_classifications(word_classifications&& other)
{
    m_face_definitions = std::move(other.m_face_definitions);
    m_argmatchers = other.m_argmatchers;
    m_faces = other.m_faces;
    m_word_faces = other.m_word_faces;
    m_length = other.m_length;
    m_face_map = std::move(other.m_face_map);
    m_test_infos = other.m_test_infos;

    // Discard coalesced test infos.
    delete m_coalesced_test_infos;
    delete other.m_coalesced_test_infos;
    m_coalesced_test_infos = nullptr;
    other.m_coalesced_test_infos = nullptr;

    other.m_faces = nullptr;        // Transferred ownership above.
    other.m_word_faces = nullptr;   // Transferred ownership above.
    other.m_test_infos = nullptr;   // Transferred ownership above.
    other.clear();
}

//------------------------------------------------------------------------------
void word_classifications::clear()
{
    free(m_faces);
    free(m_word_faces);

    m_face_definitions.clear();
    m_argmatchers = false;
    m_faces = nullptr;
    m_word_faces = nullptr;
    m_length = 0;
    m_face_map.clear();

    delete m_coalesced_test_infos;
    delete m_test_infos;
    m_coalesced_test_infos = nullptr;
    m_test_infos = nullptr;
}

//------------------------------------------------------------------------------
void word_classifications::init(size_t line_length, const word_classifications* face_defs, bool argmatchers)
{
    clear();
    m_argmatchers = argmatchers;

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
        char* faces = static_cast<char*>(malloc(line_length));
        char* word_faces = static_cast<char*>(malloc(line_length));
        if (faces && word_faces)
        {
            m_faces = faces;
            m_word_faces = word_faces;
            m_length = uint32(line_length);
            // Space means not classified; use default color.
            memset(m_faces, FACE_SPACE, line_length);
            memset(m_word_faces, FACE_SPACE, line_length);
        }
        else
        {
            free(faces);
            free(word_faces);
        }
    }
}

//------------------------------------------------------------------------------
void word_classifications::finish()
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
    static_assert(_countof(c_faces) == int32(word_class::max), "c_faces and word_class don't agree!");

    for (uint32 pos = 0; pos < m_length; ++pos)
    {
        if (m_faces[pos] == FACE_SPACE)
            m_faces[pos] = m_word_faces[pos];
    }
}

//------------------------------------------------------------------------------
bool word_classifications::equals(const word_classifications& other) const
{
    if (!m_faces && !m_word_faces && !other.m_faces && !other.m_word_faces)
        return true;
    if (!m_faces || !m_word_faces || !other.m_faces || !other.m_word_faces)
        return false;

    if (m_face_definitions.size() != other.m_face_definitions.size())
        return false;
    if (m_length != other.m_length)
        return false;
    if (strncmp(m_faces, other.m_faces, m_length) != 0 ||
        strncmp(m_word_faces, other.m_word_faces, m_length) != 0)
        return false;

    for (size_t ii = m_face_definitions.size(); ii--;)
    {
        if (!m_face_definitions[ii].equals(other.m_face_definitions[ii].c_str()))
            return false;
    }

    if (!m_test_infos != !other.m_test_infos)
        return false;
    if (m_test_infos)
    {
        if (m_test_infos->size() != other.m_test_infos->size())
            return false;
        for (size_t ii = m_test_infos->size(); ii--;)
        {
            if ((*m_test_infos)[ii].size() != (*other.m_test_infos)[ii].size())
                return false;
            for (size_t jj = (*m_test_infos)[ii].size(); jj--;)
            {
                const auto& ti = (*m_test_infos)[ii][jj];
                const auto& oti = (*other.m_test_infos)[ii][jj];
                if (ti.argmatcher != oti.argmatcher)
                    return false;
                if (ti.word_class != oti.word_class)
                    return false;
            }
        }
    }

    return true;
}

//------------------------------------------------------------------------------
char word_classifications::get_face(uint32 pos) const
{
    if (pos < 0 || pos >= m_length)
        return ' ';
    return m_faces[pos];
}

//------------------------------------------------------------------------------
const char* word_classifications::get_face_output(char face) const
{
    uint32 index = uint8(face) - 128;
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
void word_classifications::apply_face(bool words, uint32 start, uint32 length, char face, bool overwrite)
{
    char* const faces = words ? m_word_faces : m_faces;
    while (length > 0 && start < m_length)
    {
        if (overwrite || faces[start] == FACE_SPACE)
            faces[start] = face;
        start++;
        length--;
    }
}

//------------------------------------------------------------------------------
void word_classifications::classify_word(uint32 index_command, uint32 index_word, char wc, bool argmatcher, bool overwrite)
{
    if (m_coalesced_test_infos)
    {
        delete m_coalesced_test_infos;
        m_coalesced_test_infos = nullptr;
    }

    if (!m_test_infos)
        m_test_infos = new std::vector<word_class_infos>;

    if (index_command >= m_test_infos->size())
        m_test_infos->resize(index_command + 1);

    word_class_infos& infos = (*m_test_infos)[index_command];

    if (index_word >= infos.size())
        infos.resize(index_word + 1);

    word_class_info& info = infos[index_word];
    if (overwrite || info.word_class == word_class::invalid)
    {
        if (argmatcher)
            info.argmatcher = true;
        info.word_class = to_word_class(wc);
    }
}

//------------------------------------------------------------------------------
const word_class_infos* word_classifications::get_test_infos() const
{
    if (m_test_infos)
    {
        if (!m_coalesced_test_infos)
        {
            m_coalesced_test_infos = new word_class_infos;
            if (m_coalesced_test_infos)
            {
                for (const auto& infos : *m_test_infos)
                    m_coalesced_test_infos->insert(m_coalesced_test_infos->end(), infos.begin(), infos.end());
            }
        }
    }
    return m_coalesced_test_infos;
}
