// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class line_editor;

//------------------------------------------------------------------------------
class host
{
public:
                    host(line_editor* editor);
    virtual         ~host();
    virtual bool    validate() = 0;
    virtual bool    initialise() = 0;
    virtual void    shutdown() = 0;
    line_editor*    get_line_editor() const;

private:
    line_editor*    m_line_editor;
};

//------------------------------------------------------------------------------
inline host::host(line_editor* editor)
: m_line_editor(editor)
{
}

//------------------------------------------------------------------------------
inline host::~host()
{
}

//------------------------------------------------------------------------------
inline line_editor* host::get_line_editor() const
{
    return m_line_editor;
}
