// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_buffer.h"
#include "line_state.h"
#include "display_readline.h"
#include "matches_impl.h"
#include "suggestions.h"
#include "suggestionlist_impl.h"
#include "line_editor_integration.h"

#include <core/base.h>
#include <core/str_compare.h>
#include <core/settings.h>
#include <rl/rl_commands.h>
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
    "and a suggestion is available, show a usage hint '[Right]-Insert Suggestion'\n"
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
suggestions& suggestions::operator = (const suggestions& other)
{
    clear();
    m_line = other.m_line.c_str();
    for (const auto& s : other.m_items)
        add(s.m_suggestion.c_str(), s.m_suggestion_offset, s.m_source.c_str(), s.m_highlight_offset, s.m_highlight_length, s.m_tooltip.c_str(), s.m_history_index);
    m_generation_id = other.m_generation_id;
    m_dirtied = other.m_dirtied;
    return *this;
}

//------------------------------------------------------------------------------
suggestions& suggestions::operator = (suggestions&& other)
{
    m_line = std::move(other.m_line);
    m_items = std::move(other.m_items);
    m_generation_id = other.m_generation_id;
    m_dirtied = other.m_dirtied;
    other.clear();
    return *this;
}

//------------------------------------------------------------------------------
bool suggestions::is_same(const suggestions& other) const
{
    if (other.m_generation_id != m_generation_id)
        return false;
#ifdef DEBUG
    if (m_generation_id && !m_dirtied && !other.m_dirtied)
    {
        assert(other.m_line.equals(m_line.c_str()));
        assert(other.m_items.size() == m_items.size());
        if (other.m_items.size() == m_items.size())
        {
            for (size_t index = 0; index < m_items.size(); ++index)
            {
                assert(other.m_items[index].m_suggestion.equals(m_items[index].m_suggestion.c_str()));
                assert(other.m_items[index].m_suggestion_offset == m_items[index].m_suggestion_offset);
                assert(other.m_items[index].m_highlight_offset == m_items[index].m_highlight_offset);
                assert(other.m_items[index].m_highlight_length == m_items[index].m_highlight_length);
                assert(other.m_items[index].m_tooltip.equals(m_items[index].m_tooltip.c_str()));
                assert(other.m_items[index].m_source.equals(m_items[index].m_source.c_str()));
                assert(other.m_items[index].m_history_index == m_items[index].m_history_index);
            }
        }
    }
#endif
    return true;
}

//------------------------------------------------------------------------------
void suggestions::clear(uint32 generation_id)
{
    m_line.clear();
    m_items.clear();
    m_generation_id = generation_id;
    m_dirtied = false;
}

//------------------------------------------------------------------------------
void suggestions::set_line(const char* line, int32 length)
{
    m_line.concat(line, length);
}

//------------------------------------------------------------------------------
void suggestions::add(const char* text, uint32 offset, const char* source,
                      int32 highlight_offset, int32 highlight_length,
                      const char* tooltip, int32 history_index)
{
    suggestion suggestion;
    suggestion.m_suggestion = text;
    suggestion.m_suggestion_offset = offset;
    suggestion.m_highlight_offset = highlight_offset;
    suggestion.m_highlight_length = highlight_length;
    suggestion.m_tooltip = tooltip;
    suggestion.m_source = source;
    suggestion.m_history_index = history_index;
    m_items.emplace_back(std::move(suggestion));
}

//------------------------------------------------------------------------------
void suggestions::remove(uint32 index)
{
    m_items.erase(m_items.begin() + index);
    m_dirtied = true;
}

//------------------------------------------------------------------------------
void suggestions::remove_if_history_index(uint32 history_index)
{
    assert(history_index >= 0);
    for (auto it = m_items.begin(); it != m_items.end();)
    {
        if (it->m_history_index == history_index)
        {
            it = m_items.erase(it);
        }
        else
        {
            if (it->m_history_index > history_index)
                --it->m_history_index;
            ++it;
        }
    }
    m_dirtied = true;
}



//------------------------------------------------------------------------------
uint32 suggestion_manager::s_generation_id = 0;

//------------------------------------------------------------------------------
bool suggestion_manager::more() const
{
    assertimplies(m_iter.more(), m_suggestions.size() == 1);
    return m_iter.more();
}

