// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "cmd_tokenisers.h"
#include "alias_cache.h"
#include "line_editor_integration.h"

#include <core/base.h>
#include <core/os.h>
#include <core/settings.h>
#include <core/debugheap.h>

#include <assert.h>

extern setting_bool g_enhanced_doskey;

//------------------------------------------------------------------------------
const char* const c_cmd_exes[] =
{   // These are executables that pretend to be built-in commands.
    "attrib", "chcp", "format", "help", "more", "subst", "tasklist", "taskkill",
    nullptr
};

const char* const c_cmd_commands_basicwordbreaks[] =
{   // These treat special word break characters as part of the input.
    "assoc", "color", "ftype", "if", "set", "ver", "verify",
    nullptr
};

const char* const c_cmd_commands_shellwordbreaks[] =
{   // These treat special word break characters as ignored delimiters.
    "break", "call", "cd", "chdir", "cls", "copy", "date", "del", "dir",
    "dpath", "echo", "endlocal", "erase", "exit", "for", "goto", "md",
    "mkdir", "mklink", "move", "path", "pause", "popd", "prompt", "pushd",
    "rd", "rem", "ren", "rename", "rmdir", "setlocal", "shift", "start",
    "time", "title", "type", "vol",
    nullptr
};

//------------------------------------------------------------------------------
enum input_type { iTxt, iSpc, iDig, iIn, iOut, iAmp, iPipe, iMAX };
static input_type get_input_type(int32 c)
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
enum tokeniser_state : int32 { sTxt, sSpc, sDig, sIn, sOut, sIn2, sOut2, sDIn, sDOut, sDIn2, sDOut2, sReAm, sAmDi, sBREAK, sARG, sVALID, sBAD, sMAX };
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
void cmd_tokeniser_impl::start(const str_iter& iter, const char* quote_pair, bool at_beginning)
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
const char* const cmd_state::c_command_delimiters = "&(=+[]\\|;:,.<>/ \t";

//------------------------------------------------------------------------------
state_flag is_cmd_command(const char* word)
{
    dbg_ignore_scope(snapshot, "is_cmd_command"); // (s_map ctor allocates.)
    static str_map_caseless<state_flag>::type s_map;

    if (s_map.size() == 0)
    {
        // Internal commands in CMD get special word break treatment for the
        // first word break.
        s_map.emplace("rem", flag_rem|flag_internal|flag_specialwordbreaks);
        for (const char* const* cmd = c_cmd_commands_basicwordbreaks; *cmd; ++cmd)
            s_map.emplace(*cmd, flag_internal);
        for (const char* const* cmd = c_cmd_commands_shellwordbreaks; *cmd; ++cmd)
            s_map.emplace(*cmd, flag_internal|flag_specialwordbreaks);

#ifdef DEBUG
        auto const verify_rem = s_map.find("rem");
        assert(verify_rem != s_map.end() && (verify_rem->second & flag_rem));
#endif
    }

    auto it = s_map.find(word);
    if (it == s_map.end())
    {
        if (!strchr(word, '^'))
            return flag_none;

        str<> tmp;
        for (const char* walk = word; *walk; ++walk)
        {
            if (*walk == '^')
            {
                ++walk;
                if (!*walk)
                    break;
            }
            tmp.concat(walk, 1);
        }

        it = s_map.find(tmp.c_str());
        if (it == s_map.end())
            return flag_none;
    }

    return it->second;
}

//------------------------------------------------------------------------------
void cmd_state::clear()
{
    m_word.clear();
    m_first = true;
    m_failed = false;
    m_match = false;
    m_match_flag = flag_none;
}

//------------------------------------------------------------------------------
void cmd_state::next_word()
{
    if (m_first)
        m_first = false;
    else
        m_failed = true;
}

//------------------------------------------------------------------------------
bool cmd_state::test(const int32 c, const tokeniser_state new_state)
{
    // Not a command.
    if (m_failed)
        return false;

    // Some characters can precede a command; e.g. `>nul ` or `( ` before `rem`.
    if (!m_word.length() && (new_state > sDig || c == ' ' || c == '\t' || c == '(' || c == '@'))
        return false;

    // If the word is too long or a non-ASCII character is encountered, it can't
    // be a command (i.e. CMD internal command).
    if (m_word.length() > 8 || (c & ~0x7f))
    {
        cancel();
        return false;
    }

    // The accumulated word could be a command; is it followed by an appropriate
    // delimiter?
    if (m_match)
    {
        if (strchr(c_command_delimiters, c))
        {
            cancel();
            return true;
        }
        m_match = false;
    }

    // Accumulate the word.
    char ch = char(c);
    m_word.concat(&ch, 1);

    // Test if the accumulated word so far matches a command name.
    const state_flag flag = is_cmd_command(m_word.c_str());
    if (!flag)
        return false;

    // If 'rem' is the only command of interest, short circuit because it is now
    // clear the command word is not 'rem'.
    if (m_only_rem && !(flag & flag_rem))
    {
        cancel();
        return false;
    }

    m_match = true;
    m_match_flag = flag;
    return false;
}



