// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_buffer.h"
#include "line_state.h"
#include "word_collector.h"
#include "popup.h"
#include "editor_module.h"
#include "rl_commands.h"
#include "doskey.h"
#include "textlist_impl.h"
#include "history_db.h"
#include "ellipsify.h"
#include "host_callbacks.h"
#include "display_readline.h"
#include "recognizer.h"
#include "wakeup_chars.h"
#include "clink_rl_signal.h"
#include "rl_integration.h"
#include "line_editor_integration.h"
#include "suggestions.h"
#include "line_queue.h"

#include <core/base.h>
#include <core/log.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/debugheap.h>
#include <terminal/wcwidth.h>
#include <terminal/scroll.h>
#include <terminal/screen_buffer.h>
#include <terminal/terminal.h>
#include <terminal/terminal_out.h>
#include <terminal/terminal_helpers.h>
#include <terminal/ecma48_iter.h>

extern "C" {
#include <readline/history.h>
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
extern int32 find_streqn (const char *a, const char *b, int32 n);
extern void rl_replace_from_history(HIST_ENTRY *entry, int flags);
}

#include <tib.h>

#ifdef DEBUG
#include <core/assert_improved.h>
#endif
#include <core/callstack.h>
#include <core/linear_allocator.h>

#include <list>
#include <unordered_set>
#include <unordered_map>
#include <signal.h>

#include "../../../clink/app/src/version.h" // Ugh.

#define CSI(x) "\x1b[" #x

extern "C" const int32 c_clink_version = CLINK_VERSION_ENCODED;

extern bool is_test_harness();



//------------------------------------------------------------------------------
// Internal ConHost system menu command IDs.
#define ID_CONSOLE_COPY         0xFFF0
#define ID_CONSOLE_PASTE        0xFFF1
#define ID_CONSOLE_MARK         0xFFF2
#define ID_CONSOLE_SCROLL       0xFFF3
#define ID_CONSOLE_FIND         0xFFF4
#define ID_CONSOLE_SELECTALL    0xFFF5
#define ID_CONSOLE_EDIT         0xFFF6
#define ID_CONSOLE_CONTROL      0xFFF7
#define ID_CONSOLE_DEFAULTS     0xFFF8



//------------------------------------------------------------------------------
enum { paste_crlf_delete, paste_crlf_space, paste_crlf_ampersand, paste_crlf_crlf };
static setting_enum g_paste_crlf(
    "clink.paste_crlf",
    "Strips CR and LF chars on paste",
    "Setting this to 'space' makes Clink strip CR and LF characters from text\n"
    "pasted into the current line.  Set this to 'delete' to strip all newline\n"
    "characters to replace them with a space.  Set this to 'ampersand' to replace\n"
    "all newline characters with an ampersand.  Or set this to 'crlf' to paste all\n"
    "newline characters as-is (executing commands that end with newline).",
    "delete,space,ampersand,crlf",
    paste_crlf_crlf);

extern setting_bool g_adjust_cursor_style;
extern setting_bool g_match_wild;



//------------------------------------------------------------------------------
extern line_buffer* g_rl_buffer;
extern word_collector* g_word_collector;
extern editor_module::result* g_result;

//------------------------------------------------------------------------------
bool expand_history(const char* in, str_base& out)
{
    return history_db::expand(in, out) >= history_db::expand_result::expand_ok;
}

//------------------------------------------------------------------------------
static void strip_crlf(char* line, std::list<str_moveable>& overflow, int32 setting, bool* _done)
{
    bool has_overflow = false;
    int32 prev_was_crlf = 0;
    char* write = line;
    const char* read = line;
    bool done = false;
    while (*read)
    {
        char c = *read;
        if (c != '\n' && c != '\r')
        {
            prev_was_crlf = 0;
            *write = c;
            ++write;
        }
        else if (!prev_was_crlf)
        {
            switch (setting)
            {
            default:
                assert(false);
                // fall through
            case paste_crlf_delete:
                break;
            case paste_crlf_space:
                prev_was_crlf = 1;
                *write = ' ';
                ++write;
                break;
            case paste_crlf_ampersand:
                prev_was_crlf = 1;
                *write = '&';
                ++write;
                break;
            case paste_crlf_crlf:
                has_overflow = true;
                if (c == '\n')
                {
                    *write = '\n';
                    ++write;
                }
                break;
            }
        }

        ++read;
    }

    *write = '\0';

    if (has_overflow)
    {
        bool first = true;
        char* start = line;
        while (*start)
        {
            char* end = start;
            while (*end)
            {
                char c = *end;
                ++end;
                if (c == '\n')
                {
                    done = true;
                    if (first)
                        *(end - 1) = '\0';
                    break;
                }
            }

            if (first)
            {
                first = false;
            }
            else
            {
                uint32 len = (uint32)(end - start);
                overflow.emplace_back();
                str_moveable& back = overflow.back();
                back.reserve(len);
                back.concat(start, len);
            }

            start = end;
        }
    }

    if (_done)
        *_done = done;
}

//------------------------------------------------------------------------------
static void get_word_bounds(const line_buffer& buffer, int32* left, int32* right)
{
    const char* str = buffer.get_buffer();
    uint32 cursor = buffer.get_cursor();

    // Determine the word delimiter depending on whether the word's quoted.
    int32 delim = 0;
    for (uint32 i = 0; i < cursor; ++i)
    {
        char c = str[i];
        delim += (c == '\"');
    }

    // Search outwards from the cursor for the delimiter.
    delim = (delim & 1) ? '\"' : ' ';
    *left = 0;
    for (int32 i = cursor - 1; i >= 0; --i)
    {
        char c = str[i];
        if (c == delim)
        {
            *left = i + 1;
            break;
        }
    }

    const char* post = strchr(str + cursor, delim);
    if (post != nullptr)
        *right = int32(post - str);
    else
        *right = int32(strlen(str));
}

//------------------------------------------------------------------------------
bool toggle_slashes_in_rl_buffer(int32 offset, int32 length)
{
    str<1024> word;
    word.concat(g_rl_buffer->get_buffer() + offset, length);

    int32 sep = 0;
    for (uint32 i = 0; i < word.length(); ++i)
    {
        if (path::is_separator(word[i]))
        {
            const int32 was = sep;
            sep = word[i];
            // If all separators are the same, then toggle to the other kind.
            // If mixed separators exist, normalize all to the first kind.
            if (was && was != sep)
                break;
        }
    }

    switch (sep)
    {
    case '/':   sep = '\\'; break;
    case '\\':  sep = '/'; break;
    default:    return false;
    }

    path::normalise_separators(word, sep);

    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(offset, offset + length);
    g_rl_buffer->set_cursor(offset);
    g_rl_buffer->insert(word.c_str());
    g_rl_buffer->end_undo_group();
    return true;
}

//------------------------------------------------------------------------------
static void enqueue_lines(std::list<str_moveable>& lines)
{
    auto* const queue = line_queue::get();
    assert(queue);
    if (queue)
    {
        for (const auto& line : lines)
            queue->enqueue_back(line.c_str());
    }
}



//------------------------------------------------------------------------------
int32 host_add_history(int32, const char* line, const char** out_timestamp)
{
    // NOTE:  This intentionally does not send the "onhistory" Lua event.
    // Since this command explicitly manipulates the history it's reasonable
    // for it to override scripts.

    time_t timestamp;
    history_database* h = history_database::get();
    if (!h || !h->add(line, &timestamp))
    {
        if (out_timestamp)
            *out_timestamp = nullptr;
        return false;
    }

    if (out_timestamp)
    {
        static char s_timestamp_buffer[32];
        sprintf_s(s_timestamp_buffer, "%u", uint32(timestamp));
        *out_timestamp = s_timestamp_buffer;
    }
    return true;
}

//------------------------------------------------------------------------------
int32 host_remove_history(int32 rl_history_index, const char* line)
{
    history_database* h = history_database::get();
    if (!h || !h->remove(rl_history_index, line))
        return false;

    extern bool remove_suggestion_list_history_index(int32 rl_history_index);
    remove_suggestion_list_history_index(rl_history_index);
    return true;
}



//------------------------------------------------------------------------------
#ifdef UNDO_LIST_HEAP_DIAGNOSTICS
class undo_entry_heap
{
    struct tracker
    {
        bool m_freed = false;
        bool m_seen = false;
        uint32 m_num = 0;
        DWORD m_alloc_frames_hash = 0;
        uint32 m_alloc_frames_count = 0;
        void* m_alloc_frames[20];
        DWORD m_free_frames_hash = 0;
        uint32 m_free_frames_count = 0;
        void* m_free_frames[20];
    };

    struct wrapped_UNDO_LIST
    {
        tracker*    m_tracker;      // Make it easy to find in the debugger:  ((tracker**)rl_undo_list)[-1]
        UNDO_LIST   m_undo_list;
    };

public:
    undo_entry_heap() : m_heap(32768)
    {
    }

    UNDO_LIST* alloc_undo_entry()
    {
        dbg_ignore_scope(snapshot, "undo_entry_heap");

        tracker* t = new tracker;
        wrapped_UNDO_LIST* p = (wrapped_UNDO_LIST*)m_heap.alloc(sizeof(tracker*) + sizeof(*p));
        if (!p)
        {
            delete t;
            return nullptr;
        }
        t->m_num = s_num++;
        t->m_alloc_frames_count = get_callstack_frames(1, sizeof_array(t->m_alloc_frames), t->m_alloc_frames, &t->m_alloc_frames_hash);
        assert(!t->m_freed);
        p->m_tracker = t;
        m_allocated.emplace(&p->m_undo_list, t);
        return &p->m_undo_list;
    }