//------------------------------------------------------------------------------
bool suggestion_manager::get_visible(str_base& out, bool* includes_hint) const
{
    assert(g_autosuggest_enable.get());

    if (includes_hint)
        *includes_hint = false;

    out.clear();
    if (!g_rl_buffer)
        return false;
    if (is_suggestion_list_active(true/*even_if_hidden*/) || m_suggestions.empty())
        return false;

    const suggestion& first_suggestion = m_suggestions[0];

    if (first_suggestion.m_suggestion_offset > g_rl_buffer->get_length())
        return false;

    // Do not allow relaxed comparison for suggestions, as it is too confusing,
    // as a result of the logic to respect original case.
    int32 scope = g_ignore_case.get() ? str_compare_scope::caseless : str_compare_scope::exact;
    str_compare_scope compare(scope, g_fuzzy_accent.get());

    str_iter orig(g_rl_buffer->get_buffer() + first_suggestion.m_suggestion_offset);
    str_iter sugg(first_suggestion.m_suggestion.c_str(), first_suggestion.m_suggestion.length());
    str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(orig, sugg);
    if (orig.more())
        return false;

    out.concat(g_rl_buffer->get_buffer(), uint32(orig.get_pointer() - g_rl_buffer->get_buffer()));
    out.concat(sugg.get_pointer(), sugg.length());

#ifdef USE_SUGGESTION_HINT_INLINE
    if (can_show_suggestion_hint())
    {
        const char* hint_text = get_suggestion_hint_text();
#ifdef RIGHT_ALIGN_SUGGESTION_HINT
        COORD size = measure_readline_display(rl_prompt, out.c_str(), out.length());
        static const uint32 hint_cols = cell_count(hint_text) + 1;
        if (size.X + hint_cols >= _rl_screenwidth)
        {
            concat_spaces(out, _rl_screenwidth - size.X);
            size.X = 0;
        }
        concat_spaces(out, _rl_screenwidth - (size.X + hint_cols));
#endif // RIGHT_ALIGN_SUGGESTION_HINT
        out.concat(hint_text);
        if (includes_hint)
            *includes_hint = true;
    }
#endif  // USE_SUGGESTION_HINT_INLINE

    return true;
}

//------------------------------------------------------------------------------
bool suggestion_manager::has_suggestion() const
{
    str<> tmp;
    return get_visible(tmp);
}

//------------------------------------------------------------------------------
void suggestion_manager::clear()
{
    if (m_iter.more() && g_rl_buffer)
        g_rl_buffer->set_need_draw();

    new (&m_iter) str_iter();
    m_started.free();
    m_suggestions.clear();
    m_locked.clear();
    m_endword_offset = -1;
    m_suppress = false;

    new_generation();
}

