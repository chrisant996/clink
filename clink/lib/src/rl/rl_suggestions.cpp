// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_buffer.h"
#include "line_state.h"
#include "display_readline.h"

#include "matches_impl.h"
#include "rl_suggestions.h"

#include <core/base.h>
#include <core/str_compare.h>
#include <core/settings.h>
#include <terminal/ecma48_iter.h>

extern "C" {
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
}

//------------------------------------------------------------------------------
setting_bool g_autosuggest_async(
    "autosuggest.async",
    "Enable asynchronous suggestions",
    "The default is 'true'.  When this is 'true' matches are generated\n"
    "asynchronously for suggestions.  This helps to keep typing responsive.",
    true);

setting_bool g_autosuggest_enable(
    "autosuggest.enable",
    "Enable automatic suggestions",
    "The default is 'true'.  When this is 'true' a suggested command may appear\n"
    "in the 'color.suggestion' color after the cursor.  If the suggestion isn't\n"
    "what you want, just ignore it.  Or accept the whole suggestion with the Right\n"
    "arrow or End key, accept the next word of the suggestion with Ctrl+Right, or\n"
    "accept the next full word of the suggestion up to a space with Shift+Right.\n"
    "The 'autosuggest.strategy' setting determines how a suggestion is chosen.",
    true);

setting_bool g_autosuggest_hint(
    "autosuggest.hint",
    "Show usage hint for automatic suggestions",
    "The default is 'true'.  When this and 'autosuggest.enable' are both 'true'\n"
    "and a suggestion is available, show a usage hint '[Right]-Accept Suggestion'\n"
    "to help make the feature more discoverable and easy to use.  Set this to\n"
    "'false' to hide the usage hint.",
    true);

static setting_bool g_original_case(
    "autosuggest.original_case",
    "Accept original capitalization",
    "When this is enabled (the default), accepting a suggestion uses the\n"
    "original capitalization from the suggestion.",
    true);

static setting_str g_autosuggest_strategy(
    "autosuggest.strategy",
    "Controls how suggestions are chosen",
    "This determines how suggestions are chosen.  The suggestion generators are\n"
    "tried in the order listed, until one provides a suggestion.  There are three\n"
    "built-in suggestion generators, and scripts can provide new ones.\n"
    "'history' chooses the most recent matching command from the history.\n"
    "'completion' chooses the first of the matching completions.\n"
    "'match_prev_cmd' chooses the most recent matching command whose preceding\n"
    "history entry matches the most recently invoked command, but only when\n"
    "the 'history.dupe_mode' setting is 'add'.",
    "match_prev_cmd history completion");

//------------------------------------------------------------------------------
extern line_buffer* g_rl_buffer;
extern setting_enum g_ignore_case;
extern setting_bool g_fuzzy_accent;

//------------------------------------------------------------------------------
bool suggestion_manager::more() const
{
    return m_iter.more();
}

//------------------------------------------------------------------------------
bool suggestion_manager::get_visible(str_base& out) const
{
    out.clear();
    if (!g_rl_buffer)
        return false;

    // Do not allow relaxed comparison for suggestions, as it is too confusing,
    // as a result of the logic to respect original case.
    int32 scope = g_ignore_case.get() ? str_compare_scope::caseless : str_compare_scope::exact;
    str_compare_scope compare(scope, g_fuzzy_accent.get());

    str_iter orig(g_rl_buffer->get_buffer() + m_suggestion_offset);
    str_iter sugg(m_suggestion.c_str(), m_suggestion.length());
    str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(orig, sugg);
    if (orig.more())
        return false;

    out.concat(g_rl_buffer->get_buffer(), uint32(orig.get_pointer() - g_rl_buffer->get_buffer()));
    out.concat(sugg.get_pointer(), sugg.length());

#ifdef USE_SUGGESTION_HINT_INLINE
    if (g_autosuggest_hint.get())
    {
#ifdef RIGHT_ALIGN_SUGGESTION_HINT
        COORD size = measure_readline_display(rl_prompt, out.c_str(), out.length());
        static const uint32 hint_cols = cell_count(STR_SUGGESTION_HINT_INLINE) + 1;
        if (size.X + hint_cols >= _rl_screenwidth)
        {
            concat_spaces(out, _rl_screenwidth - size.X);
            size.X = 0;
        }
        concat_spaces(out, _rl_screenwidth - (size.X + hint_cols));
#endif // RIGHT_ALIGN_SUGGESTION_HINT
        out.concat(STR_SUGGESTION_HINT_INLINE);
    }
#endif  // USE_SUGGESTION_HINT_INLINE

    return true;
}