    void free_undo_entry(UNDO_LIST* p)
    {
        auto it = m_allocated.find(p);
        assert(it != m_allocated.end());
        tracker* t = it->second;
        assert(t);
        if (t->m_freed)
        {
            str<> s;
            char sa[4096];
            char sf[4096];
            char sd[4096];
            format_frames(t->m_alloc_frames_count, t->m_alloc_frames, t->m_alloc_frames_hash, sa, sizeof(sa), true);
            format_frames(t->m_free_frames_count, t->m_free_frames, t->m_free_frames_hash, sf, sizeof(sf), true);
            DWORD df_hash;
            void* df_frames[20];
            uint32 df_count = get_callstack_frames(1, sizeof_array(df_frames), df_frames, &df_hash);
            format_frames(df_count, df_frames, df_hash, sd, sizeof(sd), true);
            s.format("ALREADY FREED %p (#%u)\r\n\r\nalloc stacktrace:  %s\r\noriginal free stacktrace:  %s\r\nthis double-free stacktrace:  %s", p, t->m_num, sa, sf, sd);
            assert(!t->m_freed);
            dbgtracef("%s", s.c_str());
        }
        // NOTE: This diagnostic heap doesn't actually free any blocks; that
        // enables it to accurately, uniquely, and independently track the
        // history of each individual allocation.
        t->m_freed = true;
        t->m_free_frames_count = get_callstack_frames(1, sizeof_array(t->m_free_frames), t->m_free_frames, &t->m_free_frames_hash);
    }

    void check_undo_entry_leaks()
    {
        uint32 leaks = 0;
        uint32 new_leaks = 0;
        for (const auto tracker : m_allocated)
        {
            if (!tracker.second->m_freed)
            {
                ++leaks;
                if (!tracker.second->m_seen)
                    ++new_leaks;
            }
        }

        if (leaks)
        {
            if (new_leaks)
            {
                assert(!leaks);

                dbgtracef("----- UNDO_LIST leaks: new %u (total %u) -----", new_leaks, leaks);

#ifdef INCLUDE_CALLSTACKS
                char stack[4096];
#endif
                for (const auto alloc : m_allocated)
                {
                    if (!alloc.second->m_seen)
                    {
                        alloc.second->m_seen = true;
#ifdef INCLUDE_CALLSTACKS
                        if (!alloc.second->m_freed)
                        {
                            stack[0] = '\0';
                            format_frames(alloc.second->m_alloc_frames_count, alloc.second->m_alloc_frames, alloc.second->m_alloc_frames_hash, stack, sizeof(stack), false);
                            dbgtracef("Leak:  0x%p (#%u),  text \"%s\",  context: %s", alloc.first, alloc.second->m_num, alloc.first->text ? alloc.first->text : "(nullptr)", stack);
                        }
#endif
                    }
                }
#ifdef INCLUDE_CALLSTACKS
                dbgtracef("----- end of UNDO_LIST leaks -----");
#endif
            }
            else
            {
                dbgtracef("----- UNDO_LIST leaks: total %u; can't reset undo_entry_heap -----", leaks);
            }
        }
        else
        {
            dbg_ignore_scope(snapshot, "undo_entry_heap");

            m_allocated.clear();
            m_heap.clear();
        }
    }

private:
    linear_allocator m_heap;
    std::unordered_map<const UNDO_LIST*, tracker*> m_allocated;

    static uint32 s_num;
};

static undo_entry_heap s_undo_entry_heap;
uint32 undo_entry_heap::s_num = 0;

extern "C" UNDO_LIST* clink_alloc_undo_entry(void)
{
    return s_undo_entry_heap.alloc_undo_entry();
}

extern "C" void clink_free_undo_entry(UNDO_LIST* p)
{
    s_undo_entry_heap.free_undo_entry(p);
}

extern "C" void clink_check_undo_entry_leaks(void)
{
    s_undo_entry_heap.check_undo_entry_leaks();
}
#endif // UNDO_LIST_HEAP_DIAGNOSTICS



//------------------------------------------------------------------------------
class history_infos
{
public:
                    history_infos() = default;
                    ~history_infos();
    bool            make(const char* prefix=nullptr, int32 search_len=0, int32 orig_pos=-1);
    popup_results   activate_history_text_list(bool win_history);
private:
    void            discard();
private:
    char**          m_history = nullptr;
    entry_info*     m_infos = nullptr;
    int32           m_total = 0;
    int32           m_current = -1;
    HIST_ENTRY*     m_saved_line = nullptr;
    int32           m_saved_point = -1;
    bool            m_in_history_entry = false;
    bool            m_restore = true;
};

//------------------------------------------------------------------------------
history_infos::~history_infos()
{
    free(m_history);
    free(m_infos);
    if (m_restore)
    {
        if (m_saved_line)
        {
            assert(!rl_undo_list);
            _rl_unsave_line(m_saved_line);
            m_saved_line = nullptr;
            if (m_saved_point >= 0)
            {
                assert(m_saved_point <= g_tib->get_length());
                g_tib->set_caret(m_saved_point);
            }
        }
        else
        {
            HIST_ENTRY* entry = current_history();
            if (entry)
                rl_replace_from_history(entry, 0);
        }
    }
}

//------------------------------------------------------------------------------
bool history_infos::make(const char* prefix, int32 search_len, int32 orig_pos)
{
    assert(!m_saved_line);
    assert(m_saved_point < 0);

    HIST_ENTRY** list = history_list();
    if (!list || !history_length)
        return false;

    const bool had_undo_list = !!rl_undo_list;
    if (current_history())
    {
        m_in_history_entry = true;
        rl_maybe_replace_line();
    }
    else
    {
        m_in_history_entry = false;
        m_saved_line = _rl_alloc_saved_line();
        m_saved_point = rl_point;
    }
    rl_undo_list = 0;

    m_history = (char**)malloc(sizeof(*m_history) * history_length);
    m_infos = (entry_info*)malloc(sizeof(*m_infos) * history_length);
    m_current = -1;

    // Copy the history list (just a shallow copy of the line pointers).
    m_total = 0;
    for (int32 i = 0; i < history_length; i++)
    {
        if (prefix && search_len > 0 && !find_streqn(prefix, list[i]->line, search_len))
            continue;
        m_history[m_total] = list[i]->line;
        m_infos[m_total].index = i;
        m_infos[m_total].marked = (list[i]->data != nullptr);
        if (i == orig_pos)
        {
            m_infos[m_total].marked = had_undo_list;
            m_current = m_total;
        }
        ++m_total;
    }

    if (m_current < 0 && orig_pos < 0)
        m_current = where_history();
    if (m_current < 0 || m_current > m_total - 1)
        m_current = m_total - 1;

    return m_total > 0;
}

//------------------------------------------------------------------------------
popup_results history_infos::activate_history_text_list(bool win_history)
{
    if (m_total <= 0)
        return popup_results();

    popup_results results = ::activate_history_text_list(const_cast<const char**>(m_history), m_total, m_current, m_infos, win_history);

    assert(!rl_undo_list);
    switch (results.m_result)
    {
    case popup_result::cancel:
        if (results.m_reset_history_index)
        {
            if (m_in_history_entry)
            {
                discard();
                rl_replace_line("", 1);
                using_history();
            }
            results.m_reset_history_index = false;
        }
        break;
    case popup_result::error:
        break;
    case popup_result::select:
    case popup_result::use:
        discard();
        results.m_index = m_infos[results.m_index].index;
        break;
    }

    return results;
}

//------------------------------------------------------------------------------
void history_infos::discard()
{
    if (m_saved_line)
        _rl_free_undo_list(static_cast<UNDO_LIST*>(m_saved_line->data));
    _rl_free_history_entry(m_saved_line);
    m_saved_line = nullptr;
    m_saved_point = -1;
    m_restore = false;
}



