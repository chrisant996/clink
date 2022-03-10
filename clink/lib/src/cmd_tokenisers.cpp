// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "cmd_tokenisers.h"
#include "alias_cache.h"

#include <core/base.h>
#include <core/os.h>
#include <core/settings.h>

extern setting_bool g_enhanced_doskey;

//------------------------------------------------------------------------------
enum input_type { iTxt, iSpc, iDig, iIn, iOut, iAmp, iPipe, iMAX };
static input_type get_input_type(int c)
{
    switch (c)
    {
    default:
        return iTxt;
    case ' ':
    case '\t':
    case '\0':
        return iSpc;
    case '0':   case '1':   case '2':   case '3':   case '4':
    case '5':   case '6':   case '7':   case '8':   case '9':
        return iDig;
    case '<':
        return iIn;
    case '>':
        return iOut;
    case '&':
        return iAmp;
    case '|':
        return iPipe;
    }
}

//------------------------------------------------------------------------------
// State machine for finding command breaks.
enum tokeniser_state  { sTxt, sSpc, sDig, sIn, sOut, sIn2, sOut2, sDIn, sDOut, sDIn2, sDOut2, sReAm, sAmDi, sBREAK, sARG, sVALID, sBAD, sMAX };
static const tokeniser_state c_transition[][input_type::iMAX] =
{   //                  iTxt,   iSpc,   iDig,   iIn,    iOut,   iAmp,   sPipe,
    /* sTxt */        { sTxt,   sSpc,   sTxt,   sIn,    sOut,   sBREAK, sBREAK, },
    /* sSpc */        { sTxt,   sSpc,   sDig,   sIn,    sOut,   sBREAK, sBREAK, },
    /* sDig */        { sTxt,   sSpc,   sTxt,   sDIn,   sDOut,  sBREAK, sBREAK, },
    /* sIn */         { sARG,   sARG,   sARG,   sIn2,   sBAD,   sBAD,   sBAD,   },
    /* sOut */        { sARG,   sARG,   sARG,   sBAD,   sOut2,  sBAD,   sBAD,   },
    /* sIn2 */        { sARG,   sARG,   sARG,   sBAD,   sBAD,   sBAD,   sBAD,   },
    /* sOut2 */       { sARG,   sARG,   sARG,   sBAD,   sBAD,   sBAD,   sBAD,   },
    /* sDIn */        { sARG,   sARG,   sARG,   sDIn2,  sBAD,   sReAm,  sBAD,   },
    /* sDOut */       { sARG,   sARG,   sARG,   sBAD,   sDOut2, sReAm,  sBAD,   },
    /* sDIn2 */       { sARG,   sARG,   sARG,   sBAD,   sBAD,   sReAm,  sBAD,   },
    /* sDOut2 */      { sARG,   sARG,   sARG,   sBAD,   sBAD,   sReAm,  sBAD,   },
    /* sReAm */       { sBAD,   sBAD,   sAmDi,  sBAD,   sBAD,   sBAD,   sBAD,   },
    /* sAmDi */       { sVALID, sVALID, sVALID, sVALID, sVALID, sVALID, sVALID, },
};
static_assert(sizeof_array(c_transition) == size_t(tokeniser_state::sBREAK), "array length mismatch");



//------------------------------------------------------------------------------
cmd_tokeniser_impl::cmd_tokeniser_impl()
{
    m_alias_cache = new alias_cache;
}

//------------------------------------------------------------------------------
cmd_tokeniser_impl::~cmd_tokeniser_impl()
{
    delete m_alias_cache;
}

//------------------------------------------------------------------------------
void cmd_tokeniser_impl::begin_line()
{
    m_alias_cache->clear();
}

//------------------------------------------------------------------------------
void cmd_tokeniser_impl::start(const str_iter& iter, const char* quote_pair)
{
    m_iter = iter;
    m_start = iter.get_pointer();
    m_quote_pair = quote_pair;
    m_next_redir_arg = false;
}

//------------------------------------------------------------------------------
char cmd_tokeniser_impl::get_opening_quote() const
{
    if (!m_quote_pair || !m_quote_pair[0])
        return '"';
    return m_quote_pair[0];
}