//------------------------------------------------------------------------------
void suggestion_manager::clear()
{
    if (m_iter.more() && g_rl_buffer)
        g_rl_buffer->set_need_draw();

    new (&m_iter) str_iter();
    m_suggestion.free();
    m_line.free();
    m_started.free();
    m_suggestion_offset = -1;
    m_endword_offset = -1;
    m_suppress = false;
}

//------------------------------------------------------------------------------
bool suggestion_manager::can_suggest(const line_state& line)
{
    assert(g_autosuggest_enable.get());

    if (!g_rl_buffer)
        return false;

    if (m_paused)
        return false;

    if (RL_ISSTATE(RL_STATE_NSEARCH))
    {
        clear();
        g_rl_buffer->set_need_draw();
        g_rl_buffer->draw();
        return false;
    }

    assert(line.get_cursor() == g_rl_buffer->get_cursor());
    assert(line.get_length() == g_rl_buffer->get_length());
    assert(strncmp(line.get_line(), g_rl_buffer->get_buffer(), line.get_length()) == 0);
    if (g_rl_buffer->get_cursor() != g_rl_buffer->get_length())
    {
        clear();
        return false;
    }
    if (g_rl_buffer->get_anchor() >= 0)
        return false;

    const bool diff = (m_line.length() != g_rl_buffer->get_length() ||
                       strncmp(m_line.c_str(), g_rl_buffer->get_buffer(), m_line.length()) != 0);

    // Must check this AFTER checking cursor at end, so that moving the cursor
    // can clear the flag.
    if (is_suppressing_suggestions())
    {
        if (rl_last_func == rl_rubout ||
            rl_last_func == rl_backward_kill_word ||
            rl_last_func == rl_backward_kill_line)
        {
            suppress_suggestions();
            return false;
        }

        if (diff)
            clear();
        else
            return false;
    }

    // Update the endword offset.  Inserting part of a suggestion can't know
    // what the new endword offset will be, so this allows updating it when the
    // line editor is considering whether to generate a new suggestion.
    m_endword_offset = line.get_end_word_offset();

    // The buffers are not necessarily nul terminated!  Because of how
    // hook_display() hacks suggestions into the Readline display.
    return diff;
}

//------------------------------------------------------------------------------
bool suggestion_manager::can_update_matches()
{
    const bool diff = (m_started.length() != g_rl_buffer->get_length() ||
                       strncmp(m_started.c_str(), g_rl_buffer->get_buffer(), m_started.length()) != 0);
    return diff;
}

//------------------------------------------------------------------------------
void suggestion_manager::suppress_suggestions()
{
    clear();
    m_line.concat(g_rl_buffer->get_buffer(), g_rl_buffer->get_length());
    m_started.concat(g_rl_buffer->get_buffer(), g_rl_buffer->get_length());
    m_suppress = true;
}

//------------------------------------------------------------------------------
void suggestion_manager::set_started(const char* line)
{
    m_started = line;

#ifdef DEBUG_SUGGEST
    printf("\x1b[s\x1b[2Hstarted:  \"%s\"\x1b[K\x1b[u", m_started.c_str());
#endif
}

