// Copyright (c) 2022-2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class input_params
{
public:
    bool            get(uint32 param, uint32& value) const;
    uint32          count() const;
    short           length() const;
protected:
    bool            add(unsigned short value, uint8 len);
    void            clear();
private:
    unsigned short  m_params[4];
    uint8           m_num = 0;
    short           m_len = 0;
};