//------------------------------------------------------------------------------
// No double quote, as that would break quoting rules.
static const char c_command_delims[] = "@ \t=;,(/";
static const char c_name_delims[] = "@ \t=;,(";
static const char c_word_delims[] = " \t\n'`=+;,()[]{}";

//------------------------------------------------------------------------------
static const char* get_delims(bool command_word, bool redir_arg, bool in_word)
{
    if (redir_arg || (command_word && !in_word))
        return c_name_delims;
    if (command_word)
        return c_command_delims;
    return c_word_delims;
}

//------------------------------------------------------------------------------
static uint32 is_alias_word(str_iter& iter, alias_cache* alias_cache)
{
    const char* orig = iter.get_pointer();
    while (iter.more())
    {
        const int32 c = iter.peek();
        if (c == ' ' || c == '\t')
            break;
        iter.next();
    }

    str<> tmp;
    tmp.concat(orig, uint32(iter.get_pointer() - orig));
    if (tmp.empty())
        return false;

    str<> tmp2;
    const bool is_alias = (alias_cache ?
                           alias_cache->get_alias(tmp.c_str(), tmp2) :
                           os::get_alias(tmp.c_str(), tmp2));
    iter.reset_pointer(orig);
    return is_alias ? tmp.length() : 0;
}

//------------------------------------------------------------------------------
int32 skip_leading_parens(str_iter& iter, bool& first, alias_cache* alias_cache)
{
    int32 parens = 0;
    bool do_parens = true;

    if ((first || g_enhanced_doskey.get()) && iter.more() && iter.peek() == '(')
        do_parens = !is_alias_word(iter, alias_cache);

    if (do_parens)
    {
        const char* orig = iter.get_pointer();
        while (iter.more())
        {
            const int32 c = iter.peek();
            if (c != ' ' && c != '@' && c != '(')
                break;
            iter.next();
            if (c == '(')
            {
                first = false;
                if (iter.more() && (iter.peek() == ' ' || iter.peek() == '@'))
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
uint32 trim_trailing_parens(const char* start, uint32 offset, uint32 length, int32 parens)
{
    uint32 ret = length;

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
word_token cmd_command_tokeniser::next(uint32& offset, uint32& length)
{
    if (!m_iter.more())
    {
        offset = uint32(m_iter.get_pointer() - m_start);
        length = 0;
        return word_token(word_token::invalid_delim);
    }

    const char oq = get_opening_quote();
    const char cq = get_closing_quote();
    bool first_command = (m_iter.get_pointer() == m_start);

    // After the first command, skip past a separator (&, |, &&, or ||).
    if (!first_command)
    {
        const int32 c1 = m_iter.peek();
        if (c1 == '&' || c1 == '|')
        {
            m_iter.next();
            const int32 c2 = m_iter.peek();
            if (c1 == c2)
                m_iter.next();
        }
    }

    int32 parens = skip_leading_parens(m_iter, first_command, m_alias_cache);

    // Eat padding space after command separator or open paren.
    if (!first_command && !parens && m_iter.more() && m_iter.peek() == ' ')
        m_iter.next();

    offset = uint32(m_iter.get_pointer() - m_start);

    int32 c = 0;
    bool in_quote = false;
    bool is_arg = false;
    bool any_text = false;
    tokeniser_state state = sSpc;
    cmd_state cmd_state(true/*only_rem*/);

    // The first word can contain any non-space characters if it's an alias.
    const uint32 alias_len = is_alias_word(m_iter, m_alias_cache);
    if (alias_len)
    {
        any_text = true;
        for (uint32 n = alias_len; n--;)
            m_iter.next();
        cmd_state.test(0xff, sTxt);
    }

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

            if (new_state == sTxt || new_state == sDig)
            {
                if (!any_text && !str_chr(c_name_delims, c) && c != '&' &&  c != '|')
                {
                    any_text = true;
                    if (input == iDig && new_state == sTxt)
                    {
                        const char* p = m_iter.get_pointer();
                        if (p - 2 >= m_start && *(p - 2) == '@' && m_iter.peek() == '>')
                            m_iter.next();
                    }
                }
            }
            if (new_state == sBREAK && !any_text)
                new_state = sTxt;

            if (new_state == sARG || new_state == sVALID || new_state == sBAD)
            {
                state = sSpc;
                new_state = c_transition[state][input];
                is_arg = true;
            }
            else if (new_state != sTxt)
                is_arg = false;

            if (new_state <= sDig && !is_arg && cmd_state.test(c, new_state))
            {
                // It's a 'rem' command, so consume the rest of the line.
                while (m_iter.more())
                    m_iter.next();
                c = 0;
                break;
            }

            if (new_state == sBREAK)
            {
                m_iter.reset_pointer(m_iter.get_pointer() - 1);
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

    length = uint32(m_iter.get_pointer() - m_start);
    length = trim_trailing_parens(m_start, offset, length, parens);
    assert(length >= offset);
    length -= offset;

    return word_token(m_iter.more() ? c : 0);
}

//------------------------------------------------------------------------------
bool cmd_command_tokeniser::has_deprecated_argmatcher(const char* command)
{
    return ::has_deprecated_argmatcher(command);
}



//------------------------------------------------------------------------------
void cmd_word_tokeniser::start(const str_iter& iter, const char* quote_pair, bool at_beginning)
{
    base::start(iter, quote_pair);
    m_cmd_state.clear();
    m_command_word = at_beginning;
}

//------------------------------------------------------------------------------
word_token cmd_word_tokeniser::next(uint32& offset, uint32& length)
{
    if (!m_iter.more())
        return word_token(word_token::invalid_delim);

    const char oq = get_opening_quote();
    const char cq = get_closing_quote();

    const char* start_word;
    const char* end_word;
    bool redir_arg;
    bool command_word = m_command_word;

    auto start_new_word = [this, &start_word, &end_word, &offset, &redir_arg, &command_word]()
    {
        m_cmd_state.next_word();
        // Pick up carried redir_arg.
        redir_arg = m_next_redir_arg;
        m_next_redir_arg = false;
        // Skip past any separators.
        while (m_iter.more())
        {
            const int32 c = m_iter.peek();
            // TODO: Handle '^'?
            if (!str_chr(get_delims(command_word, redir_arg, false), c))
                break;
            m_iter.next();
        }
        // Set offset and end of word.
        start_word = end_word = m_iter.get_pointer();
        offset = uint32(end_word - m_start);
    };

    start_new_word();

    int32 c = 0;
    bool first_char = true;
    bool first_slash = false;
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

            if (first_char)
                first_slash = (c == '/');

            input_type input = get_input_type(c);
            tokeniser_state new_state = c_transition[state][input];
            if (new_state == sBREAK) // 'rem a&b' leads to this.
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
                // TODO: Handle '^'?
                input = get_input_type(c);
                new_state = c_transition[state][input];
                if (new_state == sBREAK) // 'rem' can lead to this.
                    new_state = sTxt;
            }

            // Text or space after a digit needs to include the digit in the word.
            if (new_state <= sSpc && state <= sDig)
                end_word = m_iter.get_pointer();

            // Must handle sARG, etc before halting, so "foo >" registers the
            // second (and empty) word as a redir_arg.
            if (!m_iter.more())
                break;

            if (c == '^')
            {
                char c2 = *m_iter.get_next_pointer();
                input_type input2 = get_input_type(c2);
                if (input2 == sSpc)
                {
                    c = c2;
                    input = input2;
                    new_state = c_transition[state][input];
                }
            }

            // Internal commands have special word break syntax.
            if (new_state == sTxt && m_cmd_state.test(c, new_state))
                break;

            // Forward slash in a command word is a word break in CMD.
            if (c == '/' && !first_slash && !first_char && command_word && !redir_arg)
                break;

            m_iter.next();
            if (c == '^')
                m_iter.next();

            // Space or equal or semicolon is a word break.
            if (new_state == sSpc)
                break;
            if (new_state == sTxt && str_chr(get_delims(command_word, redir_arg, !first_slash && !first_char), c))
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

        first_char = false;
    }

    length = uint32(end_word - start_word);

    if (!redir_arg)
        m_command_word = false;

    return word_token(m_iter.more() ? c : 0, redir_arg);
}
