// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "errfile_reader.h"
#include <core/str.h>
#include <assert.h>

//------------------------------------------------------------------------------
errfile_reader::errfile_reader()
{
}

//------------------------------------------------------------------------------
errfile_reader::~errfile_reader()
{
    if (m_file)
        fclose(m_file);
}

//------------------------------------------------------------------------------
bool errfile_reader::open(const char* name)
{
    assert(!m_file);
    m_file = fopen(name, "rb");
    m_utf16 = -1;
    return !!m_file;
}

//------------------------------------------------------------------------------
bool errfile_reader::next(str_base& out)
{
    assert(m_file);

    out.clear();
    m_buffer.clear();

    if (feof(m_file))
        return false;

    if (m_utf16 < 0)
    {
        const int c1 = fgetc(m_file);
        if (c1 == EOF)
            return false;

        const int c2 = fgetc(m_file);
        if (c2 == EOF)
        {
            m_utf16 = false;
            if (c1 == '\n')
                return true;
            ungetc(c1, m_file);
        }
        else
        {
            // First line is always numeric, which significantly simplifies
            // how to detect UTF16 output.
            m_utf16 = !c2;
            if (m_utf16)
            {
                const wchar_t c = uint8(c1) | (uint16(c2) >> 8);
                if (c == '\n')
                    return true;
                m_buffer.concat(&c, 1);
            }
            else
            {
                ungetc(c2, m_file);
                if (c1 == '\n')
                    return true;
                const char c = char(c1);
                out.concat(&c, 1);
            }
        }
    }

    if (m_utf16)
    {
        int c1 = fgetc(m_file);
        if (c1 == EOF)
            return false;
        int c2 = fgetc(m_file);
        if (c2 == EOF)
            return false;
        while (true)
        {
            const wchar_t c = uint8(c1) | (uint16(c2) << 8);
            if (!c || c == '\n')
                break;
            m_buffer.concat(&c, 1);
            c1 = fgetc(m_file);
            if (c1 == EOF)
                break;
            c2 = fgetc(m_file);
            if (c2 == EOF)
                break;
        }
        out = m_buffer.c_str();
    }
    else
    {
        char c = fgetc(m_file);
        if (c == EOF)
            return false;
        while (c != EOF && c && c != '\n')
        {
            out.concat(&c, 1);
            c = fgetc(m_file);
        }
    }

    if (out.length() && out.c_str()[out.length() - 1] == '\r')
        out.truncate(out.length() - 1);

    if (!m_utf16)
    {
        // Convert ACP -> UTF16 -> UTF8.
        DWORD cch = MultiByteToWideChar(CP_ACP, 0, out.c_str(), -1, nullptr, 0);
        if (cch && m_buffer.reserve(cch / sizeof(wchar_t)))
        {
            cch = MultiByteToWideChar(CP_ACP, 0, out.c_str(), -1, m_buffer.data(), cch);
            if (cch)
                out = m_buffer.c_str();
        }
    }

    return true;
}
