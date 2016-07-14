// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class doskey
{
public:
                doskey(const char* shell_name);
                ~doskey();
    bool        add_alias(const char* alias, const char* text);
    bool        remove_alias(const char* alias);
    bool        begin(wchar_t* chars, unsigned max_chars);
    bool        next(wchar_t* chars, unsigned max_chars);

private:
    struct token
    {
        short   start;
        short   length;
    };

    int         tokenise(wchar_t* source, token* tokens, int max_tokens);
    const char* m_shell_name;
    wchar_t*    m_alias_text;
    wchar_t*    m_alias_next;
    wchar_t*    m_input;
    token       m_tokens[10];
    unsigned    m_token_count;
};
