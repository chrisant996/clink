// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class prompt
{
public:
                    prompt();
                    prompt(prompt&& rhs);
                    prompt(const prompt& rhs) = delete;
                    ~prompt();
    prompt&         operator = (prompt&& rhs);
    prompt&         operator = (const prompt& rhs) = delete;
    void            clear();
    const wchar_t*  get() const;
    void            set(const wchar_t* chars, int char_count=0);
    bool            is_set() const;

protected:
    wchar_t*        m_data;
};

//------------------------------------------------------------------------------
class tagged_prompt
    : public prompt
{
public:
    void            set(const wchar_t* chars, int char_count=0);
    void            tag(const wchar_t* value);

private:
    int             is_tagged(const wchar_t* chars, int char_count=0);
};

//------------------------------------------------------------------------------
class prompt_utils
{
public:
    static prompt   extract_from_console();
};
