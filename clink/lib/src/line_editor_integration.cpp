// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_editor_impl.h"
#include "line_editor_integration.h"
#include "host_callbacks.h"
#include "match_pipeline.h"
#include "reclassify.h"
#include "rl/rl_suggestions.h"

#include <core/str.h>
#include <core/str_transform.h>
#include <core/str_unordered_set.h>
#include <core/linear_allocator.h>
#include <core/os.h>
#include <core/debugheap.h>

//------------------------------------------------------------------------------
static line_editor_impl* s_editor = nullptr;
static host_callbacks* s_callbacks = nullptr;

//------------------------------------------------------------------------------
void set_active_line_editor(line_editor_impl* editor, host_callbacks* callbacks)
{
    assertimplies(!editor, !callbacks);
    assertimplies(editor, !s_editor);
    assertimplies(editor, !s_callbacks);

    s_editor = editor;
    s_callbacks = callbacks;
}



//------------------------------------------------------------------------------
extern "C" const char* host_get_env(const char* name)
{
    static int32 rotate = 0;
    static str<> rotating_tmp[10];

    str<>& s = rotating_tmp[rotate];
    rotate = (rotate + 1) % sizeof_array(rotating_tmp);
    if (!os::get_env(name, s))
        return nullptr;
    return s.c_str();
}



//------------------------------------------------------------------------------
const char** host_copy_dir_history(int32* total)
{
    if (!s_callbacks)
        return nullptr;

    return s_callbacks->copy_dir_history(total);
}



//------------------------------------------------------------------------------
void set_prompt(const char* prompt, const char* rprompt, bool redisplay)
{
    if (!s_editor)
        return;

    s_editor->set_prompt(prompt, rprompt, redisplay);
}



//------------------------------------------------------------------------------
bool is_regen_blocked()
{
    if (!s_editor)
        return false;

    return s_editor->m_matches.is_regen_blocked();
}

//------------------------------------------------------------------------------
void reset_generate_matches()
{
    if (!s_editor)
        return;

    s_editor->reset_generate_matches();
}

//------------------------------------------------------------------------------
void reselect_matches()
{
    if (!s_editor)
        return;

    s_editor->reselect_matches();
}



//------------------------------------------------------------------------------
static str_unordered_set s_deprecated_argmatchers;
static linear_allocator s_deprecated_argmatchers_store(1024);

//------------------------------------------------------------------------------
void clear_deprecated_argmatchers()
{
    s_deprecated_argmatchers.clear();
    s_deprecated_argmatchers_store.reset();
}

//------------------------------------------------------------------------------
void mark_deprecated_argmatcher(const char* command)
{
    if (s_deprecated_argmatchers.find(command) == s_deprecated_argmatchers.end())
    {
        dbg_ignore_scope(snapshot, "deprecated argmatcher lookup");
        const char* store = s_deprecated_argmatchers_store.store(command);
        s_deprecated_argmatchers.insert(store);
    }
}

//------------------------------------------------------------------------------
bool has_deprecated_argmatcher(const char* command)
{
    wstr<32> in(command);
    wstr<32> out;
    str_transform(in.c_str(), in.length(), out, transform_mode::lower);
    str<32> name(out.c_str());
    return s_deprecated_argmatchers.find(name.c_str()) != s_deprecated_argmatchers.end();
}