//------------------------------------------------------------------------------
int32 clink_newline(int32 count, int32 invoking_key)
{
    clink_accept_line(*g_tib, invoking_key, nullptr, nullptr);
    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_accept_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    ctx.set_mark_active(false);

#ifdef TIB_TODO
    if (_rl_history_preserve_point)
        _rl_history_saved_point = (rl_point == rl_end) ? -1 : rl_point;
#endif

    ctx.set_done();

#ifdef TIB_TODO
#if defined (VI_MODE)
    if (rl_editing_mode == vi_mode)
    {
        _rl_vi_done_inserting();
        if (_rl_vi_textmod_command(_rl_vi_last_command) == 0)
            _rl_vi_reset_last ();
    }
#endif /* VI_MODE */
#endif

    end_prompt(-1/*crlf*/);
    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_reload(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    assert(g_result);
    return force_reload_scripts();
}

//------------------------------------------------------------------------------
int32_t clink_reset_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    g_rl_buffer->reset();
    clear_suggestion();

    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_exit(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    clink_reset_line(ctx, 0, nullptr, nullptr);
    ctx.insert_text("exit 0");
    clink_accept_line(ctx, 0, nullptr, nullptr);

    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_ctrl_c(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    if (ctx.has_selection())
    {
        tib::copy(ctx, key, name, params);
        ctx.clear_selection();
        return 0;
    }

    clink_sighandler(SIGINT);

    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_paste(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    str<1024> utf8;
    if (!os::get_clipboard_text(utf8))
        return 0;

    dbg_ignore_scope(snapshot, "clink_paste");

    bool done = false;
    std::list<str_moveable> overflow;
    strip_crlf(utf8.data(), overflow, g_paste_crlf.get(), &done);
    strip_wakeup_chars(utf8);

    ctx.begin_undo_group();
    ctx.set_mark(ctx.get_sel_begin());
    ctx.insert_text(utf8.c_str());
    ctx.end_undo_group();

    enqueue_lines(overflow);
    if (done)
    {
        display_readline();
        clink_accept_line(ctx, '\r', nullptr, nullptr);
    }

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_copy_line(int32 count, int32 invoking_key)
{
    os::set_clipboard_text(g_rl_buffer->get_buffer(), g_rl_buffer->get_length());

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_copy_word(int32 count, int32 invoking_key)
{
    if (count < 0 || !g_rl_buffer)
    {
Nope:
        tib::ding();
        return 0;
    }

    words words;
    collect_words(*g_rl_buffer, words, collect_words_mode::whole_command);
    if (words.empty())
        goto Nope;

    if (!rl_explicit_arg)
    {
        uint32 line_cursor = g_rl_buffer->get_cursor();
        for (auto const& word : words)
        {
            if (line_cursor >= word.offset &&
                line_cursor <= word.offset + word.length)
            {
                os::set_clipboard_text(g_rl_buffer->get_buffer() + word.offset, word.length);
                return 0;
            }
        }
    }
    else
    {
        count = rl_numeric_arg;
        for (auto const& word : words)
        {
            if (count-- == 0)
            {
                os::set_clipboard_text(g_rl_buffer->get_buffer() + word.offset, word.length);
                return 0;
            }
        }
    }

    goto Nope;
}

//------------------------------------------------------------------------------
int32 clink_copy_cwd(int32 count, int32 invoking_key)
{
    str<> cwd;
    if (os::get_current_dir(cwd))
    {
        cwd << PATH_SEP;
        path::normalise(cwd);
    }
    os::set_clipboard_text(cwd.c_str(), cwd.length());
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_expand_env_var(int32 count, int32 invoking_key)
{
    // Extract the word under the cursor.
    int32 word_left, word_right;
    get_word_bounds(*g_rl_buffer, &word_left, &word_right);

    str<1024> in;
    in.concat(g_rl_buffer->get_buffer() + word_left, word_right - word_left);

    str<> out;
    os::expand_env(in.c_str(), in.length(), out);

    // Update Readline with the resulting expansion.
    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(word_left, word_right);
    g_rl_buffer->set_cursor(word_left);
    g_rl_buffer->insert(out.c_str());
    g_rl_buffer->end_undo_group();

    return 0;
}

//------------------------------------------------------------------------------
enum { el_alias = 1, el_envvar = 2, el_history = 4 };
static int32 do_expand_line(int32 flags)
{
    bool expanded = false;
    str<> in;
    str<> out;
    int32 point = rl_point;

    in = g_rl_buffer->get_buffer();

    if (flags & el_history)
    {
        if (expand_history(in.c_str(), out))
        {
            in = out.c_str();
            point = -1;
            expanded = true;
        }
    }

    if (flags & el_alias)
    {
        doskey_alias alias;
        doskey doskey("cmd.exe");
        doskey.resolve(in.c_str(), alias, point < 0 ? nullptr : &point);
        if (alias)
        {
            alias.next(out);
            in = out.c_str();
            expanded = true;
        }
    }

    if (flags & el_envvar)
    {
        if (os::expand_env(in.c_str(), in.length(), out, point < 0 ? nullptr : &point))
        {
            in = out.c_str();
            expanded = true;
        }
    }

    if (!expanded)
    {
        tib::ding();
        return 0;
    }

    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(0, ~0);
    if (!out.empty())
        g_rl_buffer->insert(out.c_str());
    g_rl_buffer->set_cursor(point);
    g_rl_buffer->end_undo_group();

    return 0;
}

//------------------------------------------------------------------------------
// Expands a doskey alias (but only the first line, if $T is present).
int32 clink_expand_doskey_alias(int32 count, int32 invoking_key)
{
    return do_expand_line(el_alias);
}

//------------------------------------------------------------------------------
// Performs history expansion.
int32 clink_expand_history(int32 count, int32 invoking_key)
{
    return do_expand_line(el_history);
}

//------------------------------------------------------------------------------
// Performs history and doskey alias expansion.
int32 clink_expand_history_and_alias(int32 count, int32 invoking_key)
{
    return do_expand_line(el_history|el_alias);
}

//------------------------------------------------------------------------------
// Performs history, doskey alias, and environment variable expansion.
int32 clink_expand_line(int32 count, int32 invoking_key)
{
    return do_expand_line(el_history|el_alias|el_envvar);
}

//------------------------------------------------------------------------------
int32 clink_up_directory(int32 count, int32 invoking_key)
{
    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(0, ~0u);
    g_rl_buffer->insert(" cd ..");
    g_rl_buffer->end_undo_group();
    clink_newline(1, invoking_key);

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_insert_dot_dot(int32 count, int32 invoking_key)
{
    str<> str;

    if (uint32 cursor = g_rl_buffer->get_cursor())
    {
        char last_char = g_rl_buffer->get_buffer()[cursor - 1];
        if (last_char != ' ' && !path::is_separator(last_char))
            str << PATH_SEP;
    }

    str << ".." << PATH_SEP;

    g_rl_buffer->insert(str.c_str());

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_shift_space(int32 count, int32 invoking_key)
{
    return _rl_dispatch(' ', _rl_keymap);
}

//------------------------------------------------------------------------------
int32 clink_magic_suggest_space(int32 count, int32 invoking_key)
{
    insert_suggestion(suggestion_action::insert_next_full_word);
    g_rl_buffer->insert(" ");
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_toggle_slashes(int32 count, int32 invoking_key)
{
    if (count < 0 || !g_rl_buffer)
    {
Nope:
        tib::ding();
        return 0;
    }

    words words;
    collect_words(*g_rl_buffer, words, collect_words_mode::whole_command);
    if (words.empty())
        goto Nope;

    if (!rl_explicit_arg)
    {
        uint32 line_cursor = g_rl_buffer->get_cursor();
        for (auto const& word : words)
        {
            if (line_cursor >= word.offset &&
                line_cursor <= word.offset + word.length)
            {
                if (!toggle_slashes_in_rl_buffer(word.offset, word.length))
                    break;
                return 0;
            }
        }
    }
    else
    {
        count = rl_numeric_arg;
        for (auto const& word : words)
        {
            if (count-- == 0)
            {
                if (!toggle_slashes_in_rl_buffer(word.offset, word.length))
                    break;
                return 0;
            }
        }
    }

    goto Nope;
}



//------------------------------------------------------------------------------
int32 clink_scroll_line_up(int32 count, int32 invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), -1, SCR_BYLINE);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_scroll_line_down(int32 count, int32 invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), 1, SCR_BYLINE);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_scroll_page_up(int32 count, int32 invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), -1, SCR_BYPAGE);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_scroll_page_down(int32 count, int32 invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), 1, SCR_BYPAGE);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_scroll_top(int32 count, int32 invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), -1, SCR_TOEND);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_scroll_bottom(int32 count, int32 invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), 1, SCR_TOEND);
    return 0;
}



//------------------------------------------------------------------------------
int32 clink_find_conhost(int32 count, int32 invoking_key)
{
    HWND hwndConsole = GetConsoleWindow();
    if (!hwndConsole)
    {
        tib::ding();
        return 0;
    }

    // Invoke conhost's Find command via the system menu.
    SendMessage(hwndConsole, WM_SYSCOMMAND, ID_CONSOLE_FIND, 0);

    deduce_scroll_mode(GetStdHandle(STD_OUTPUT_HANDLE));
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_mark_conhost(int32 count, int32 invoking_key)
{
    HWND hwndConsole = GetConsoleWindow();
    if (!hwndConsole)
    {
        tib::ding();
        return 0;
    }

    // Conhost's Mark command is asynchronous and saves/restores the cursor info
    // and position.  So we need to trick the cursor into being visible, so that
    // it gets restored as visible since that's the state Readline will be in
    // after the Mark command finishes.
    show_cursor(true);

    // Invoke conhost's Mark command via the system menu.
    SendMessage(hwndConsole, WM_SYSCOMMAND, ID_CONSOLE_MARK, 0);

    deduce_scroll_mode(GetStdHandle(STD_OUTPUT_HANDLE));
    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_selectall_conhost(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    if (ctx.get_sel_begin() != 0 || ctx.get_sel_end() != ctx.get_length())
        return tib::select_all(ctx, key, name, params);

    HWND hwndConsole = GetConsoleWindow();
    if (!hwndConsole)
    {
        tib::ding();
        return 0;
    }

    if (ctx.get_caret() == 0)
    {
        g_tib->set_selection(0, ~0);
        display_readline();
    }

    // Invoke conhost's Select All command via the system menu.
    SendMessage(hwndConsole, WM_SYSCOMMAND, ID_CONSOLE_SELECTALL, 0);

    deduce_scroll_mode(GetStdHandle(STD_OUTPUT_HANDLE));
    return 0;
}



//------------------------------------------------------------------------------
int32 clink_popup_directories(int32 count, int32 invoking_key)
{
    // Copy the directory list (just a shallow copy of the dir pointers).
    int32 total = 0;
    const char** history = host_copy_dir_history(&total);
    if (!history || !total)
    {
        free(history);
        tib::ding();
        return 0;
    }

    // Popup list.
    const popup_results results = activate_directories_text_list(history, total);

    // Handle results.
    switch (results.m_result)
    {
    case popup_result::cancel:
        break;
    case popup_result::error:
        tib::ding();
        break;
    case popup_result::select:
    case popup_result::use:
        {
            bool end_sep = (results.m_text.c_str()[0] &&
                            path::is_separator(results.m_text.c_str()[results.m_text.length() - 1]));

            char qs[2] = {};
            if (rl_basic_quote_characters &&
                rl_basic_quote_characters[0] &&
                rl_filename_quote_characters &&
                _rl_strpbrk(results.m_text.c_str(), rl_filename_quote_characters) != 0)
            {
                qs[0] = rl_basic_quote_characters[0];
            }

            str<> dir;
            dir.format("%s%s%s", qs, results.m_text.c_str(), qs);

            bool use = (results.m_result == popup_result::use);
            g_tib->begin_undo_group();
            if (use)
            {
                if (!end_sep)
                    dir.concat(PATH_SEP);
                rl_replace_line(dir.c_str(), 0);
                g_tib->set_caret(g_tib->get_length());
            }
            else
            {
                g_tib->insert_text(dir.c_str());
            }
            g_tib->end_undo_group();
            display_readline();
            if (use)
                clink_newline(1, invoking_key);
        }
        break;
    }

    free(history);

    return 0;
}



//------------------------------------------------------------------------------
int32 clink_complete_numbers(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._internal._complete_numbers"))
        tib::ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_menu_complete_numbers(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._internal._menu_complete_numbers"))
        tib::ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_menu_complete_numbers_backward(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._internal._menu_complete_numbers_backward"))
        tib::ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_old_menu_complete_numbers(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._internal._old_menu_complete_numbers"))
        tib::ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_old_menu_complete_numbers_backward(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._internal._old_menu_complete_numbers_backward"))
        tib::ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_popup_complete_numbers(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._internal._popup_complete_numbers"))
        tib::ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_popup_show_help(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._internal._popup_show_help"))
        tib::ding();
    return 0;
}



//------------------------------------------------------------------------------
int32_t clink_select_complete(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    extern bool activate_select_complete(editor_module::result& result, bool reactivate);
    if (!g_result || !activate_select_complete(*g_result, ctx.get_last_command_func() == clink_select_complete))
        tib::ding();
    return 0;
}



//------------------------------------------------------------------------------
int32_t clink_toggle_suggestion_list(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    extern bool toggle_suggestion_list(editor_module::result& result, int8 mode);
    if (!g_result || !toggle_suggestion_list(*g_result, -1/*toggles on/off*/))
        tib::ding();
    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_show_suggestion_list(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    extern bool toggle_suggestion_list(editor_module::result& result, int8 mode);
    if (!g_result || !toggle_suggestion_list(*g_result, true/*turns on*/))
        tib::ding();
    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_cancel_suggestion_list(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    extern bool toggle_suggestion_list(editor_module::result& result, int8 mode);
    if (!g_result || !toggle_suggestion_list(*g_result, false/*turns off*/))
        tib::ding();
    return 0;
}



//------------------------------------------------------------------------------
int32_t cua_forward_char(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    int32_t count = ctx.get_numeric_argument();
    if (count != 0)
    {
        bool sugg = false;
another_word:
        if (insert_suggestion(suggestion_action::insert_next_full_word))
        {
            sugg = true;
            count--;
            if (count > 0)
                goto another_word;
            return 0;
        }
        if (sugg)
            return 0;
    }

    return tib::cua_forward_char(ctx, key, name, params);
}



//------------------------------------------------------------------------------
int32_t clink_forward_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    int32_t count = ctx.get_numeric_argument();
    if (count != 0)
    {
        bool sugg = false;
another_word:
        if (insert_suggestion(suggestion_action::insert_next_word))
        {
            sugg = true;
            count--;
            if (count > 0)
                goto another_word;
        }
        if (sugg)
            return 0;
    }

    return tib::forward_word(ctx, key, name, params);
}

//------------------------------------------------------------------------------
int32_t clink_forward_bigword(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    int32_t count = ctx.get_numeric_argument();
    if (count != 0)
    {
        bool sugg = false;
another_word:
        if (insert_suggestion(suggestion_action::insert_next_full_word))
        {
            sugg = true;
            count--;
            if (count > 0)
                goto another_word;
        }
        if (sugg)
            return 0;
    }

    return tib::forward_bigword(ctx, key, name, params);
}

//------------------------------------------------------------------------------
int32_t clink_forward_char(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    if (insert_suggestion(suggestion_action::insert_to_end))
        return 0;

    return tib::forward_char(ctx, key, name, params);
}

//------------------------------------------------------------------------------
int32 clink_forward_byte(int32 count, int32 invoking_key)
{
    if (insert_suggestion(suggestion_action::insert_to_end))
        return 0;

    return rl_forward_byte(count, invoking_key);
}

//------------------------------------------------------------------------------
int32_t clink_end_of_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    if (insert_suggestion(suggestion_action::insert_to_end))
        return 0;

    return tib::end_of_line(ctx, key, name, params);
}

//------------------------------------------------------------------------------
int32_t clink_insert_suggested_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    if (!insert_suggestion(suggestion_action::insert_to_end))
        tib::ding();

    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_insert_suggested_full_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    if (!insert_suggestion(suggestion_action::insert_next_full_word))
        tib::ding();

    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_insert_suggested_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    if (!insert_suggestion(suggestion_action::insert_next_word))
        tib::ding();

    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_accept_suggested_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    if (insert_suggestion(suggestion_action::insert_to_end))
        return clink_accept_line(ctx, key, name, params);

    tib::ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_popup_history(int32 count, int32 invoking_key)
{
    int32 current = -1;
    int32 orig_pos = where_history();
    int32 search_len = g_tib->get_caret();

    history_infos hi;
    if (!hi.make(g_rl_buffer->get_buffer(), search_len, orig_pos))
    {
ding:
        tib::ding();
        return 0;
    }

    // Popup list.
    const popup_results results = hi.activate_history_text_list(false/*win_history*/);

    switch (results.m_result)
    {
    case popup_result::cancel:
        break;
    case popup_result::error:
        goto ding;
    case popup_result::select:
    case popup_result::use:
        {
            history_set_pos(results.m_index);
            rl_replace_from_history(current_history(), 0);
            suppress_suggestions();

            const tib::textpos_t end = g_tib->get_length();
            const bool point_at_end = (!search_len || _rl_history_point_at_end_of_anchored_search);
            g_tib->set_caret(point_at_end ? end : search_len);
            g_tib->set_mark(point_at_end ? search_len : end);

            display_readline();
            if (results.m_result == popup_result::use)
                clink_newline(1, 0);
        }
        break;
    }

    return 0;
}



//------------------------------------------------------------------------------
static int32 adjust_point_delta(int32& point, int32 delta, char* buffer)
{
    if (delta <= 0)
        return 0;

    const int32 length = int32(strlen(buffer));
    if (point == length)
        return 0;

    if (point > length)
    {
        point = length;
        return 0;
    }

    if (delta > length - point)
        delta = length - point;

    int32 tmp = point;
    int32 count = 0;

#if defined (HANDLE_MULTIBYTE)
    if (MB_CUR_MAX == 1 || rl_byte_oriented)
#endif
    {
        tmp += delta;
        count += delta;
    }
#if defined (HANDLE_MULTIBYTE)
    else
    {
        while (delta)
        {
            int32 was = tmp;
            tmp = _rl_find_next_mbchar(buffer, tmp, 1, MB_FIND_NONZERO);
            if (tmp <= was)
                break;
            count++;
            delta--;
        }
    }
#endif

    point = tmp;
    return count;
}

//------------------------------------------------------------------------------
static int32 adjust_point_point(int32& point, int32 target, char* buffer)
{
    if (target <= point)
        return 0;

    const int32 length = int32(strlen(buffer));
    if (point == length)
        return 0;

    if (point > length)
    {
        point = length;
        return 0;
    }

    if (target > length)
        target = length;

    int32 tmp = point;
    int32 count = 0;

#if defined (HANDLE_MULTIBYTE)
    if (MB_CUR_MAX == 1 || rl_byte_oriented)
#endif
    {
        count = target - tmp;
        tmp = target;
    }
#if defined (HANDLE_MULTIBYTE)
    else
    {
        while (tmp < target)
        {
            int32 was = tmp;
            tmp = _rl_find_next_mbchar(buffer, tmp, 1, MB_FIND_NONZERO);
            if (tmp <= was)
                break;
            count++;
        }
    }
#endif

    point = tmp;
    return true;
}

//------------------------------------------------------------------------------
static int32 adjust_point_keyseq(int32& point, const char* keyseq, char* buffer)
{
    if (!keyseq || !*keyseq)
        return 0;

    const int32 length = int32(strlen(buffer));
    if (point == length)
        return 0;

    if (point > length)
    {
        point = length;
        return 0;
    }

    int32 tmp = point;
    int32 count = 0;

#if defined (HANDLE_MULTIBYTE)
    if (MB_CUR_MAX == 1 || rl_byte_oriented)
#endif
    {
        const char* found = strstr(buffer + tmp, keyseq);
        int32 delta = found ? int32(found - (buffer + tmp)) : length - tmp;
        tmp += delta;
        count += delta;
    }
#if defined (HANDLE_MULTIBYTE)
    else
    {
        int32 keyseq_len = int32(strlen(keyseq));
        while (buffer[tmp] && strncmp(buffer + tmp, keyseq, keyseq_len) != 0)
        {
            tmp = _rl_find_next_mbchar(buffer, tmp, 1, MB_FIND_NONZERO);
            count++;
        }
    }
#endif

    if (tmp > length)
        tmp = length;

    point = tmp;
    return count;
}

//------------------------------------------------------------------------------
static str<16, false> s_win_fn_input_buffer;
static bool read_win_fn_input_char()
{
    int32 c;

    RL_SETSTATE(RL_STATE_MOREINPUT);
    c = rl_read_key();
    RL_UNSETSTATE(RL_STATE_MOREINPUT);

    if (c < 0)
        return false;

    if (RL_ISSTATE(RL_STATE_MACRODEF))
        _rl_add_macro_char(c);

#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_restore_tty_signals ();
#endif

    if (c == 27/*Esc*/ || c == 7/*^G*/)
    {
nope:
        s_win_fn_input_buffer.clear();
        return true;
    }

    s_win_fn_input_buffer.concat(reinterpret_cast<const char*>(&c), 1);

    WCHAR_T wc;
    mbstate_t mbs = {};
    size_t validate = MBRTOWC(&wc, s_win_fn_input_buffer.c_str(), s_win_fn_input_buffer.length(), &mbs);

    if (MB_NULLWCH(validate))
        goto nope;

    // Once there's a valid UTF8 character, the input is complete.
    return !MB_INVALIDCH(validate);
}

//------------------------------------------------------------------------------
static char* get_history(int32 item)
{
    HIST_ENTRY** list = history_list();
    if (!list || !history_length)
        return nullptr;

    if (item >= history_length)
        item = history_length - 1;
    if (item < 0)
        return nullptr;

    return list[item]->line;
}

//------------------------------------------------------------------------------
static char* get_previous_command()
{
    int32 previous = where_history();
    return get_history(previous);
}

//------------------------------------------------------------------------------
int32 win_f1(int32 count, int32 invoking_key)
{
#ifdef TIB_TODO
    const bool had_selection = (cua_get_anchor() >= 0);

    if (insert_suggestion(suggestion_action::insert_to_end))
        return 0;

    if (count <= 0)
        count = 1;

    while (count && rl_point < rl_end)
    {
        rl_forward_char(1, invoking_key);
        count--;
    }

    if (!count)
        return 0;

    if (had_selection)
        return 0;

    char* prev_buffer = get_previous_command();
    if (!prev_buffer)
    {
ding:
        tib::ding();
        return 0;
    }

    int32 old_point = 0;
    adjust_point_point(old_point, rl_point, prev_buffer);
    if (!prev_buffer[old_point])
        goto ding;

    int32 end_point = old_point;
    adjust_point_delta(end_point, count, prev_buffer);
    if (end_point <= old_point)
        goto ding;

    str<> more;
    more.concat(prev_buffer + old_point, end_point - old_point);
    rl_insert_text(more.c_str());

    // Prevent generating a suggestion when inserting characters from the
    // previous command, otherwise it's often only possible to insert one
    // character before suggestions take over.
    suggestions suggestions;
    set_suggestions(rl_line_buffer, 0, &suggestions);
#endif

    return 0;
}

//------------------------------------------------------------------------------
static int32 finish_win_f2()
{
#ifdef TIB_TODO
#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_restore_tty_signals();
#endif

    rl_clear_message();

    char* prev_buffer = get_previous_command();
    if (!prev_buffer)
    {
        tib::ding();
        return 0;
    }

    if (s_win_fn_input_buffer.empty())
        return 0;

    int32 old_point = 0;
    adjust_point_point(old_point, rl_point, prev_buffer);
    if (prev_buffer[old_point])
    {
        int32 end_point = old_point;
        int32 count = adjust_point_keyseq(end_point, s_win_fn_input_buffer.c_str(), prev_buffer);
        if (end_point > old_point)
        {
            // How much to delete.
            int32 del_point = rl_point;
            adjust_point_delta(del_point, count, rl_line_buffer);

            // What to insert.
            str<> more;
            more.concat(prev_buffer + old_point, end_point - old_point);

            rl_begin_undo_group();
            rl_delete_text(rl_point, del_point);
            rl_insert_text(more.c_str());
            rl_end_undo_group();
        }
    }
#endif

    return 0;
}

//------------------------------------------------------------------------------
#if defined (READLINE_CALLBACKS)
int32 _win_f2_callback(_rl_callback_generic_arg *data)
{
#ifdef TIB_TODO
    if (!read_win_fn_input_char())
        return 0;

    /* Deregister function, let rl_callback_read_char deallocate data */
    _rl_callback_func = 0;
    want_redisplay_readline();

    return finish_win_f2();
#else
    return 0;
#endif
}
#endif

//------------------------------------------------------------------------------
static const char c_normal[] = "\001\x1b[m\002";
int32 win_f2(int32 count, int32 invoking_key)
{
#ifdef TIB_TODO
    s_win_fn_input_buffer.clear();
    rl_message("\x01\x1b[%sm\x02(enter char to copy up to: )%s ", get_popup_colors(), c_normal);

#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_disable_tty_signals ();
#endif

#if defined (READLINE_CALLBACKS)
    if (RL_ISSTATE(RL_STATE_CALLBACK))
    {
        _rl_callback_data = _rl_callback_data_alloc(count);
        _rl_callback_func = _win_f2_callback;
        return 0;
    }
#endif

    while (!read_win_fn_input_char())
        ;

    return finish_win_f2();
#else
    return 0;
#endif
}

//------------------------------------------------------------------------------
int32 win_f3(int32 count, int32 invoking_key)
{
    return win_f1(999999, invoking_key);
}

//------------------------------------------------------------------------------
static int32 finish_win_f4()
{
#ifdef TIB_TODO
#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_restore_tty_signals();
#endif

    rl_clear_message();

    if (s_win_fn_input_buffer.empty())
        return 0;

    int32 end_point = rl_point;
    adjust_point_keyseq(end_point, s_win_fn_input_buffer.c_str(), rl_line_buffer);
    if (end_point > rl_point)
        rl_delete_text(rl_point, end_point);
#endif

    return 0;
}

//------------------------------------------------------------------------------
#if defined (READLINE_CALLBACKS)
int32 _win_f4_callback(_rl_callback_generic_arg *data)
{
    if (!read_win_fn_input_char())
        return 0;

    /* Deregister function, let rl_callback_read_char deallocate data */
    _rl_callback_func = 0;
    want_redisplay_readline();

    return finish_win_f4();
}
#endif

//------------------------------------------------------------------------------
int32 win_f4(int32 count, int32 invoking_key)
{
    s_win_fn_input_buffer.clear();
    rl_message("\x01\x1b[%sm\x02(enter char to delete up to: )%s ", get_popup_colors(), c_normal);

#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_disable_tty_signals ();
#endif

#if defined (READLINE_CALLBACKS)
    if (RL_ISSTATE(RL_STATE_CALLBACK))
    {
        _rl_callback_data = _rl_callback_data_alloc(count);
        _rl_callback_func = _win_f4_callback;
        return 0;
    }
#endif

    while (!read_win_fn_input_char())
        ;

    return finish_win_f4();
}

//------------------------------------------------------------------------------
int32 win_f6(int32 count, int32 invoking_key)
{
    rl_insert_text("\x1a");
    return 0;
}

//------------------------------------------------------------------------------
int32 win_f7(int32 count, int32 invoking_key)
{

    history_infos hi;
    const int32 total = hi.make();
    if (!total)
    {
ding:
        tib::ding();
        return 0;
    }

    const popup_results results = hi.activate_history_text_list(true/*win_history*/);

    switch (results.m_result)
    {
    case popup_result::cancel:
        break;
    case popup_result::error:
        goto ding;
    case popup_result::use:
    case popup_result::select:
        {
            history_set_pos(results.m_index);
            rl_replace_from_history(current_history(), 0);
            suppress_suggestions();

            display_readline();
            if (results.m_result == popup_result::use)
                clink_newline(1, 0);
        }
        break;
    }

    return 0;
}

//------------------------------------------------------------------------------
static int32 s_history_number = -1;
static int32 finish_win_f9()
{
#ifdef TIB_TODO
#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_restore_tty_signals();
#endif

    rl_clear_message();

    if (s_history_number >= 1)
    {
        --s_history_number;
        if (s_history_number >= history_length)
            s_history_number = history_length - 1;
        if (history_length > 0)
        {
            rl_begin_undo_group();
            rl_delete_text(0, rl_end);
            rl_point = 0;
            rl_insert_text(get_history(s_history_number));
            rl_end_undo_group();
        }
    }
#endif

    return 0;
}

//------------------------------------------------------------------------------
static void set_f9_message()
{
#ifdef TIB_TODO
    if (s_history_number >= 0)
        rl_message("\x01\x1b[%sm\x02(enter history number: %d)%s ", get_popup_colors(), s_history_number, c_normal);
    else
        rl_message("\x01\x1b[%sm\x02(enter history number: )%s ", get_popup_colors(), c_normal);
#endif
}

//------------------------------------------------------------------------------
#ifdef TIB_TODO
static bool read_history_digit()
{
    int32 c;

    RL_SETSTATE(RL_STATE_MOREINPUT);
    c = rl_read_key();
    RL_UNSETSTATE(RL_STATE_MOREINPUT);

    if (c < 0)
        return false;

    if (RL_ISSTATE(RL_STATE_MACRODEF))
        _rl_add_macro_char(c);

#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_restore_tty_signals ();
#endif

    if (c >= '0' && c <= '9')
    {
        if (s_history_number < 0)
            s_history_number = 0;
        if (s_history_number <= 99999)
        {
            s_history_number *= 10;
            s_history_number += c - '0';
        }
    }
    else if (c == 27/*Esc*/ || c == 7/*^G*/)
    {
        s_history_number = -1;
        return true;
    }
    else if (c == 13/*Enter*/)
    {
        return true;
    }
    else if (c == 8/*Backspace*/)
    {
        s_history_number /= 10;
        if (s_history_number == 0)
            s_history_number = -1;
    }

    set_f9_message();
    return false;
}
#endif

//------------------------------------------------------------------------------
#if defined (READLINE_CALLBACKS)
int32 _win_f9_callback(_rl_callback_generic_arg *data)
{
#ifdef TIB_TODO
    if (!read_history_digit())
        return 0;

    /* Deregister function, let rl_callback_read_char deallocate data */
    _rl_callback_func = 0;
    want_redisplay_readline();

    return finish_win_f9();
#else
    return 0;
#endif
}
#endif

//------------------------------------------------------------------------------
int32 win_f9(int32 count, int32 invoking_key)
{
#ifdef TIB_TODO
    s_history_number = -1;
    set_f9_message();

#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_disable_tty_signals ();
#endif

#if defined (READLINE_CALLBACKS)
    if (RL_ISSTATE(RL_STATE_CALLBACK))
    {
        _rl_callback_data = _rl_callback_data_alloc(count);
        _rl_callback_func = _win_f9_callback;
        return 0;
    }
#endif

    while (!read_history_digit())
        ;

    return finish_win_f9();
#else
    return 0;
#endif
}

//------------------------------------------------------------------------------
bool win_fn_callback_pending()
{
    return (_rl_callback_func == _win_f2_callback ||
            _rl_callback_func == _win_f4_callback ||
            _rl_callback_func == _win_f9_callback);
}



//------------------------------------------------------------------------------
static int32_t kill_worker(tib::editor_context& ctx, bool forward, uint8_t word, bool copy=false) noexcept
{
    const auto line = ctx.get_text();

    if (ctx.has_selection())
    {
        tib::add_to_kill_ring(-1, line.c_str() + ctx.get_sel_begin(), ctx.get_sel_end() - ctx.get_sel_begin());
        ctx.del();
        return 0;
    }

    const tib::textpos_t orig_caret = ctx.get_caret();

    // Readline seems to be inconsistent about when it dings, but mimic them.
    if (!forward && !word && !copy)
    {
        tib::ding();
        return 0;
    }

    ctx.set_caret(orig_caret);

    auto inverted = (word ? (forward ? tib::backward_word : tib::forward_word)
                          : (forward ? tib::begin_of_line : tib::end_of_line));

    if (copy && word)
        inverted(ctx, 0, nullptr, nullptr);

    const tib::textpos_t c1 = ctx.get_caret();

    tib::do_with_numeric_argument(ctx, 0, nullptr, nullptr, inverted, [&]() {
        if (word)
        {
            return forward ? ctx.move_right(word) : ctx.move_left(word);
        }
        else
        {
            forward ? ctx.end_of_input(word) : ctx.begin_of_input(word);
            return false;
        }
    }, tib::NO_DING);

    const tib::textpos_t c2 = ctx.get_caret();

    const tib::textpos_t start = min(c1, c2);
    const tib::textpos_t end = max(c1, c2);
    tib::add_to_kill_ring((c1 < c2), line.c_str() + start, end - start);

    if (copy)
    {
        ctx.set_caret(orig_caret);
    }
    else if (start != end)
    {
        ctx.begin_undo_group();
        ctx.remove_text(start, end);
        ctx.set_mark(start);
        ctx.end_undo_group();
    }
    return 0;
}

//------------------------------------------------------------------------------
int32_t backward_kill_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    return kill_worker(ctx, false/*forward*/, true/*word*/);
}

//------------------------------------------------------------------------------
int32_t forward_kill_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    return kill_worker(ctx, true/*forward*/, true/*word*/);
}

//------------------------------------------------------------------------------
int32_t backward_kill_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    return kill_worker(ctx, false/*forward*/, false/*word*/);
}

//------------------------------------------------------------------------------
int32_t forward_kill_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    return kill_worker(ctx, true/*forward*/, false/*word*/);
}

//------------------------------------------------------------------------------
int32_t kill_full_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    const auto line = ctx.get_text();

    tib::add_to_kill_ring(-1, line.c_str(), line.length());

    if (!line.empty())
    {
        ctx.begin_undo_group();
        ctx.remove_text(0, ~0);
        ctx.set_mark(0);
        ctx.end_undo_group();
    }
    return 0;
}

//------------------------------------------------------------------------------
int32_t copy_backward_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    return kill_worker(ctx, false/*forward*/, true/*word*/, true/*copy*/);
}

//------------------------------------------------------------------------------
int32_t copy_forward_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    return kill_worker(ctx, true/*forward*/, true/*word*/, true/*copy*/);
}

//------------------------------------------------------------------------------
static int32_t kill_region_worker(tib::editor_context& ctx, bool copy) noexcept
{
    const auto line = ctx.get_text();

    const tib::textpos_t c1 = ctx.get_caret();
    const tib::textpos_t c2 = ctx.has_selection() ? ctx.get_anchor() : ctx.get_mark();

    const tib::textpos_t start = min(c1, c2);
    const tib::textpos_t end = max(c1, c2);
    tib::add_to_kill_ring(-1, line.c_str() + start, end - start);

    if (!copy && start != end)
    {
        ctx.begin_undo_group();
        ctx.remove_text(start, end);
        ctx.set_mark(start);
        ctx.end_undo_group();
    }
    return 0;
}

//------------------------------------------------------------------------------
int32_t kill_region(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    return kill_region_worker(ctx, false/*copy*/);
}

//------------------------------------------------------------------------------
int32_t copy_region_to_kill(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    return kill_region_worker(ctx, true/*copy*/);
}

//------------------------------------------------------------------------------
int32_t rubout_or_delete(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    const tib::textpos_t caret = ctx.get_caret();
    if (caret == ctx.get_length())
        return tib::del_char_left(ctx, 0, nullptr, nullptr);
    else
        return tib::del_char_right(ctx, 0, nullptr, nullptr);
}

//------------------------------------------------------------------------------
int32_t insert_close(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    // Only for use with these specific characters.
    if (key != ')' && key != ']' && key != '}')
    {
        tib::ding();
        return 0;
    }

    // Insert the character.
    const char c = char(key);
    ctx.insert_text(&c, 1, ctx.get_overwrite_mode());

    // An explicit argument bypasses the matching behavior.
    if (ctx.has_numeric_argument())
        return 0;

    // Redirected input bypasses the matching behavior.
    DWORD dummy;
    HANDLE h = get_std_handle(STD_INPUT_HANDLE);
    if (!h || !GetConsoleMode(h, &dummy))
        return 0;

    // Determine the matching opening character.
    const char o = (c == ')' ? '(' :
                    c == ']' ? '[' :
                    c == '}' ? '{' : 0);
    assert(o);
    if (!o)
    {
        tib::ding();
        return 0;
    }

    // Find the matching opening character.
    int32_t pending = -1;
    tib::textpos_t paren = ctx.get_caret() - 2; // -1 is the closing character.
    for (const auto text = ctx.get_text().c_str(); paren > 0; --paren)
    {
        if (text[paren] == c)
            --pending;
        else if (text[paren] == o)
            ++pending;
        if (!pending)
            break;
    }

    // No match?
    if (pending)
        return 0;

    // Any input available?
    if (tib::term_in_avail(0))
        return 0;

    // Remember the caret position.
    const auto caret = ctx.get_caret();
#if 0
    const auto top = ctx.get_top();
    const auto left = ctx.get_left();
#endif

    // Go to the matching paren and update the display.
    ctx.set_caret(paren);
    ctx.display();

    // The cursor is visible on the matching paren while waiting briefly for
    // input.
    {
        const auto was_visible = show_cursor(1);

        tib::term_in_avail(500);

        if (!was_visible)
            show_cursor(0);
    }

    // Restore the caret position.
    ctx.set_caret(caret);
#if 0
    ctx.set_top(top);
    ctx.set_left(left);
#endif
    return 0;
}

//------------------------------------------------------------------------------
int32_t insert_comment(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    const char* comment = _rl_comment_begin ? _rl_comment_begin : "::";
    const size_t comment_len = strlen(comment);
    ctx.begin_undo_group();
    ctx.begin_of_input();
    if (ctx.has_numeric_argument() && strnicmp(ctx.get_text().c_str(), comment, comment_len) == 0)
        ctx.remove_text(0, comment_len);
    else
        ctx.insert_text(_rl_comment_begin ? _rl_comment_begin : "::");
    ctx.end_undo_group();
    return clink_accept_line(ctx, 0, nullptr, nullptr);
}

//------------------------------------------------------------------------------
int32_t unix_filename_rubout(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    const tib::textpos_t end = ctx.get_caret();
    if (end == 0)
    {
        tib::ding();
        return 0;
    }

    const char* line = ctx.get_text().c_str();
    const char* p = line + end - 1;

    auto count = max(1, ctx.get_numeric_argument());
    while (count--)
    {
        // Retreat through spaces.
        while (p >= line && *p == ' ')
            --p;

        // Retreat through path separators.
        while (p >= line && path::is_separator(*p))
            --p;

        // Retreat through anything other than spaces or path separators.
        while (p >= line && *p != ' ' && !path::is_separator(*p))
            --p;
    }

    ++p;
    assert(p >= line);

    const tib::textpos_t start = tib::textpos_t(p - line);
    tib::add_to_kill_ring(false, line + start, end - start);

    if (end != start)
    {
        ctx.begin_undo_group();
        ctx.remove_text(start, end);
        ctx.set_mark(start);
        ctx.end_undo_group();
    }
    return 0;
}

//------------------------------------------------------------------------------
int32_t unix_line_discard(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    const tib::textpos_t end = ctx.get_caret();
    if (end == 0)
    {
        tib::ding();
        return 0;
    }

    tib::add_to_kill_ring(false, ctx.get_text().c_str(), end);

    if (end)
    {
        ctx.begin_undo_group();
        ctx.remove_text(0, end);
        ctx.set_mark(0);
        ctx.end_undo_group();
    }
    return 0;
}

//------------------------------------------------------------------------------
int32_t unix_word_rubout(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    const tib::textpos_t end = ctx.get_caret();
    if (end == 0)
    {
        tib::ding();
        return 0;
    }

    const char* line = ctx.get_text().c_str();
    const char* p = line + end - 1;

    auto count = max(1, ctx.get_numeric_argument());
    while (count--)
    {
        // Retreat through spaces.
        while (p >= line && *p == ' ')
            --p;

        // Retreat through non-spaces.
        while (p >= line && *p != ' ')
            --p;
    }

    ++p;
    assert(p >= line);

    const tib::textpos_t start = tib::textpos_t(p - line);
    tib::add_to_kill_ring(false, line + start, end - start);

    if (end != start)
    {
        ctx.begin_undo_group();
        ctx.remove_text(start, end);
        ctx.set_mark(start);
        ctx.end_undo_group();
    }
    return 0;
}

//------------------------------------------------------------------------------
int32_t yank(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    if (!tib::get_kill_ring_count())
    {
        tib::abort(ctx, key, name, params);
        return 1;
    }

    ctx.begin_undo_group();
    ctx.set_mark(ctx.get_caret());
    ctx.insert_text(tib::get_kill_ring_text(tib::get_kill_ring_index()));
    ctx.end_undo_group();
    return 0;
}

#ifdef TIB_TODO
//------------------------------------------------------------------------------
int32_t yank_last_arg(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    return 0;
}

//------------------------------------------------------------------------------
int32_t yank_nth_arg(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    return 0;
}
#endif

//------------------------------------------------------------------------------
int32_t yank_pop(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    const auto index = tib::get_kill_ring_index();
    const auto len = tib::get_kill_ring_text_length(index);
    tib::editor_command_func_t last_command = ctx.get_last_command_func();
    if (!len || (last_command != yank && last_command != yank_pop))
    {
nope:
        tib::abort(ctx, key, name, params);
        return 1;
    }

    const auto text = tib::get_kill_ring_text(index);
    const auto line = ctx.get_text().c_str();
    const auto caret = ctx.get_caret();
    if (len > caret || strncmp(line + caret - len, text, len) != 0)
        goto nope;

    ctx.begin_undo_group();
    ctx.remove_text(caret - len, caret);
    tib::pop_kill_ring_index();
    auto ret = yank(ctx, 0, nullptr, nullptr);
    ctx.end_undo_group();
    return ret;
}

//------------------------------------------------------------------------------
int32_t re_read_init_file(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    extern void initialise_readline(bool no_user=false);
    initialise_readline();
    return 0;
}

//------------------------------------------------------------------------------
int32_t refresh_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    force_redisplay_readline();
    display_readline();
    g_tib->clear_auto_deactivate_mark();
    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_tilde_expand(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    const auto line = ctx.get_text().c_str();
    const auto end = ctx.get_length();
    auto caret = ctx.get_caret();
    auto start = caret;

    // If the caret is immediately after a tilde, then expand the rest of the
    // word.  Otherwise, retreat to find a tilde at the beginning of a word.
    if (caret && line[caret - 1] == '~')
    {
        start = caret - 1;
    }
    else
    {
        // Retreat through non-spaces and non-quotes.
        while (start && line[start - 1] != ' ' && line[start - 1] != '"')
            --start;
    }

    // No tilde?  Easy out.
    if (line[start] != '~')
        return 0;

    // Advance through non-spaces and non-quotes.
    while (caret < end && line[caret] != ' ' && line[caret] != '"')
        ++caret;

    // Get the word.
    tib::cstring word;
    word.set(line + start, caret - start);

    // Expand it.
    char* expanded = tilde_expand(word.c_str());
    ctx.begin_undo_group();
    ctx.remove_text(start, caret);
    ctx.insert_text(expanded);
    ctx.end_undo_group();
    free(expanded);
    return 0;
}

//------------------------------------------------------------------------------
int32_t clear_display(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    // Cursor to top-left, clear screen, clear scrollback buffer.
    tib::term_out(CSI(H) CSI(2J) CSI(3J));

    reset_display_readline();
    refresh_input_line();
    return 0;
}

//------------------------------------------------------------------------------
int32_t clear_screen(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    if (ctx.has_numeric_argument())
        return refresh_line(ctx, 0, nullptr, nullptr);

    // Cursor to top-left, clear screen.
    tib::term_out(CSI(H) CSI(2J));

    reset_display_readline();
    refresh_input_line();
    return 0;
}



//------------------------------------------------------------------------------
static bool s_globbing_wild = false;
static bool s_literal_wild = false;
bool is_globbing_wild() { return s_globbing_wild; }
bool is_literal_wild() { return s_literal_wild; }

//------------------------------------------------------------------------------
static int32 glob_completion_internal(int32 what_to_do)
{
    s_globbing_wild = true;
    if (!rl_explicit_arg)
        s_literal_wild = true;

    return rl_complete_internal(what_to_do);
}

//------------------------------------------------------------------------------
int32 glob_complete_word(int32 count, int32 invoking_key)
{
    if (rl_editing_mode == emacs_mode)
        rl_explicit_arg = 1; /* force `*' append */

    return glob_completion_internal(rl_completion_mode(glob_complete_word));
}

//------------------------------------------------------------------------------
int32 glob_expand_word(int32 count, int32 invoking_key)
{
    return glob_completion_internal('*');
}

//------------------------------------------------------------------------------
int32 glob_list_expansions(int32 count, int32 invoking_key)
{
    return glob_completion_internal('?');
}



//------------------------------------------------------------------------------
int32 edit_and_execute_command(int32 count, int32 invoking_key)
{
#ifdef TIB_TODO
    str<> line;
    if (rl_explicit_arg)
    {
        HIST_ENTRY* h = history_get(count);
        if (!h)
        {
            tib::ding();
            return 0;
        }
        line = h->line;
    }
    else
    {
        line.concat(rl_line_buffer, rl_end);
        if (!host_add_history(0, line.c_str()))
        {
            tib::ding();
            return 0;
        }
    }

    str_moveable tmp_file;
    FILE* file = os::create_temp_file(&tmp_file);
    if (!file)
    {
LDing:
        tib::ding();
        return 0;
    }

    if (fputs(line.c_str(), file) < 0)
    {
        fclose(file);
LUnlinkFile:
        unlink(tmp_file.c_str());
        goto LDing;
    }
    fclose(file);
    file = nullptr;

    // Save and reset console state.
    HANDLE std_handles[2] = { GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE) };
    DWORD prev_mode[2];
    static_assert(_countof(std_handles) == _countof(prev_mode), "array sizes must match");
    for (size_t i = 0; i < _countof(std_handles); ++i)
        GetConsoleMode(std_handles[i], &prev_mode[i]);
    SetConsoleMode(std_handles[0], (prev_mode[0] | ENABLE_PROCESSED_INPUT) & ~(ENABLE_WINDOW_INPUT|ENABLE_MOUSE_INPUT));
    debug_show_console_mode(&prev_mode[0]);
    bool was_visible = show_cursor(true);
    rl_clear_signals();

    // Build editor command.
    str<> editor;
    str_moveable command;
    const char* const qs = (strpbrk(tmp_file.c_str(), rl_filename_quote_characters)) ? "\"" : "";
    if ((!os::get_env("VISUAL", editor) && !os::get_env("EDITOR", editor)) || editor.empty())
        editor = "%systemroot%\\system32\\notepad.exe";
    command.format("%s %s%s%s", editor.c_str(), qs, tmp_file.c_str(), qs);

    // Execute editor command.
    wstr_moveable wcommand(command.c_str());
    const int32 exit_code = _wsystem(wcommand.c_str());

    // Restore console state.
    show_cursor(was_visible);
    prev_mode[0] = cleanup_console_input_mode(prev_mode[0]);
    for (size_t i = 0; i < _countof(std_handles); ++i)
        SetConsoleMode(std_handles[i], prev_mode[i]);
    debug_show_console_mode();
    rl_set_signals();

    // Was the editor launched successfully?
    if (exit_code < 0)
        goto LUnlinkFile;

    // Read command(s) from temp file.
    line.clear();
    wstr_moveable wtmp_file(tmp_file.c_str());
    file = _wfopen(wtmp_file.c_str(), L"rt");
    if (!file)
        goto LUnlinkFile;
    char buffer[4096];
    while (true)
    {
        const int32 len = fread(buffer, 1, sizeof(buffer), file);
        if (len <= 0)
            break;
        line.concat(buffer, len);
    }
    fclose(file);

    // Trim trailing newlines to avoid redundant blank commands.  Ensure a final
    // newline so all lines get executed (otherwise it will go into edit mode).
    while (line.length() && line.c_str()[line.length() - 1] == '\n')
        line.truncate(line.length() - 1);
    line.concat("\n");

    // Split into multiple lines.
    std::list<str_moveable> overflow;
    strip_crlf(line.data(), overflow, paste_crlf_crlf, nullptr);
    strip_wakeup_chars(line);

    // Replace the input line with the content from the temp file.
    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(0, rl_end);
    rl_point = 0;
    if (!line.empty())
        g_rl_buffer->insert(line.c_str());
    g_rl_buffer->end_undo_group();

    // Queue any additional lines.
    enqueue_lines(overflow);

    // Accept the input and execute it.
    display_readline();
    clink_newline(1, invoking_key);
#endif

    return 0;
}

//------------------------------------------------------------------------------
int32 magic_space(int32 count, int32 invoking_key)
{
#ifdef TIB_TODO
    str<> in;
    str<> out;

    in.concat(g_rl_buffer->get_buffer(), g_rl_buffer->get_cursor());
    if (expand_history(in.c_str(), out))
    {
        g_rl_buffer->begin_undo_group();
        g_rl_buffer->remove(0, rl_point);
        rl_point = 0;
        if (!out.empty())
            g_rl_buffer->insert(out.c_str());
        g_rl_buffer->end_undo_group();
    }
#endif

    rl_insert(1, ' ');
    return 0;
}



//------------------------------------------------------------------------------
struct alert_char
{
    char32_t        ucs;
    char32_t        ucs2;
    const char*     text;
    uint32          len;
};

//------------------------------------------------------------------------------
static void list_ambiguous_codepoints(const char* tag, const std::vector<alert_char>& chars)
{
    static const char red[] = "\x1b[1;91;40m";
    static const char norm[] = "\x1b[m";

    str<> s;
    str<> tmp;

    s << "  " << tag << ":\n";
    g_terminal->write(s.c_str(), s.length());

    for (alert_char ac : chars)
    {
        // Print formatted string.

        if (ac.ucs2)
            s.format("        Unicode: %s0x%04X 0x%04X%s, UTF8", red, ac.ucs, ac.ucs2, norm);
        else
            s.format("        Unicode: %s0x%04X%s, UTF8", red, ac.ucs, norm);
        for (uint32 i = 0; i < ac.len; ++i)
        {
            tmp.format(" %s0x%02.2X%s", red, uint8(ac.text[i]), norm);
            s.concat(tmp.c_str(), tmp.length());
        }
        tmp.format(", reported width %s%d%s", red, clink_wcswidth(ac.text, ac.len), norm);
        s << tmp << ", text \"" << red;
        s.concat(ac.text, ac.len);
        s << norm << "\"\n";
        g_terminal->write(s.c_str(), s.length());

        // Log plain text string.

        ecma48_state state;
        ecma48_iter iter(s.c_str(), state);
        tmp.clear();

        while (const ecma48_code& code = iter.next())
            if (code.get_type() == ecma48_code::type_chars)
                tmp.concat(code.get_pointer(), code.get_length());

        tmp.trim();

        LOG("%s", tmp.c_str());
    }
}

//------------------------------------------------------------------------------
static void list_problem_codes(const std::vector<prompt_problem_details>& problems)
{
    static const char err[] = "\x1b[1;91;40m";
    static const char wrn[] = "\x1b[1;93;40m";
    static const char norm[] = "\x1b[m";

    str<> s;
    str<> tmp;

    for (auto const& problem : problems)
    {
        // Print formatted string.

        const char* color = (problem.type & BIT_PROMPT_PROBLEM) ? err : wrn;

        s.clear();
        s << "        " << color;
        if (problem.type & BIT_PROMPT_PROBLEM)
            s << "Problem:";
        else
            s << "Warning:";
        s << norm << " at offset ";

        tmp.format("%d", problem.offset);
        s << tmp.c_str() << ", text \"" << color;

        {
            str_iter iter(problem.code.c_str());
            const char* seq = iter.get_pointer();
            while (int32 c = iter.next())
            {
                if (c < 0x20)
                {
                    char ctrl[2] = { '^', char(c + 0x40) };
                    s.concat(ctrl, 2);
                }
                else if (c >= 0x7f && c < 0xa0)
                {
                    s.concat("^?", 2);
                }
                else
                {
                    s.concat(seq, int32(iter.get_pointer() - seq));
                }
                seq = iter.get_pointer();
            }
        }

        s << norm << "\"\n";
        g_terminal->write(s.c_str(), s.length());

        // Log plain text string.

        {
            ecma48_state state;
            ecma48_iter iter(s.c_str(), state);
            tmp.clear();

            while (const ecma48_code& code = iter.next())
                if (code.get_type() == ecma48_code::type_chars)
                    tmp.concat(code.get_pointer(), code.get_length());
        }

        tmp.trim();
        LOG("%s", tmp.c_str());
    }
}

//------------------------------------------------------------------------------
static void analyze_char_widths(const char* s, std::vector<alert_char>& cjk)
{
    if (!s)
        return;

    bool ignoring = false;
    str_iter iter(s);
    while (true)
    {
        const char* const text = iter.get_pointer();
        const int32 c = iter.next();
        if (!c)
            break;

        if (c == RL_PROMPT_START_IGNORE && !ignoring)
            ignoring = true;
        else if (c == RL_PROMPT_END_IGNORE && ignoring)
            ignoring = false;
        else if (!ignoring)
        {
            const int32 kind = test_ambiguous_width_char(c, &iter);
            if (kind)
            {
                alert_char ac = {};
                ac.ucs = c;
                if (kind == 4)
                    ac.ucs2 = iter.next();
                ac.text = text;
                ac.len = uint32(iter.get_pointer() - text);

                switch (kind)
                {
                case 1: cjk.push_back(ac); break;
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
class terminal_file : public terminal_out
{
public:
                            terminal_file(const char* file, int32 rows, int32 cols, int32 top);
    virtual                 ~terminal_file();
    virtual void            open() {}
    virtual void            begin() {}
    virtual void            end() {}
    virtual void            close() {}
    virtual void            write(const char* chars, int32 length);
    virtual bool            get_line_text(int32 line, str_base& out) const { return false; }
    virtual void            flush() {}
    virtual int32           get_columns() const { return m_cols; }
    virtual int32           get_rows() const { return m_rows; }
    virtual int32           get_top() const { return m_top; }
    virtual bool            get_cursor_pos(int16& x, int16& y) const { assert(false); x = y = 0; return false; }
    virtual int32           is_line_default_color(int32 line) const { assert(false); return -1; }
    virtual int32           line_has_color(int32 line, const BYTE* attrs, int32 num_attrs, BYTE mask=0xff) const { assert(false); return -1; }
    virtual int32           find_line(int32 starting_line, int32 distance, const char* text, find_line_mode mode, const BYTE* attrs=nullptr, int32 num_attrs=0, BYTE mask=0xff) const { assert(false); return -1; }

private:
    FILE* const             m_file;
    const int32             m_rows;
    const int32             m_cols;
    const int32             m_top;
};

//------------------------------------------------------------------------------
terminal_file::terminal_file(const char* file, int32 rows, int32 cols, int32 top)
: m_file(fopen(file, "w"))
, m_rows(rows)
, m_cols(cols)
, m_top(top)
{
}

//------------------------------------------------------------------------------
terminal_file::~terminal_file()
{
    if (m_file)
        fclose(m_file);
}

//------------------------------------------------------------------------------
void terminal_file::write(const char* chars, int32 length)
{
    if (m_file)
        fwrite(chars, length, 1, m_file);
}

//------------------------------------------------------------------------------
extern void task_manager_diagnostics();
static void do_clink_diagnostics(bool include_settings=false)
{
    static char bold[] = "\x1b[1m";
    static char norm[] = "\x1b[m";
    static char err[] = "\x1b[1;91;40m";
    static char lf[] = "\n";

    str<> t;
    const char* p;
    const int32 spacing = 16;
    const bool has_explicit_nonzero_arg = (g_tib && g_tib->has_numeric_argument() && g_tib->get_numeric_argument());

    int32 id = 0;
    host_context context;
    host_get_app_context(id, context);

    str<> _lambda_s;
    auto print_heading = [&](const char* text)
    {
        _lambda_s.clear();
        _lambda_s << bold << text << ":" << norm << lf;
        g_terminal->write(_lambda_s.c_str(), _lambda_s.length());
    };
    auto print_value = [&](const char* name, const char* value)
    {
        if (value && *value)
        {
            _lambda_s.format("  %-*s  %s\n", spacing, name, value);
            g_terminal->write(_lambda_s.c_str(), _lambda_s.length());
        }
    };

    // Version and binaries dir.

    print_heading("version");

    print_value("version", CLINK_VERSION_STR_WITH_BRANCH);
#ifdef DEBUG
    print_value("flavor", "DEBUG");
#endif
    print_value("binaries", context.binaries.c_str());

    if (has_explicit_nonzero_arg)
        print_value("architecture", AS_STR(ARCHITECTURE_NAME));

    // Session info.

    print_heading("session");

    t.format("%d", id);
    print_value("session", t.c_str());
    print_value("profile", context.profile.c_str());
    print_value("log", file_logger::get_path());    // ACTUAL FILE IN USE.
    print_value("default_settings", context.default_settings.c_str());

    settings::get_settings_file(t);
    print_value("settings", t.c_str());             // ACTUAL FILE IN USE.

    history_database* history = history_database::get();
    if (history)
    {
        history->get_history_path(t);
        print_value("history", t.c_str());          // ACTUAL FILE IN USE.
    }

    print_value("scripts", context.scripts.c_str());
    print_value("default_inputrc", context.default_inputrc.c_str());
    print_value("inputrc", rl_get_last_init_file());    // ACTUAL FILE IN USE.

    // Language info.

    const DWORD cpid = GetACP();
    const DWORD chcp = GetConsoleCP();
    if (has_explicit_nonzero_arg || cpid != 1252 || chcp != 437)
    {
        print_heading("language");

        t.format("%u", cpid);
        print_value("codepage", t.c_str());

        t.format("%u", chcp);
        print_value("console codepage", t.c_str());

        const DWORD kbid = LOWORD(GetKeyboardLayout(0));
        t.format("%u", kbid);
        print_value("keyboard langid", t.c_str());

        WCHAR wide_layout_name[KL_NAMELENGTH * 2];
        if (!GetKeyboardLayoutNameW(wide_layout_name))
            wide_layout_name[0] = 0;
        t = wide_layout_name;
        print_value("keyboard layout", t.c_str());
    }

    // Terminal info.

    const char* const ansicon_problem = get_ansicon_problem();
    if (has_explicit_nonzero_arg || ansicon_problem)
    {
        print_heading("terminal");

        make_found_ansi_handler_string(t);
        print_value("terminal", t.c_str());

        if (ansicon_problem)
        {
            t.format("        %sProblem:  ANSICON detected (%s).%s\n"
                     "        %sAvoid ANSICON on Windows 10 or greater; it's unnecessary,%s\n"
                     "        %sless functional, and greatly degrades performance.%s\n",
                     err, ansicon_problem, norm,
                     err, norm,
                     err, norm);
            g_terminal->write(t.c_str(), t.length());
        }
    }

    host_call_lua_rl_global_function("clink._internal._diagnostics");

    task_manager_diagnostics();

    // Check for known potential ambiguous character width issues.

    str_moveable display_prompt;
    display_prompt.concat(g_prompt_prefix.c_str());
    display_prompt.concat(g_prompt.c_str());

    {
        std::vector<alert_char> cjk;

        analyze_char_widths(display_prompt.c_str(), cjk);
        analyze_char_widths(g_rprompt.c_str(), cjk);

        if (cjk.size())
        {
            print_heading("ambiguous width characters in prompt");

            if (cjk.size())
            {
                list_ambiguous_codepoints("CJK ambiguous characters", cjk);
                g_terminal->write(
                    "    Running 'chcp 65001' can often fix width problems with these characters.\n"
                    "    Or you can use a different character.\n");
            }
        }
    }

    // Check for problem escape codes and characters in prompt string.

    {
        std::vector<prompt_problem_details> problems;
        prompt_contains_problem_codes(display_prompt.c_str(), &problems);

        if (!problems.empty())
        {
            print_heading("problematic codes in prompt");
            list_problem_codes(problems);
            g_terminal->write(
                "    These characters in the prompt string can cause problems.  Clink will try\n"
                "    to compensate as much as it can, but for best results you may need to fix\n"
                "    the prompt string by removing the characters.\n");
        }
    }

    // Optionally include settings.

    if (include_settings)
    {
        str<> value;
        bool printed = false;
        for (auto iter = settings::first(); auto* next = iter.next();)
        {
            str<> value;
            next->get_descriptive(value);
            t.format("%s = %s\n", next->get_name(), value.c_str());
            if (!printed)
            {
                g_terminal->write("\n\n");
                print_heading("clink_settings");
                printed = true;
            }
            g_terminal->write(t.c_str(), t.length());
        }
    }
}

//------------------------------------------------------------------------------
int32_t clink_diagnostics(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    end_prompt(true/*crlf*/);

    do_clink_diagnostics();

    if (!ctx.has_numeric_argument() || !ctx.get_numeric_argument())
        g_terminal->write("\n(Use a numeric argument for additional diagnostics; e.g. press Alt+1 first.)\n");

    rl_forced_update_display();
    return 0;
}

//------------------------------------------------------------------------------
int32_t clink_diagnostics_output(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept
{
    end_prompt(true/*crlf*/);

    int32 id = 0;
    host_context context;
    host_get_app_context(id, context);

    int32 rows = 50;
    int32 cols = 80;
    int32 top = 0;
    if (!is_test_harness())
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        {
            rows = (csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
            cols = (csbi.srWindow.Right - csbi.srWindow.Left) + 1;
            top = csbi.srWindow.Top;
        }
    }

    str_moveable file;
    path::join(context.profile.c_str(), "clink.info", file);
    terminal_file out(file.c_str(), rows, cols, top);

    {
        // Because redirecting to a file, not the console.
        suppress_implicit_write_console_logging nolog;

        g_tib->set_numeric_argument(999);
        g_terminal->redirect(&out);

        do_clink_diagnostics(true/*include_settings*/);

        g_terminal->redirect(nullptr);
        g_tib->clear_numeric_argument();
    }

    printf("Clink diagnostics output written to '%s'.\n", file.c_str());

    rl_forced_update_display();
    return 0;
}



//------------------------------------------------------------------------------
void reset_command_states()
{
    s_globbing_wild = false;
    s_literal_wild = false;
}