//------------------------------------------------------------------------------
void suggestion_manager::set(const char* line, uint32 endword_offset, const char* suggestion, uint32 offset)
{
#ifdef DEBUG_SUGGEST
    static int32 s_suggnum = 0;
    printf("\x1b[s\x1b[H#%u:  set suggestion:  \"%s\", offset %d, endword ofs %d\x1b[K\x1b[u",
           ++s_suggnum, suggestion, offset, endword_offset);
#endif

    if ((m_suggestion.length() == 0) == (!suggestion || !*suggestion) &&
        m_suggestion_offset == offset &&
        m_endword_offset == endword_offset &&
        (!suggestion || m_suggestion.equals(suggestion)) &&
        m_line.equals(line))
    {
        return;
    }

    if (!suggestion || !*suggestion)
    {
malformed:
        clear();
        m_line = line;
        m_started = line;
        if (g_rl_buffer)
            g_rl_buffer->draw();
        return;
    }

    const uint32 line_len = uint32(strlen(line));
    if (line_len < offset)
    {
        assert(false);
        goto malformed;
    }

    m_suggestion = suggestion;
    new (&m_iter) str_iter(m_suggestion.c_str(), m_suggestion.length());

    // Do not allow relaxed comparison for suggestions, as it is too confusing,
    // as a result of the logic to respect original case.
    {
        int32 scope = g_ignore_case.get() ? str_compare_scope::caseless : str_compare_scope::exact;
        str_compare_scope compare(scope, g_fuzzy_accent.get());

        str_iter orig(line + offset);
        const int32 matchlen = str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(orig, m_iter);

        if (orig.more())
            goto malformed;
    }

    m_suggestion_offset = offset;
    m_endword_offset = endword_offset;

    m_line = line;
    m_started = line;

#ifdef DEBUG_SUGGEST
    printf("\x1b[s\x1b[2Hline:     \"%s\"\x1b[K\x1b[u", m_line.c_str());
#endif

    if (g_rl_buffer)
    {
        g_rl_buffer->set_need_draw();
        g_rl_buffer->draw();
    }
}

//------------------------------------------------------------------------------
static bool is_suggestion_word_break(int32 c)
{
    return c == ' ' || c == '\t';
}

//------------------------------------------------------------------------------
void suggestion_manager::resync_suggestion_iterator(uint32 old_cursor)
{
    const int32 consume = g_rl_buffer->get_cursor() - old_cursor;
    assert(consume >= 0);

    const char* const start = m_iter.get_pointer();
    while (int32(m_iter.get_pointer() - start) < consume)
        m_iter.next();
}