//------------------------------------------------------------------------------
bool suggestion_manager::can_suggest(const line_state& line)
{
    assert(g_autosuggest_enable.get());

    if (!g_rl_buffer)
        return false;

    if (m_paused)
        return false;

    if (RL_ISSTATE(RL_STATE_NSEARCH|RL_STATE_READSTR))
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

    const auto& m_line = m_suggestions.get_line();
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
bool suggestion_manager::is_locked_against_suggestions()
{
    assert(g_rl_buffer);
    return g_rl_buffer && g_rl_buffer->get_fingerprint(false) == m_locked;
}

//------------------------------------------------------------------------------
void suggestion_manager::lock_against_suggestions(bool lock)
{
    assert(g_rl_buffer);
    if (g_rl_buffer)
    {
        if (lock)
            m_locked = g_rl_buffer->get_fingerprint(false);
        else
            m_locked.clear();
    }
}

//------------------------------------------------------------------------------
void suggestion_manager::suppress_suggestions()
{
    clear();
    lock_against_suggestions(true);
    m_suggestions.set_line(g_rl_buffer->get_buffer(), g_rl_buffer->get_length());
    m_started.concat(g_rl_buffer->get_buffer(), g_rl_buffer->get_length());
    m_suppress = true;
}

//------------------------------------------------------------------------------
void suggestion_manager::set_started(const char* line)
{
    assert(g_autosuggest_enable.get());

    m_started = line;

#ifdef DEBUG
    if (dbg_get_env_int("CLINK_DEBUG_SUGGEST"))
        printf("\x1b[s\x1b[2Hstarted:  \"%s\"\x1b[K\x1b[u", m_started.c_str());
#endif
}

//------------------------------------------------------------------------------
void suggestion_manager::set(const char* line, uint32 endword_offset, suggestions* suggestions)
{
#ifdef DEBUG
    if (suggestions && !suggestions->empty())
    {
        assertimplies(suggestions->size() > 1, g_autosuggest_enable.get());
        assertimplies(suggestions->size() == 1 && !suggestions->get(0).m_suggestion.equals(line), g_autosuggest_enable.get());
    }
#endif

#ifdef DEBUG
    if (dbg_get_env_int("CLINK_DEBUG_SUGGEST"))
    {
        static int32 s_suggnum = 0;
        if (!suggestions || suggestions->empty())
            printf("\x1b[s\x1b[H#%u:  set suggestions:  none, endword ofs %d\x1b[K\x1b[u",
                   ++s_suggnum, endword_offset);
        else
            printf("\x1b[s\x1b[H#%u:  set suggestions:  \"%s\"%s, offset %d, endword ofs %d\x1b[K\x1b[u",
                   ++s_suggnum, suggestions->get(0).m_suggestion.c_str(),
                   suggestions->size() > 1 ? " ..." : "",
                   suggestions->get(0).m_suggestion_offset, endword_offset);
    }
#endif

    // Are the new suggestions different?
    if (m_suggestions.size() == (suggestions ? suggestions->size() : 0) &&
        m_endword_offset == endword_offset &&
        m_suggestions.get_line().equals(line))
    {
        bool same = true;
        if (suggestions)
        {
            for (size_t ii = 0; same && ii < suggestions->size(); ++ii)
            {
                same = (m_suggestions[ii].m_suggestion.equals(suggestions->get(ii).m_suggestion.c_str()) &&
                        m_suggestions[ii].m_suggestion_offset == suggestions->get(ii).m_suggestion_offset &&
                        m_suggestions[ii].m_source.equals(suggestions->get(ii).m_source.c_str()));
            }
        }
        if (same)
            return;
    }

    // Validate offsets.
    if (suggestions && !suggestions->empty())
    {
        const uint32 line_len = uint32(strlen(line));
        for (size_t ii = suggestions->size(); ii--;)
        {
            if (line_len < suggestions->get(ii).m_suggestion_offset)
            {
                assert(false);
                suggestions->remove(ii);
            }
        }
    }

    // Are the new suggestions empty?
    if (!suggestions || suggestions->empty() || !g_autosuggest_enable.get())
    {
malformed:
        clear();
        m_suggestions.m_generation_id = new_generation();
        m_suggestions.set_line(line);
        m_started = line;
        if (g_rl_buffer)
        {
            if (is_suggestion_list_active(true/*even_if_hidden*/))
                g_rl_buffer->set_need_draw();
            g_rl_buffer->draw();
        }
        return;
    }

    suggestions->m_generation_id = new_generation();
    m_suggestions = std::move(*suggestions);
    if (is_suggestion_list_enabled())
    {
        new (&m_iter) str_iter(nullptr, 0);
    }
    else
    {
        new (&m_iter) str_iter(m_suggestions[0].m_suggestion);

        // Do not allow relaxed comparison for suggestions, as it is too
        // confusing, as a result of the logic to respect original case.
        {
            int32 scope = g_ignore_case.get() ? str_compare_scope::caseless : str_compare_scope::exact;
            str_compare_scope compare(scope, g_fuzzy_accent.get());

            str_iter orig(line + m_suggestions[0].m_suggestion_offset);
            const int32 matchlen = str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(orig, m_iter);

            if (orig.more())
                goto malformed;
        }
    }

    m_endword_offset = endword_offset;

    m_suggestions.set_line(line);
    m_started = line;

#ifdef DEBUG
    if (dbg_get_env_int("CLINK_DEBUG_SUGGEST"))
        printf("\x1b[s\x1b[2Hline:     \"%s\"\x1b[K\x1b[u", m_suggestions.get_line().c_str());
#endif

    if (g_rl_buffer)
    {
        g_rl_buffer->set_need_draw();
        g_rl_buffer->draw();
    }
}

//------------------------------------------------------------------------------
bool suggestion_manager::get(suggestions& out)
{
    if (!g_autosuggest_enable.get())
    {
        out.clear();
        return false;
    }

    if (!m_suggestions.is_same(out))
        out = m_suggestions;
    return true;
}

//------------------------------------------------------------------------------
static bool is_suggestion_word_break(int32 c)
{
    return c == ' ' || c == '\t';
}

//------------------------------------------------------------------------------
void suggestion_manager::resync_suggestion_iterator(uint32 old_cursor)
{
    assert(g_autosuggest_enable.get());
    assert(m_suggestions.size() == 1);

    const int32 consume = g_rl_buffer->get_cursor() - old_cursor;
    assert(consume >= 0);

    const char* const start = m_iter.get_pointer();
    while (int32(m_iter.get_pointer() - start) < consume)
        m_iter.next();
}

//------------------------------------------------------------------------------
uint32 suggestion_manager::new_generation()
{
    ++s_generation_id;
    if (!s_generation_id)
        ++s_generation_id;
    return s_generation_id;
}

//------------------------------------------------------------------------------
bool suggestion_manager::insert(suggestion_action action)
{
    if (!g_autosuggest_enable.get())
        return false;

    if (is_suggestion_list_active(true/*even_if_hidden*/) || m_suggestions.size() != 1)
        return false;

    if (!m_iter.more() || g_rl_buffer->get_cursor() != g_rl_buffer->get_length())
        return false;

    // Do not allow relaxed comparison for suggestions, as it is too confusing,
    // as a result of the logic to respect original case.
    int32 scope = g_ignore_case.get() ? str_compare_scope::caseless : str_compare_scope::exact;
    str_compare_scope compare(scope, g_fuzzy_accent.get());

    const bool original_case = g_original_case.get();
    const suggestion& first_suggestion = m_suggestions[0];

    // Special case when inserting to the end and the suggestion covers the
    // entire line.  Replace the entire line to use the original capitalization.
    // If the suggestion doesn't match up through the end of the line, then it's
    // malformed and must be discarded.
    if (original_case && action == suggestion_action::insert_to_end && first_suggestion.m_suggestion_offset == 0)
    {
        str_iter orig_iter(g_rl_buffer->get_buffer(), g_rl_buffer->get_length());
        str_iter sugg_iter(first_suggestion.m_suggestion.c_str(), first_suggestion.m_suggestion.length());
        str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(orig_iter, sugg_iter);
        if (!orig_iter.more() && sugg_iter.more())
        {
            g_rl_buffer->begin_undo_group();
            g_rl_buffer->remove(0, g_rl_buffer->get_length());
            g_rl_buffer->insert(first_suggestion.m_suggestion.c_str());
            g_rl_buffer->end_undo_group();
        }
        suppress_suggestions();
        return !orig_iter.more();
    }

    // Reset suggestion iterator.
    new (&m_iter) str_iter(first_suggestion.m_suggestion.c_str(), first_suggestion.m_suggestion.length());

    // Consume the suggestion iterator up to the end word offset.
    if (first_suggestion.m_suggestion_offset < m_endword_offset)
    {
        str_iter orig_iter(g_rl_buffer->get_buffer() + first_suggestion.m_suggestion_offset, m_endword_offset - first_suggestion.m_suggestion_offset);
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
    uint32 replace_offset = max(m_endword_offset, first_suggestion.m_suggestion_offset);
    const char* insert = m_iter.get_pointer();

    // Track quotes between end word offset and cursor (end of line).
    bool quote = false;
    {
        const uint32 len = g_rl_buffer->get_length();
        if (m_endword_offset > 0)
            quote = (g_rl_buffer->get_buffer()[m_endword_offset - 1] == '"');
        if (replace_offset < len)
        {
            str_iter orig_iter(g_rl_buffer->get_buffer() + replace_offset, g_rl_buffer->get_length() - replace_offset);
            const char* const inflection = g_rl_buffer->get_buffer() + first_suggestion.m_suggestion_offset;
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
        end_offset = first_suggestion.m_suggestion_offset + int32(m_iter.get_pointer() - first_suggestion.m_suggestion.c_str());
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
        m_suggestions.set_line(g_rl_buffer->get_buffer(), g_rl_buffer->get_length());
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
