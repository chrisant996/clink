// Copyright (c) 2022-2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class input_params
{
public:
    bool            get(unsigned int param, unsigned int& value) const;
    unsigned int    count() const;
    short           length() const;
protected:
    bool            add(unsigned short value, unsigned char len);
    void            clear();
private:
    unsigned short  m_params[4];
    unsigned char   m_num = 0;
    short           m_len = 0;
};