//------------------------------------------------------------------------------
bool suggestion_manager::insert(suggestion_action action)
{
    if (!m_iter.more() || g_rl_buffer->get_cursor() != g_rl_buffer->get_length())
        return false;

    // Do not allow relaxed comparison for suggestions, as it is too confusing,
    // as a result of the logic to respect original case.
    int32 scope = g_ignore_case.get() ? str_compare_scope::caseless : str_compare_scope::exact;
    str_compare_scope compare(scope, g_fuzzy_accent.get());

    const bool original_case = g_original_case.get();

    // Special case when inserting to the end and the suggestion covers the
    // entire line.  Replace the entire line to use the original capitalization.
    // If the suggestion doesn't match up through the end of the line, then it's
    // malformed and must be discarded.
    if (original_case && action == suggestion_action::insert_to_end && m_suggestion_offset == 0)
    {
        str_iter orig_iter(g_rl_buffer->get_buffer(), g_rl_buffer->get_length());
        str_iter sugg_iter(m_suggestion.c_str(), m_suggestion.length());
        str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(orig_iter, sugg_iter);
        if (!orig_iter.more() && sugg_iter.more())
        {
            g_rl_buffer->begin_undo_group();
            g_rl_buffer->remove(0, g_rl_buffer->get_length());
            g_rl_buffer->insert(m_suggestion.c_str());
            g_rl_buffer->end_undo_group();
        }
        suppress_suggestions();
        return !orig_iter.more();
    }

    // Reset suggestion iterator.
    new (&m_iter) str_iter(m_suggestion.c_str(), m_suggestion.length());

    // Consume the suggestion iterator up to the end word offset.
    if (m_suggestion_offset < m_endword_offset)
    {
        str_iter orig_iter(g_rl_buffer->get_buffer() + m_suggestion_offset, m_endword_offset - m_suggestion_offset);
        str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(orig_iter, m_iter);
        if (orig_iter.more())
        {
            // Early mismatch!  The suggester returned an invalid suggestion.
            assert(false);
            clear();
            return false;
        }
    }

    // Find the offset at which to replace with suggestion text.
    uint32 replace_offset = m_endword_offset;
    const char* insert = m_iter.get_pointer();

    // Track quotes between end word offset and cursor (end of line).
    bool quote = false;
    {
        const uint32 len = g_rl_buffer->get_length();
        if (replace_offset > 0)
            quote = (g_rl_buffer->get_buffer()[replace_offset - 1] == '"');
        if (replace_offset < len)
        {
            str_iter orig_iter(g_rl_buffer->get_buffer() + replace_offset, g_rl_buffer->get_length() - replace_offset);
            const char* const inflection = g_rl_buffer->get_buffer() + m_suggestion_offset;
            const char* const target = g_rl_buffer->get_buffer() + len;
            while (orig_iter.get_pointer() < target)
            {
                int32 c = orig_iter.next();
                if (orig_iter.get_pointer() > inflection)
                    m_iter.next();
                if (c == '"')
                    quote = !quote;
            }
        }
    }

    unsigned end_offset = g_rl_buffer->get_length();
    if (original_case)
    {
        end_offset = m_suggestion_offset + int32(m_iter.get_pointer() - m_suggestion.c_str());
    }
    else
    {
        replace_offset = end_offset;
        insert = m_iter.get_pointer();
    }

    // Begin an undo group and replace the rest of the line with the suggestion.
    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(replace_offset, g_rl_buffer->get_length());
    g_rl_buffer->insert(insert);

    // Truncate the line if appropriate.
    bool trunc = false;
    if (action != suggestion_action::insert_to_end)
    {
        g_rl_buffer->set_cursor(end_offset);

        if (action == suggestion_action::insert_next_full_word)
        {
            const char* const insert = m_iter.get_pointer();

            // Skip spaces.
            while (int32 c = m_iter.peek())
            {
                if (!is_suggestion_word_break(c))
                    break;
                m_iter.next();
            }

            // Skip non-spaces.
            while (int32 c = m_iter.peek())
            {
                if (c == '"')
                    quote = !quote;
                else if (!quote && is_suggestion_word_break(c))
                    break;
                m_iter.next();
            }

            const int32 len = int32(m_iter.get_pointer() - insert);
            g_rl_buffer->set_cursor(g_rl_buffer->get_cursor() + len);
        }
        else if (action == suggestion_action::insert_next_word)
        {
            // Skip forward a word.
            rl_forward_word(1, 0);

            // Resync the suggestion iterator.
            resync_suggestion_iterator(end_offset);
        }

        trunc = (g_rl_buffer->get_cursor() < g_rl_buffer->get_length());
        if (trunc)
            g_rl_buffer->remove(g_rl_buffer->get_cursor(), g_rl_buffer->get_length());
    }

    // End the undo group.
    g_rl_buffer->end_undo_group();

    if (!trunc)
    {
        suppress_suggestions();
    }
    else
    {
        m_line.clear();
        m_line.concat(g_rl_buffer->get_buffer(), g_rl_buffer->get_length());
        m_started.clear();
        m_started.concat(g_rl_buffer->get_buffer(), g_rl_buffer->get_length());
    }

    return true;
}

//------------------------------------------------------------------------------
bool suggestion_manager::pause(bool pause)
{
    const bool was_paused = m_paused;
    m_paused = pause;
    return was_paused;
}
