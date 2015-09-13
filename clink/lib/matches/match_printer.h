// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class terminal;
class matches;

//------------------------------------------------------------------------------
class match_printer
{
public:
                    match_printer(terminal* terminal);
    virtual         ~match_printer();
    virtual void    print(const matches& matches) = 0;

private:
    terminal*       get_terminal() const;
    terminal*       m_terminal;
};

//------------------------------------------------------------------------------
inline match_printer::match_printer(terminal* terminal)
: m_terminal(terminal)
{
}

//------------------------------------------------------------------------------
inline match_printer::~match_printer()
{
}

//------------------------------------------------------------------------------
inline terminal* match_printer::get_terminal() const
{
    return m_terminal;
}
