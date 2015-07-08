/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "globber.h"
#include "path.h"

//------------------------------------------------------------------------------
globber::globber(const context& ctx)
: m_context(ctx)
{
    if (m_context.path == nullptr)      m_context.path = "";
    if (m_context.wildcard == nullptr)  m_context.wildcard = "*";

    str<MAX_PATH> glob;
    glob << m_context.path;
    glob << m_context.wildcard;

    wstr<MAX_PATH> wglob(glob.c_str());
    m_handle = FindFirstFileW(wglob.c_str(), &m_data);
    if (m_handle == INVALID_HANDLE_VALUE)
        m_handle = nullptr;

    path::get_directory(glob.c_str(), m_root);
}

//------------------------------------------------------------------------------
globber::~globber()
{
    if (m_handle != nullptr)
        FindClose(m_handle);
}

//------------------------------------------------------------------------------
bool globber::next(str_base& out)
{
    if (m_handle == nullptr)
        return false;

    const wchar_t* c = m_data.cFileName;
    if (c[0] == '.' && (!c[1] || (c[1] == '.' && !c[2])) && !m_context.dots)
    {
        next_file();
        return next(out);
    }

    int attr = m_data.dwFileAttributes;
    if ((attr & FILE_ATTRIBUTE_HIDDEN) && !m_context.hidden)
    {
        next_file();
        return next(out);
    }

    if ((attr & FILE_ATTRIBUTE_DIRECTORY) && m_context.no_directories)
    {
        next_file();
        return next(out);
    }

    if ((attr & ~FILE_ATTRIBUTE_DIRECTORY) && m_context.no_files)
    {
        next_file();
        return next(out);
    }

    str<MAX_PATH> file_name = m_data.cFileName;

    out.clear();
    path::join(m_root.c_str(), file_name.c_str(), out);

    if (attr & FILE_ATTRIBUTE_DIRECTORY && !m_context.no_dir_suffix)
        out << "\\";

    next_file();
    return true;
}

//------------------------------------------------------------------------------
void globber::next_file()
{
    if (FindNextFileW(m_handle, &m_data))
        return;

    FindClose(m_handle);
    m_handle = nullptr;
}