//------------------------------------------------------------------------------
char cmd_tokeniser_impl::get_closing_quote() const
{
    if (!m_quote_pair || !m_quote_pair[0])
        return '"';
    return (m_quote_pair && m_quote_pair[0]) ? m_quote_pair[0] : '"';
}



//------------------------------------------------------------------------------
static const char c_rem_matcher[4] = { 'r', 'e', 'm' };
static const char c_REM_matcher[4] = { 'R', 'E', 'M' };
static const char c_rem_end[] = "&(=+[]\\|;:,.<>/ \t";

//------------------------------------------------------------------------------
int skip_leading_parens(str_iter& iter, bool& first, alias_cache* alias_cache)
{
    int parens = 0;
    bool do_parens = true;

    if ((first || g_enhanced_doskey.get()) && iter.more() && iter.peek() == '(')
    {
        str<> tmp;
        str<> tmp2;
        const char* orig = iter.get_pointer();
        while (iter.more())
        {
            const int c = iter.peek();
            if (c == ' ' || c == '\t')
                break;
            iter.next();
        }
        tmp.concat(orig, static_cast<unsigned int>(iter.get_pointer() - orig));
        do_parens = (alias_cache ?
                     !alias_cache->get_alias(tmp.c_str(), tmp2) :
                     !os::get_alias(tmp.c_str(), tmp2));
        iter.reset_pointer(orig);
    }

    if (do_parens)
    {
        const char* orig = iter.get_pointer();
        while (iter.more())
        {
            const int c = iter.peek();
            if (c != ' ' && c != '(')
                break;
            iter.next();
            if (c == '(')
            {
                first = false;
                if (iter.more() && iter.peek() == ' ')
                    iter.next();
                orig = iter.get_pointer();
                parens++;
            }
        }
        iter.reset_pointer(orig);
    }

    return parens;
}

//------------------------------------------------------------------------------
unsigned int trim_trailing_parens(const char* start, unsigned int offset, unsigned int length, int parens)
{
    unsigned int ret = length;

    // Skip trailing parens to match skipped leading parens.
    while (parens > 0 && length > offset)
    {
        length--;
        if (start[length] == ')')
        {
            parens--;
            ret = length;
        }
        else if (start[length] != ' ')
            break;
    }

    return ret;
}

//------------------------------------------------------------------------------
word_token cmd_command_tokeniser::next(unsigned int& offset, unsigned int& length)
{
    if (!m_iter.more())
        return word_token(word_token::invalid_delim);

    const char oq = get_opening_quote();
    const char cq = get_closing_quote();

    // Skip past any separators.
    while (m_iter.more())
    {
        const int c = m_iter.peek();
        if (c != '&' && c != '|')
            break;
        m_iter.next();
    }

    bool first = (m_iter.get_pointer() == m_start);
    int parens = skip_leading_parens(m_iter, first, m_alias_cache);

    // Eat padding space after command separate or open paren.
    if (!first && !parens && m_iter.more() && m_iter.peek() == ' ')
        m_iter.next();

    offset = static_cast<unsigned int>(m_iter.get_pointer() - m_start);

    int c = 0;
    bool in_quote = false;
    bool is_break = false;
    tokeniser_state state = sSpc;
    int rem_state = 0;
    while (m_iter.more())
    {
        c = m_iter.next();

        if (in_quote)
        {
            if (c == cq)
            {
                assert(state == sTxt);
                in_quote = false;
            }
            else if (c == '^')
            {
                m_iter.next();
            }
        }
        else
        {
            if (c == '^')
            {
                m_iter.next();
            }

            const input_type input = get_input_type(c);
            tokeniser_state new_state = c_transition[state][input];

            if (new_state == sARG || new_state == sVALID || new_state == sBAD)
            {
                state = sSpc;
                new_state = c_transition[state][input];
            }

            if (rem_state < 0)
            {
                // Not a 'rem' command.
            }
            else if (rem_state == 3)
            {
                if (!(c & ~0x7f) && strchr(c_rem_end, static_cast<unsigned char>(c)))
                {
                    // It's a 'rem' command, so consume the rest of the line.
                    while (m_iter.more())
                        m_iter.next();
                    c = 0;
                    break;
                }
                rem_state = -1;
            }
            else if (rem_state == 0 && (new_state > sDig || c == ' ' || c == '\t' || c == '(' || c == '@'))
            {
                // Not text to be tested for 'rem'.
            }
            else if (c == c_rem_matcher[rem_state] || c == c_REM_matcher[rem_state])
                rem_state++;
            else
                rem_state = -1;

            if (new_state == sBREAK)
            {
                is_break = true;
                break;
            }

            if (c == oq)
            {
                assert(new_state == sTxt);
                in_quote = true;
            }

            state = new_state;
        }
    }

    length = static_cast<unsigned int>(m_iter.get_pointer() - m_start) - is_break;
    length = trim_trailing_parens(m_start, offset, length, parens);
    assert(length >= offset);
    length -= offset;

    return word_token(m_iter.more() ? c : 0);
}