//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
void force_update_internal(bool restrict)
{
    if (!s_editor)
        return;

    s_editor->force_update_internal(restrict);
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
void update_matches()
{
    if (!s_editor)
        return;

    s_editor->update_matches();
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
matches* get_mutable_matches(bool nosort)
{
    if (!s_editor)
        return nullptr;

    return s_editor->get_mutable_matches(nosort);
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
matches* maybe_regenerate_matches(const char* needle, display_filter_flags flags)
{
    if (!s_editor || s_editor->m_matches.is_regen_blocked())
        return nullptr;

    // Check if a match display filter is active.
    matches_impl& regen = s_editor->m_regen_matches;
    bool old_filtering = false;
    if (!regen.match_display_filter(nullptr, nullptr, nullptr, flags, &old_filtering))
        return nullptr;

#ifdef DEBUG
    int32 debug_filter = dbg_get_env_int("DEBUG_FILTER");
    if (debug_filter) puts("REGENERATE_MATCHES");
#endif

    command_line_states command_line_states;
    std::vector<word> words;
    uint32 command_offset = s_editor->collect_words(words, &regen, collect_words_mode::stop_at_cursor, command_line_states);

    match_pipeline pipeline(regen);
    pipeline.reset();

#ifdef DEBUG
    if (debug_filter) puts("-- GENERATE");
#endif

    const auto linestates = command_line_states.get_linestates(s_editor->m_buffer);
    pipeline.generate(linestates, s_editor->m_generator, old_filtering);

#ifdef DEBUG
    if (debug_filter) puts("-- SELECT");
#endif

    pipeline.select(needle);
    pipeline.sort();

#ifdef DEBUG
    if (debug_filter)
    {
        matches_iter iter = regen.get_iter();
        while (iter.next())
            printf("match '%s'\n", iter.get_match());
        puts("-- DONE");
    }
#endif

    if (old_filtering)
    {
        // Using old_filtering lets deprecated generators filter based on the
        // input needle.  That poisons the collected matches for any other use,
        // so the matches must be reset.
        reset_generate_matches();
    }

    return &regen;
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
uint32 collect_words(const line_buffer& buffer, std::vector<word>& words, collect_words_mode mode)
{
    if (!s_editor)
        return 0;

    return s_editor->collect_words(buffer, words, mode);
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
void refresh_recognizer()
{
    if (s_editor)
        s_editor->reclassify(reclassify_reason::recognizer);
}



//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
void host_send_event(const char* event_name)
{
    if (!s_callbacks)
        return;

    s_callbacks->send_event(event_name);
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
void host_send_oninputlinechanged_event(const char* line)
{
    if (!s_callbacks)
        return;

    s_callbacks->send_oninputlinechanged_event(line);
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
bool host_call_lua_rl_global_function(const char* func_name)
{
    if (!s_editor)
        return false;

    return s_editor->call_lua_rl_global_function(func_name);
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
bool host_call_lua_rl_global_function(const char* func_name, const line_state* line)
{
    if (!s_callbacks)
        return false;

    return s_callbacks->call_lua_rl_global_function(func_name, line);
}



//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
void host_filter_prompt()
{
    if (!s_callbacks)
        return;

    s_callbacks->filter_prompt();
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
extern "C" void host_filter_transient_prompt(int32 crlf)
{
    if (!s_callbacks)
        return;

    s_callbacks->filter_transient_prompt(crlf < 0/*final*/);
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
int32 host_filter_matches(char** matches)
{
    if (!s_callbacks)
        return 0;

    return s_callbacks->filter_matches(matches);
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
void host_invalidate_matches()
{
    reset_generate_matches();
    clear_suggestion();
    if (s_editor)
        s_editor->try_suggest();
}



//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
bool host_can_suggest(const line_state& line)
{
    if (!s_callbacks)
        return false;

    return s_callbacks->can_suggest(line);
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
bool host_suggest(const line_states& lines, matches* matches, int32 generation_id)
{
    if (!s_callbacks)
        return false;

    return s_callbacks->suggest(lines, matches, generation_id);
}



//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
bool notify_matches_ready(std::shared_ptr<match_builder_toolkit> toolkit, int32 generation_id)
{
    if (!s_editor || !toolkit)
        return false;

    matches* matches = toolkit->get_matches();
    return s_editor->notify_matches_ready(generation_id, matches);
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
void override_line_state(const char* line, const char* needle, int32 point)
{
    assert(s_editor);
    if (!s_editor)
        return;

    s_editor->override_line(line, needle, point);
}

//------------------------------------------------------------------------------
#ifdef DEBUG
bool is_line_state_overridden()
{
    if (!s_editor)
        return false;

    return s_editor->is_line_overridden();
}
#endif

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
void before_display_readline()
{
    assert(s_editor);
    if (s_editor)
        s_editor->classify();
}

//------------------------------------------------------------------------------
// WARNING:  This calls Lua using the MAIN coroutine.
void reclassify(reclassify_reason why)
{
    if (s_editor)
        s_editor->reclassify(why);
}