//------------------------------------------------------------------------------
bool cmd_command_tokeniser::has_deprecated_argmatcher(const char* command)
{
    extern bool host_has_deprecated_argmatcher(const char* command);
    return host_has_deprecated_argmatcher(command);
}



//------------------------------------------------------------------------------
word_token cmd_word_tokeniser::next(unsigned int& offset, unsigned int& length)
{
    if (!m_iter.more())
        return word_token(word_token::invalid_delim);

    const char oq = get_opening_quote();
    const char cq = get_closing_quote();

    const char* start_word;
    const char* end_word;
    bool redir_arg;

    auto start_new_word = [this, &start_word, &end_word, &offset, &redir_arg]()
    {
        // Skip past any separators.
        while (m_iter.more())
        {
            const int c = m_iter.peek();
            if ((c & ~0xff) || !strchr(" \t=;", c))
                break;
            m_iter.next();
        }
        // Set offset and end of word.
        start_word = end_word = m_iter.get_pointer();
        offset = static_cast<unsigned int>(end_word - m_start);
        // Pick up carried redir_arg.
        redir_arg = m_next_redir_arg;
        m_next_redir_arg = false;
    };

    start_new_word();

    int c = 0;
    bool in_quote = false;
    tokeniser_state state = sSpc;
    while (true)
    {
        if (in_quote)
        {
            if (!m_iter.more())
                break;

            c = m_iter.next();

            if (c == cq)
            {
                assert(state == sTxt);
                in_quote = false;
            }
            else if (c == '^')
            {
                m_iter.next();
            }

            end_word = m_iter.get_pointer();
        }
        else
        {
            c = m_iter.peek();

            input_type input = get_input_type(c);
            tokeniser_state new_state = c_transition[state][input];
            if (new_state == sBREAK) // 'rem' can lead to this.
                new_state = sTxt;

            // sARG, sVALID, and sBAD mean end_word onwards is a redirection
            // token and c starts a new token.
            if (new_state == sARG || new_state == sVALID || new_state == sBAD)
            {
                m_next_redir_arg = (new_state == sARG);

                // If the word is not empty, return it.
                if (end_word > start_word)
                    break;

                // Else start a new word.
                state = sSpc;
                start_new_word();
                c = m_iter.peek();
                input = get_input_type(c);
                new_state = c_transition[state][input];
                if (new_state == sBREAK) // 'rem' can lead to this.
                    new_state = sTxt;
            }

            // Space after a digit needs to include the digit in the word.
            if (new_state == sSpc && state <= sDig)
                end_word = m_iter.get_pointer();

            // Must handle sARG, etc before halting, so "foo >" registers the
            // second (and empty) word as a redir_arg.
            if (!m_iter.more())
                break;

            m_iter.next();
            if (c == '^')
                m_iter.next();

            // Space or equal or semicolon is a word break.
            if (new_state == sSpc)
                break;
            if (new_state == sTxt && (c == '=' || c == ';'))
                break;

            // Normal text always updates the end of the word.
            if (new_state == sTxt)
                end_word = m_iter.get_pointer();

            if (c == oq)
            {
                assert(new_state == sTxt);
                in_quote = true;
            }

            state = new_state;
        }
    }

    length = static_cast<unsigned int>(end_word - start_word);

    return word_token(m_iter.more() ? c : 0, redir_arg);
}
