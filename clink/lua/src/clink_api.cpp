// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_input_idle.h"
#include "line_state_lua.h"
#include "prompt.h"
#include "../../app/src/version.h" // Ugh.

#include <core/base.h>
#include <core/log.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/str_iter.h>
#include <core/str_transform.h>
#include <core/str_tokeniser.h>
#include <core/str_unordered_set.h>
#include <core/settings.h>
#include <core/linear_allocator.h>
#include <core/debugheap.h>
#include <lib/intercept.h>
#include <lib/popup.h>
#include <lib/word_collector.h>
#include <lib/cmd_tokenisers.h>
#include <lib/reclassify.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>
#include <terminal/screen_buffer.h>
#include <readline/readline.h>

extern "C" {
#include <lua.h>
#include <lstate.h>
#include <readline/history.h>
}

#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>
#include <shlwapi.h>



//------------------------------------------------------------------------------
extern int force_reload_scripts();
extern void host_signal_delayed_init();
extern void host_mark_deprecated_argmatcher(const char* name);
extern void set_suggestion(const char* line, unsigned int endword_offset, const char* suggestion, unsigned int offset);
extern setting_bool g_gui_popups;
extern setting_enum g_dupe_mode;
extern setting_color g_color_unrecognized;
extern setting_color g_color_executable;

#ifdef _WIN64
static const char c_uninstall_key[] = "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
#else
static const char c_uninstall_key[] = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
#endif

#ifdef TRACK_LOADED_LUA_FILES
extern "C" int is_lua_file_loaded(lua_State* state, const char* filename);
#endif



//------------------------------------------------------------------------------
static bool search_for_extension(str_base& full, const char* word, str_base& out)
{
    path::append(full, "");
    const unsigned int trunc = full.length();

    str<> pathext;
    if (!os::get_env("pathext", pathext))
        return false;

    str_tokeniser tokens(pathext.c_str(), ";");
    const char *start;
    int length;

    const char* ext = path::get_extension(word);
    str<16> token_ext;

    while (str_token token = tokens.next(start, length))
    {
        if (ext)
        {
            token_ext.clear();
            token_ext.concat(start, length);
            if (token_ext.iequals(ext))
            {
                full.truncate(trunc);
                path::append(full, word);
                if (os::get_path_type(full.c_str()) == os::path_type_file)
                {
                    out = full.c_str();
                    return true;
                }
            }
        }
        else
        {
            full.truncate(trunc);
            path::append(full, word);
            full.concat(start, length);
            if (os::get_path_type(full.c_str()) == os::path_type_file)
            {
                out = full.c_str();
                return true;
            }
        }
    }

    return false;
}

//------------------------------------------------------------------------------
static bool search_for_executable(const char* _word, const char* cwd, str_base& out)
{
    // Bail out early if it's obviously not going to succeed.
    if (strlen(_word) >= MAX_PATH)
        return false;

// TODO: dynamically load NeedCurrentDirectoryForExePathW.
    wstr<32> word(_word);
    const bool need_cwd = !!NeedCurrentDirectoryForExePathW(word.c_str());
    const bool need_path = !rl_last_path_separator(_word);

    // Make list of paths to search.
    str<> tmp;
    str<> paths;
    if (need_cwd)
        paths = cwd;
    if (need_path && os::get_env("PATH", tmp))
    {
        if (paths.length() > 0)
            paths.concat(";", 1);
        paths.concat(tmp.c_str(), tmp.length());
    }

    str<> full;
    str<280> token;
    str_tokeniser tokens(paths.c_str(), ";");
    while (tokens.next(token))
    {
        token.trim();
        if (token.empty())
            continue;

        // Get full path name.
        path::join(cwd, token.c_str(), tmp);
        if (!os::get_full_path_name(tmp.c_str(), full, tmp.length()))
            continue;

        // Skip drives that are unknown, invalid, or remote.
        {
            char drive[4];
            drive[0] = full.c_str()[0];
            drive[1] = ':';
            drive[2] = '\\';
            drive[3] = '\0';
            if (os::get_drive_type(drive) < os::drive_type_removable)
                continue;
        }

        // Try PATHEXT extensions.
        if (search_for_extension(full, _word, out))
            return true;
    }

    return false;
}

//------------------------------------------------------------------------------
bool has_file_association(const char* name)
{
    const char* ext = path::get_extension(name);
    if (!ext)
        return false;

    if (os::get_path_type(name) != os::path_type_file)
        return false;

    wstr<32> wext(ext);
    DWORD cchOut = 0;
    HRESULT hr = AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN|ASSOCF_NOFIXUPS, ASSOCSTR_EXECUTABLE, wext.c_str(), nullptr, nullptr, &cchOut);
    if (FAILED(hr) || !cchOut)
        return false;

    return true;
}

//------------------------------------------------------------------------------
class recognizer
{
    friend HANDLE get_recognizer_event();

    struct cache_entry
    {
        str_moveable        m_file;
        recognition         m_recognition;
    };

    struct entry
    {
                            entry() {}
        bool                empty() const { return m_key.empty(); }
        void                clear();
        str_moveable        m_key;
        str_moveable        m_word;
        str_moveable        m_cwd;
    };

public:
                            recognizer();
                            ~recognizer() { assert(!m_thread); }
    void                    shutdown();
    void                    clear();
    int                     find(const char* key, recognition& cached, str_base* file) const;
    bool                    enqueue(const char* key, const char* word, const char* cwd, recognition* cached=nullptr);
    bool                    need_refresh();
    void                    end_line();

private:
    bool                    usable() const;
    bool                    store(const char* word, const char* file, recognition cached, bool pending=false);
    bool                    dequeue(entry& entry);
    bool                    set_result_available(bool available);
    void                    notify_ready(bool available);
    static void             proc(recognizer* r);

private:
    linear_allocator        m_heap;
    str_unordered_map<cache_entry> m_cache;
    str_unordered_map<cache_entry> m_pending;
    entry                   m_queue;
    mutable std::recursive_mutex m_mutex;
    std::unique_ptr<std::thread> m_thread;
    HANDLE                  m_event = nullptr;
    bool                    m_processing = false;
    bool                    m_result_available = false;
    volatile bool           m_zombie = false;

    static HANDLE           s_ready_event;
};

//------------------------------------------------------------------------------
HANDLE recognizer::s_ready_event = nullptr;
static recognizer s_recognizer;

//------------------------------------------------------------------------------
HANDLE get_recognizer_event()
{
    str<32> tmp;
    g_color_unrecognized.get_descriptive(tmp);
    if (tmp.empty())
    {
        str<32> tmp2;
        g_color_executable.get_descriptive(tmp2);
        if (tmp2.empty())
            return nullptr;
    }

    // Locking is not needed because concurrency is not possible until after
    // this event has been created, which can only happen on the main thread.

    if (s_recognizer.m_zombie)
        return nullptr;
    return s_recognizer.s_ready_event;
}

//------------------------------------------------------------------------------
bool check_recognizer_refresh()
{
    return s_recognizer.need_refresh();
}

//------------------------------------------------------------------------------
extern "C" void end_recognizer()
{
    s_recognizer.end_line();
    s_recognizer.clear();
}

//------------------------------------------------------------------------------
void shutdown_recognizer()
{
    s_recognizer.shutdown();
}

//------------------------------------------------------------------------------
void recognizer::entry::clear()
{
    m_key.clear();
    m_word.clear();
    m_cwd.clear();
}

//------------------------------------------------------------------------------
recognizer::recognizer()
: m_heap(1024)
{
#ifdef DEBUG
    // Singleton; assert if there's ever more than one.
    static bool s_created = false;
    assert(!s_created);
    s_created = true;
#endif

    s_recognizer.s_ready_event = CreateEvent(nullptr, true, false, nullptr);
}

//------------------------------------------------------------------------------
void recognizer::clear()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    m_queue.clear();
    m_cache.clear();
    m_pending.clear();
    m_heap.reset();
}

//------------------------------------------------------------------------------
int recognizer::find(const char* key, recognition& cached, str_base* file) const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (usable())
    {
        auto const iter = m_cache.find(key);
        if (iter != m_cache.end())
        {
            cached = iter->second.m_recognition;
            if (file)
                *file = iter->second.m_file.c_str();
            return 1;
        }
    }

    if (usable())
    {
        auto const iter = m_pending.find(key);
        if (iter != m_pending.end())
        {
            cached = iter->second.m_recognition;
            if (file)
                *file = iter->second.m_file.c_str(); // Always empty.
            return -1;
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
bool recognizer::enqueue(const char* key, const char* word, const char* cwd, recognition* cached)
{
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);

        if (!usable())
            return false;

        assert(s_ready_event);

        if (!m_event)
        {
            m_event = CreateEvent(nullptr, false, false, nullptr);
            if (!m_event)
                return false;
        }

        if (!m_thread)
        {
            dbg_ignore_scope(snapshot, "Recognizer thread");
            m_thread = std::make_unique<std::thread>(&proc, this);
        }

        m_queue.m_key = key;
        m_queue.m_word = word;
        m_queue.m_cwd = cwd;

        // Assume unrecognized at first.
        store(key, nullptr, recognition::unrecognized, true/*pending*/);
        if (cached)
            *cached = recognition::unrecognized;

        SetEvent(m_event);  // Signal thread there is work to do.
    }

    Sleep(0);           // Give up timeslice in case thread gets result quickly.
    return true;
}

//------------------------------------------------------------------------------
bool recognizer::need_refresh()
{
    return set_result_available(false);
}

//------------------------------------------------------------------------------
void recognizer::end_line()
{
    HANDLE ready_event;
    bool processing;

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        ready_event = s_ready_event;
        processing = m_processing && !m_zombie;
        // s_ready_event is never closed, so there is no concurrency concern
        // about it going from non-null to null.
        if (!ready_event)
            return;
    }

    // If the recognizer is still processing something then wait briefly until
    // processing is finished, in case it finishes quickly enough to be able to
    // refresh the input line colors.
    if (processing)
    {
        const DWORD tick_begin = GetTickCount();
        while (true)
        {
            const volatile DWORD tick_now = GetTickCount();
            const int timeout = int(tick_begin) + 2500 - int(tick_now);
            if (timeout < 0)
                break;

            if (WaitForSingleObject(ready_event, DWORD(timeout)) != WAIT_OBJECT_0)
                break;

            host_reclassify(reclassify_reason::recognizer);

            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (!m_processing || !usable())
                break;
        }
    }

    host_reclassify(reclassify_reason::recognizer);
}

//------------------------------------------------------------------------------
bool recognizer::usable() const
{
    return !m_zombie && s_ready_event;
}

//------------------------------------------------------------------------------
bool recognizer::store(const char* word, const char* file, recognition cached, bool pending)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (!usable())
        return false;

    auto& map = pending ? m_pending : m_cache;

    auto const iter = map.find(word);
    if (iter != map.end())
    {
        cache_entry entry;
        entry.m_file = file;
        entry.m_recognition = cached;
        map.insert_or_assign(iter->first, std::move(entry));
        set_result_available(true);
        return true;
    }

    dbg_ignore_scope(snapshot, "Recognizer");
    const char* key = m_heap.store(word);
    if (!key)
        return false;

    cache_entry entry;
    entry.m_file = file;
    entry.m_recognition = cached;
    map.emplace(key, std::move(entry));
    set_result_available(true);
    return true;
}

//------------------------------------------------------------------------------
bool recognizer::dequeue(entry& entry)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (!usable() || m_queue.empty())
        return false;

    entry = std::move(m_queue);
    assert(m_queue.empty());
    return true;
}

//------------------------------------------------------------------------------
bool recognizer::set_result_available(const bool available)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    const bool was_available = m_result_available;
    const bool changing = (available != m_result_available);

    if (changing)
        m_result_available = available;

    if (s_ready_event)
    {
        if (!available)
            ResetEvent(s_ready_event);
        else if (changing)
            SetEvent(s_ready_event);
    }

    return was_available;
}

//------------------------------------------------------------------------------
void recognizer::notify_ready(bool available)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (available)
        set_result_available(available);

    // Always set the ready event, even if no results are available:  this lets
    // end_line() stop waiting when the queue is finished being processed, even
    // if no results are available.
    if (s_ready_event)
        SetEvent(s_ready_event);
}

//------------------------------------------------------------------------------
void recognizer::shutdown()
{
    std::unique_ptr<std::thread> thread;

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);

        clear();
        m_zombie = true;

        if (m_event)
            SetEvent(m_event);

        thread = std::move(m_thread);
    }

    if (thread)
        thread->join();

    if (m_event)
        CloseHandle(m_event);
}

//------------------------------------------------------------------------------
void recognizer::proc(recognizer* r)
{
    CoInitialize(0);

    while (true)
    {
        if (WaitForSingleObject(r->m_event, INFINITE) != WAIT_OBJECT_0)
        {
            // Uh oh.
            Sleep(5000);
        }

        entry entry;
        while (true)
        {
            {
                std::lock_guard<std::recursive_mutex> lock(r->m_mutex);
                if (r->m_zombie || !r->dequeue(entry))
                {
                    r->m_processing = false;
                    r->m_pending.clear();
                    if (!r->m_zombie)
                        r->notify_ready(false);
                    break;
                }
                r->m_processing = true;
            }

            // Search for executable file.
            str<> found;
            recognition result = recognition::unrecognized;
            if (search_for_executable(entry.m_word.c_str(), entry.m_cwd.c_str(), found) ||
                has_file_association(entry.m_word.c_str()))
            {
                result = recognition::executable;
            }

            // Store result.
            r->store(entry.m_key.c_str(), found.c_str(), result);
            r->notify_ready(true);
        }

        if (r->m_zombie)
            break;
    }

    CoUninitialize();
puts("ended");
}

//------------------------------------------------------------------------------
recognition recognize_command(const char* line, const char* word, bool quoted, bool& ready, str_base* file)
{
    ready = true;

    str<> tmp;
    if (!quoted)
    {
        str_iter iter(word);
        while (iter.more())
        {
            const char* ptr = iter.get_pointer();
            const int c = iter.next();
            if (c != '^' || !iter.peek())
                tmp.concat(ptr, static_cast<int>(iter.get_pointer() - ptr));
        }
        word = tmp.c_str();
    }

    str<> tmp2;
    if (os::expand_env(word, -1, tmp2))
        word = tmp2.c_str();

    // Ignore UNC paths, because they can take up to 2 minutes to time out.
    // Even running that on a thread would either starve the consumers or
    // accumulate threads faster than they can finish.
    if (path::is_unc(word))
        return recognition::unknown;

    // Check for directory intercepts (-, ..., ...., dir\, and so on).
    if (intercept_directory(line) != intercept_result::none)
        return recognition::navigate;

    // Check for drive letter.
    if (word[0] && word[1] == ':' && !word[2])
    {
        int type = os::get_drive_type(word);
        if (type > os::drive_type_invalid)
            return recognition::navigate;
    }

    // Check for cached result.
    recognition cached;
    const int found = s_recognizer.find(word, cached, file);
    if (found)
    {
        ready = (found > 0);
        return cached;
    }

    // Expand environment variables.
    str<32> expanded;
    const char* orig_word = word;
    unsigned int len = static_cast<unsigned int>(strlen(word));
    if (os::expand_env(word, len, expanded))
    {
        word = expanded.c_str();
        len = expanded.length();
    }

    // Wildcards mean it can't be an executable file.
    if (strchr(word, '*') || strchr(word, '?'))
        return recognition::unrecognized;

    // Queue for background thread processing.
    str<> cwd;
    os::get_current_dir(cwd);
    if (!s_recognizer.enqueue(orig_word, word, cwd.c_str(), &cached))
        return recognition::unknown;

    ready = false;
    return cached;
}



//------------------------------------------------------------------------------
/// -name:  clink.print
/// -ver:   1.2.11
/// -arg:   ...
/// This works like
/// <a href="https://www.lua.org/manual/5.2/manual.html#pdf-print">print()</a>,
/// but this supports ANSI escape codes and Unicode.
///
/// If the special value <code>NONL</code> is included anywhere in the argument
/// list then the usual trailing newline is omitted.  This can sometimes be
/// useful particularly when printing certain ANSI escape codes.
///
/// <strong>Note:</strong>  In Clink versions before v1.2.11 the
/// <code>clink.print()</code> API exists (undocumented) but accepts exactly one
/// string argument and is therefore not fully compatible with normal
/// <code>print()</code> syntax.  If you use fewer or more than 1 argument or if
/// the argument is not a string, then first checking the Clink version (e.g.
/// <a href="#clink.version_encoded">clink.version_encoded</a>) can avoid
/// runtime errors.
/// -show:  clink.print("\x1b[32mgreen\x1b[m \x1b[35mmagenta\x1b[m")
/// -show:  -- Outputs <code>green</code> in green, a space, and <code>magenta</code> in magenta.
/// -show:
/// -show:  local a = "hello"
/// -show:  local world = 73
/// -show:  clink.print("a", a, "world", world)
/// -show:  -- Outputs <code>a       hello   world   73</code>.
/// -show:
/// -show:  clink.print("hello", NONL)
/// -show:  clink.print("world")
/// -show:  -- Outputs <code>helloworld</code>.
static int clink_print(lua_State* state)
{
    str<> out;
    bool nl = true;
    bool err = false;

    int n = lua_gettop(state);              // Number of arguments.
    lua_getglobal(state, "NONL");           // Special value `NONL`.
    lua_getglobal(state, "tostring");       // Function to convert to string (reused each loop iteration).

    int printed = 0;
    for (int i = 1; i <= n; i++)
    {
        // Check for magic `NONL` value.
        if (lua_compare(state, -2, i, LUA_OPEQ))
        {
            nl = false;
            continue;
        }

        // Call function to convert arg to a string.
        lua_pushvalue(state, -1);           // Function to be called (tostring).
        lua_pushvalue(state, i);            // Value to print.
        if (lua_state::pcall(state, 1, 1) != 0)
        {
            if (const char* error = lua_tostring(state, -1))
            {
                puts("");
                puts(error);
            }
            return 0;
        }

        // Get result from the tostring call.
        size_t l;
        const char* s = lua_tolstring(state, -1, &l);
        if (s == NULL)
        {
            err = true;
            break;                          // Allow accumulated output to be printed before erroring out.
        }
        lua_pop(state, 1);                  // Pop result.

        // Add tab character to the output.
        if (printed++)
            out << "\t";

        // Add string result to the output.
        out.concat(s, int(l));
    }

    if (g_printer)
    {
        if (nl)
            out.concat("\n");
        g_printer->print(out.c_str(), out.length());
    }
    else
    {
        printf("%s%s", out.c_str(), nl ? "\n" : "");
    }

    if (err)
        return luaL_error(state, LUA_QL("tostring") " must return a string to " LUA_QL("print"));

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.version_encoded
/// -ver:   1.1.10
/// -var:   integer
/// The Clink version number encoded as a single integer following the format
/// <span class="arg">Mmmmpppp</span> where <span class="arg">M</span> is the
/// major part, <span class="arg">m</span> is the minor part, and
/// <span class="arg">p</span> is the patch part of the version number.
///
/// For example, Clink v95.6.723 would be <code>950060723</code>.
///
/// This format makes it easy to test for feature availability by encoding
/// version numbers from the release notes.

//------------------------------------------------------------------------------
/// -name:  clink.version_major
/// -ver:   1.1.10
/// -var:   integer
/// The major part of the Clink version number.
/// For v<strong>1</strong>.2.3.a0f14d the major version is 1.

//------------------------------------------------------------------------------
/// -name:  clink.version_minor
/// -ver:   1.1.10
/// -var:   integer
/// The minor part of the Clink version number.
/// For v1.<strong>2</strong>.3.a0f14d the minor version is 2.

//------------------------------------------------------------------------------
/// -name:  clink.version_patch
/// -ver:   1.1.10
/// -var:   integer
/// The patch part of the Clink version number.
/// For v1.2.<strong>3</strong>.a0f14d the patch version is 3.

//------------------------------------------------------------------------------
/// -name:  clink.version_commit
/// -ver:   1.1.10
/// -var:   string
/// The commit part of the Clink version number.
/// For v1.2.3.<strong>a0f14d</strong> the commit part is a0f14d.



// BEGIN -- Clink 0.4.8 API compatibility --------------------------------------

extern "C" {
#include "lua.h"
#include <compat/config.h>
#include <readline/rlprivate.h>
}

extern int              get_clink_setting(lua_State* state);
extern int              glob_impl(lua_State* state, bool dirs_only, bool back_compat);
extern int              lua_execute(lua_State* state);

//------------------------------------------------------------------------------
int old_glob_dirs(lua_State* state)
{
    return glob_impl(state, true, true/*back_compat*/);
}

//------------------------------------------------------------------------------
int old_glob_files(lua_State* state)
{
    return glob_impl(state, false, true/*back_compat*/);
}

//------------------------------------------------------------------------------
static int get_setting_str(lua_State* state)
{
    return get_clink_setting(state);
}

//------------------------------------------------------------------------------
static int get_setting_int(lua_State* state)
{
    return get_clink_setting(state);
}

//------------------------------------------------------------------------------
static int get_rl_variable(lua_State* state)
{
    // Check we've got at least one string argument.
    if (lua_gettop(state) == 0 || !lua_isstring(state, 1))
        return 0;

    const char* string = lua_tostring(state, 1);
    const char* rl_cvar = rl_variable_value(string);
    if (rl_cvar == nullptr)
        return 0;

    lua_pushstring(state, rl_cvar);
    return 1;
}

//------------------------------------------------------------------------------
static int is_rl_variable_true(lua_State* state)
{
    int i;
    const char* cvar_value;

    i = get_rl_variable(state);
    if (i == 0)
    {
        return 0;
    }

    cvar_value = lua_tostring(state, -1);
    i = (_stricmp(cvar_value, "on") == 0) || (_stricmp(cvar_value, "1") == 0);
    lua_pop(state, 1);
    lua_pushboolean(state, i);

    return 1;
}

//------------------------------------------------------------------------------
static int get_host_process(lua_State* state)
{
    lua_pushstring(state, rl_readline_name);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.split
/// -deprecated: string.explode
/// -arg:   str:string
/// -arg:   sep:string
/// -ret:   table



// END -- Clink 0.4.8 API compatibility ----------------------------------------



//------------------------------------------------------------------------------
/// -name:  clink.match_display_filter
/// -deprecated: builder:addmatch
/// -var:   function
/// This is no longer used.
/// -show:  clink.match_display_filter = function(matches)
/// -show:  &nbsp; -- Transform matches.
/// -show:  &nbsp; return matches
/// -show:  end

//------------------------------------------------------------------------------
static int map_string(lua_State* state, transform_mode mode)
{
    const char* string;
    int length;

    // Check we've got at least one argument...
    if (lua_gettop(state) == 0)
        return 0;

    // ...and that the argument is a string.
    if (!lua_isstring(state, 1))
        return 0;

    string = lua_tostring(state, 1);
    length = (int)strlen(string);

    wstr<> out;
    if (length)
    {
        wstr<> in(string);
        str_transform(in.c_str(), in.length(), out, mode);
    }

    if (_rl_completion_case_map)
    {
        for (unsigned int i = 0; i < out.length(); ++i)
        {
            if (out[i] == '-' && (mode != transform_mode::upper))
                out.data()[i] = '_';
            else if (out[i] == '_' && (mode == transform_mode::upper))
                out.data()[i] = '-';
        }
    }

    str<> text(out.c_str());

    lua_pushlstring(state, text.c_str(), text.length());

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.lower
/// -ver:   0.4.9
/// -arg:   text:string
/// -ret:   string
/// This API correctly converts UTF8 strings to lowercase, with international
/// linguistic awareness.
/// -show:  clink.lower("Hello World") -- returns "hello world"
static int to_lowercase(lua_State* state)
{
    return map_string(state, transform_mode::lower);
}

//------------------------------------------------------------------------------
/// -name:  clink.upper
/// -ver:   1.1.5
/// -arg:   text:string
/// -ret:   string
/// This API correctly converts UTF8 strings to uppercase, with international
/// linguistic awareness.
/// -show:  clink.upper("Hello World") -- returns "HELLO WORLD"
static int to_uppercase(lua_State* state)
{
    return map_string(state, transform_mode::upper);
}

//------------------------------------------------------------------------------
/// -name:  clink.popuplist
/// -ver:   1.2.17
/// -arg:   title:string
/// -arg:   items:table
/// -arg:   [index:integer]
/// -ret:   string, boolean, integer
/// Displays a popup list and returns the selected item.  May only be used
/// within a <a href="#luakeybindings">luafunc: key binding</a>.
///
/// <span class="arg">title</span> is required and captions the popup list.
///
/// <span class="arg">items</span> is a table of strings to display.
///
/// <span class="arg">index</span> optionally specifies the default item (or 1
/// if omitted).
///
/// The function returns one of the following:
/// <ul>
/// <li>nil if the popup is canceled or an error occurs.
/// <li>Three values:
/// <ul>
/// <li>string indicating the <code>value</code> field from the selected item
/// (or the <code>display</code> field if no value field is present).
/// <li>boolean which is true if the item was selected with <kbd>Shift</kbd> or
/// <kbd>Ctrl</kbd> pressed.
/// <li>integer indicating the index of the selected item in the original
/// <span class="arg">items</span> table.
/// </ul>
/// </ul>
///
/// Alternatively, the <span class="arg">items</span> argument can be a table of
/// tables with the following scheme:
/// -show:  {
/// -show:  &nbsp;   {
/// -show:  &nbsp;       value       = "...",   -- Required; this is returned if the item is chosen.
/// -show:  &nbsp;       display     = "...",   -- Optional; displayed instead of value.
/// -show:  &nbsp;       description = "...",   -- Optional; displayed in a dimmed color in a second column.
/// -show:  &nbsp;   },
/// -show:  &nbsp;   ...
/// -show:  }
///
/// The <code>value</code> field is returned if the item is chosen.
///
/// The optional <code>display</code> field is displayed in the popup list
/// instead of the <code>value</code> field.
///
/// The optional <code>description</code> field is displayed in a dimmed color
/// in a second column.  If it contains tab characters (<code>"\t"</code>) the
/// description string is split into multiple columns (up to 3).
///
/// Starting in v1.3.18, if any description contains a tab character, then the
/// descriptions are automatically aligned in a column.
///
/// Otherwise, the descriptions follow immediately after the display field.
/// They can be aligned in a column by making all of the display fields be the
/// same number of character cells.
static int popup_list(lua_State* state)
{
    if (!lua_state::is_in_luafunc())
        return luaL_error(state, "clink.popuplist may only be used in a " LUA_QL("luafunc:") " key binding");

    enum arg_indices { makevaluesonebased, argTitle, argItems, argIndex};

    const char* title = checkstring(state, argTitle);
    int index = optinteger(state, argIndex, 1) - 1;
    if (!title || !lua_istable(state, argItems))
        return 0;

    int num_items = int(lua_rawlen(state, argItems));
    if (!num_items)
        return 0;

#ifdef DEBUG
    int top = lua_gettop(state);
#endif

    std::vector<autoptr<const char>> items;
    items.reserve(num_items);
    for (int i = 1; i <= num_items; ++i)
    {
        lua_rawgeti(state, argItems, i);

        const char* value = nullptr;
        const char* display = nullptr;
        const char* description = nullptr;

        if (lua_istable(state, -1))
        {
            lua_pushliteral(state, "value");
            lua_rawget(state, -2);
            if (lua_isstring(state, -1))
                value = lua_tostring(state, -1);
            lua_pop(state, 1);

            lua_pushliteral(state, "display");
            lua_rawget(state, -2);
            if (lua_isstring(state, -1))
                display = lua_tostring(state, -1);
            lua_pop(state, 1);

            lua_pushliteral(state, "description");
            lua_rawget(state, -2);
            if (lua_isstring(state, -1))
                description = lua_tostring(state, -1);
            lua_pop(state, 1);
        }
        else
        {
            display = lua_tostring(state, -1);
        }

        if (!value && !display)
            value = display = "";
        else if (!display)
            display = value;
        else if (!value)
            value = display;

        size_t alloc_size = 3; // NUL terminators.
        alloc_size += strlen(value);
        alloc_size += strlen(display);
        if (description) alloc_size += strlen(description);

        str_moveable s;
        s.reserve(alloc_size);

        {
            char* p = s.data();
            append_string_into_buffer(p, value);
            append_string_into_buffer(p, display);
            append_string_into_buffer(p, description, true/*allow_tabs*/);
        }

        items.emplace_back(s.detach());

        lua_pop(state, 1);
    }

#ifdef DEBUG
    assert(lua_gettop(state) == top);
    assert(num_items == items.size());
#endif

    const char* choice;
    if (index > items.size()) index = items.size();
    if (index < 0) index = 0;

    popup_result result;
    if (!g_gui_popups.get())
    {
        popup_results activate_text_list(const char* title, const char** entries, int count, int current, bool has_columns);
        popup_results results = activate_text_list(title, &*items.begin(), int(items.size()), index, true/*has_columns*/);
        result = results.m_result;
        index = results.m_index;
        choice = results.m_text.c_str();
    }
    else
    {
        result = do_popup_list(title, &*items.begin(), items.size(), 0, false, false, false, index, choice, popup_items_mode::display_filter);
    }

    switch (result)
    {
    case popup_result::select:
    case popup_result::use:
        lua_pushstring(state, choice);
        lua_pushboolean(state, (result == popup_result::select));
        lua_pushinteger(state, index + 1);
        return 3;
    }

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.getsession
/// -ver:   1.1.44
/// -ret:   string
/// Returns the current Clink session id.
///
/// This is needed when using
/// <code><span class="hljs-built_in">io</span>.<span class="hljs-built_in">popen</span>()</code>
/// (or similar functions) to invoke <code>clink history</code> or <code>clink
/// info</code> while Clink is installed for autorun.  The popen API spawns a
/// new CMD.exe, which gets a new Clink instance injected, so the history or
/// info command will use the new session unless explicitly directed to use the
/// calling session.
/// -show:  local c = os.getalias("clink")
/// -show:  local r = io.popen(c.." --session "..clink.getsession().." history")
static int get_session(lua_State* state)
{
    str<32> session;
    session.format("%d", GetCurrentProcessId());
    lua_pushlstring(state, session.c_str(), session.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.getansihost
/// -ver:   1.1.48
/// -ret:   string
/// Returns a string indicating who Clink thinks will currently handle ANSI
/// escape codes.  This can change based on the <code>terminal.emulation</code>
/// setting.  This always returns <code>"unknown"</code> until the first edit
/// prompt (see <a href="#clink.onbeginedit">clink.onbeginedit()</a>).
///
/// This can be useful in choosing what kind of ANSI escape codes to use, but it
/// is a best guess and is not necessarily 100% reliable.
///
/// <table>
/// <tr><th>Return</th><th>Description</th></tr>
/// <tr><td>"unknown"</td><td>Clink doesn't know.</td></tr>
/// <tr><td>"clink"</td><td>Clink is emulating ANSI support.  256 color and 24 bit color escape
///     codes are mapped to the nearest of the 16 basic colors.</td></tr>
/// <tr><td>"conemu"</td><td>Clink thinks ANSI escape codes will be handled by ConEmu.</td></tr>
/// <tr><td>"ansicon"</td><td>Clink thinks ANSI escape codes will be handled by ANSICON.</td></tr>
/// <tr><td>"winterminal"</td><td>Clink thinks ANSI escape codes will be handled by Windows
///     Terminal.</td></tr>
/// <tr><td>"winconsole"</td><td>Clink thinks ANSI escape codes will be handled by the default
///     console support in Windows, but Clink detected a terminal replacement that won't support 256
///     color or 24 bit color.</td></tr>
/// <tr><td>"winconsolev2"</td><td>Clink thinks ANSI escape codes will be handled by the default
///     console support in Windows, or it might be handled by a terminal replacement that Clink
///     wasn't able to detect.</td></tr>
/// </table>
static int get_ansi_host(lua_State* state)
{
    static const char* const s_handlers[] =
    {
        "unknown",
        "clink",
        "conemu",
        "ansicon",
        "winterminal",
        "winconsolev2",
        "winconsole",
    };

    static_assert(sizeof_array(s_handlers) == size_t(ansi_handler::max), "must match ansi_handler enum");

    size_t handler = size_t(get_current_ansi_handler());
    lua_pushstring(state, s_handlers[handler]);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.translateslashes
/// -ver:   1.2.7
/// -arg:   [mode:integer]
/// -ret:   integer
/// This overrides how Clink translates slashes in completion matches, which is
/// normally determined by the <code>match.translate_slashes</code> setting.
///
/// This is reset every time match generation is invoked, so use a generator to
/// set this.
///
/// The <span class="arg">mode</span> specifies how to translate slashes when
/// generators add matches:
/// <table>
/// <tr><th>Mode</th><th>Description</th></tr>
/// <tr><td><code>0</code></td><td>No translation.</td></tr>
/// <tr><td><code>1</code></td><td>Translate using the system path separator (backslash on Windows).</td></tr>
/// <tr><td><code>2</code></td><td>Translate to slashes (<code>/</code>).</td></tr>
/// <tr><td><code>3</code></td><td>Translate to backslashes (<code>\</code>).</td></tr>
/// </table>
///
/// If <span class="arg">mode</span> is omitted, then the function returns the
/// current slash translation mode without changing it.
///
/// Note:  Clink always generates file matches using the system path separator
/// (backslash on Windows), regardless what path separator may have been typed
/// as input.  Setting this to <code>0</code> does not disable normalizing typed
/// input paths when invoking completion; it only disables translating slashes
/// in custom generators.
/// -show:  -- This example affects all match generators, by using priority -1 to
/// -show:  -- run first and returning false to let generators continue.
/// -show:  -- To instead affect only one generator, call clink.translateslashes()
/// -show:  -- in its :generate() function and return true.
/// -show:  local force_slashes = clink.generator(-1)
/// -show:  function force_slashes:generate()
/// -show:  &nbsp;   clink.translateslashes(2)  -- Convert to slashes.
/// -show:  &nbsp;   return false               -- Allow generators to continue.
/// -show:  end
static int translate_slashes(lua_State* state)
{
    extern void set_slash_translation(int mode);
    extern int get_slash_translation();

    if (lua_isnoneornil(state, 1))
    {
        lua_pushinteger(state, get_slash_translation());
        return 1;
    }

    bool isnum;
    int mode = checkinteger(state, 1, &isnum);
    if (!isnum)
        return 0;

    if (mode < 0 || mode > 3)
        mode = 1;

    set_slash_translation(mode);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.slash_translation
/// -deprecated: clink.translateslashes
/// -arg:   type:integer
/// Controls how Clink will translate the path separating slashes for the
/// current path being completed. Values for <span class="arg">type</span> are;</br>
/// -1 - no translation</br>
/// 0 - to backslashes</br>
/// 1 - to forward slashes
static int slash_translation(lua_State* state)
{
    if (lua_gettop(state) == 0)
        return 0;

    if (!lua_isnumber(state, 1))
        return 0;

    int mode = int(lua_tointeger(state, 1));
    if (mode < 0)           mode = 0;
    else if (mode == 0)     mode = 3;
    else if (mode == 1)     mode = 2;
    else                    mode = 1;

    extern void set_slash_translation(int mode);
    set_slash_translation(mode);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.reload
/// -ver:   1.2.29
/// Reloads Lua scripts and Readline config file at the next prompt.
static int reload(lua_State* state)
{
    force_reload_scripts();
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.reclassifyline
/// -ver:   1.3.9
/// Reclassify the input line text again and refresh the input line display.
static int reclassify_line(lua_State* state)
{
    const bool ismain = (G(state)->mainthread == state);
    if (ismain)
        host_reclassify(reclassify_reason::force);
    else
        lua_input_idle::signal_reclassify();
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.refilterprompt
/// -ver:   1.2.46
/// Invoke the prompt filters again and refresh the prompt.
///
/// Note: this can potentially be expensive; call this only infrequently.
int g_prompt_refilter = 0;
static int refilter_prompt(lua_State* state)
{
    g_prompt_refilter++;
    void host_filter_prompt();
    host_filter_prompt();
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int g_prompt_redisplay = 0;
static int get_refilter_redisplay_count(lua_State* state)
{
    lua_pushinteger(state, g_prompt_refilter);
    lua_pushinteger(state, g_prompt_redisplay);
    return 2;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int is_transient_prompt_filter(lua_State* state)
{
    lua_pushboolean(state, prompt_filter::is_filtering());
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int history_suggester(lua_State* state)
{
    const char* line = checkstring(state, 1);
    const int match_prev_cmd = lua_toboolean(state, 2);
    if (!line)
        return 0;

    HIST_ENTRY** history = history_list();
    if (!history || history_length <= 0)
        return 0;

    // 'match_prev_cmd' only works when 'history.dupe_mode' is 'add'.
    if (match_prev_cmd && g_dupe_mode.get() != 0)
        return 0;

    int scanned = 0;
    const DWORD tick = GetTickCount();

    const int scan_min = 200;
    const DWORD ms_max = 50;

    const char* prev_cmd = (match_prev_cmd && history_length > 0) ? history[history_length - 1]->line : nullptr;
    for (int i = history_length; --i >= 0;)
    {
        // Search at least SCAN_MIN entries.  But after that don't keep going
        // unless it's been less than MS_MAX milliseconds.
        if (scanned >= scan_min && !(scanned % 20) && GetTickCount() - tick >= ms_max)
            break;
        scanned++;

        str_iter lhs(line);
        str_iter rhs(history[i]->line);
        int matchlen = str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(lhs, rhs);

        // lhs isn't exhausted, or rhs is exhausted?  Continue searching.
        if (lhs.more() || !rhs.more())
            continue;

        // Zero matching length?  Is ok with 'match_prev_cmd', otherwise
        // continue searching.
        if (!matchlen && !match_prev_cmd)
            continue;

        // Match previous command, if needed.
        if (match_prev_cmd)
        {
            if (i <= 0 || str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(prev_cmd, history[i - 1]->line) != -1)
                continue;
        }

        // Suggest this history entry.
        lua_pushstring(state, history[i]->line);
        lua_pushinteger(state, 1);
        return 2;
    }

    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int set_suggestion_result(lua_State* state)
{
    bool isnum;
    const char* line = checkstring(state, -4);
    int endword_offset = checkinteger(state, -3, &isnum) - 1;
    if (!line || !isnum)
        return 0;

    const int line_len = strlen(line);
    if (endword_offset < 0 || endword_offset > line_len)
        return 0;

    const char* suggestion = optstring(state, -2, nullptr);
    int offset = optinteger(state, -1, 0, &isnum) - 1;
    if (!isnum || offset < 0 || offset > line_len)
        offset = line_len;

    set_suggestion(line, endword_offset, suggestion, offset);
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int kick_idle(lua_State* state)
{
    extern void kick_idle();
    kick_idle();
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int matches_ready(lua_State* state)
{
    bool isnum;
    int id = checkinteger(state, 1, &isnum);
    if (!isnum)
        return 0;

    extern bool notify_matches_ready(int generation_id);
    lua_pushboolean(state, notify_matches_ready(id));
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int recognize_command(lua_State* state)
{
    const char* line = checkstring(state, 1);
    const char* word = checkstring(state, 2);
    const bool quoted = lua_toboolean(state, 3);
    if (!line || !word)
        return 0;
    if (!*line || !*word)
        return 0;

    bool ready;
    const recognition recognized = recognize_command(line, word, quoted, ready, nullptr/*file*/);
    lua_pushinteger(state, int(recognized));
    return 1;
}

//------------------------------------------------------------------------------
static int generate_from_history(lua_State* state)
{
    HIST_ENTRY** list = history_list();
    if (!list)
        return 0;

    cmd_command_tokeniser command_tokeniser;
    cmd_word_tokeniser word_tokeniser;
    word_collector collector(&command_tokeniser, &word_tokeniser);
    collector.init_alias_cache();

    save_stack_top ss(state);

    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_generate_from_historyline");
    lua_rawget(state, -2);

    while (*list)
    {
        const char* buffer = (*list)->line;
        unsigned int len = static_cast<unsigned int>(strlen(buffer));

        // Collect one line_state for each command in the line.
        std::vector<word> words;
        commands commands;
        collector.collect_words(buffer, len, len/*cursor*/, words, collect_words_mode::whole_command);
        commands.set(buffer, len, 0, words);

        for (const line_state& line : commands.get_linestates(buffer, len))
        {
            // clink._generate_from_historyline
            lua_pushvalue(state, -1);

            // line_state
            line_state_lua line_lua(line);
            line_lua.push(state);

            if (lua_state::pcall(state, 1, 0) != 0)
                break;
        }

        list++;
    }

    return 0;
}

//------------------------------------------------------------------------------
static int api_reset_generate_matches(lua_State* state)
{
    extern void reset_generate_matches();
    reset_generate_matches();
    return 0;
}

//------------------------------------------------------------------------------
static int mark_deprecated_argmatcher(lua_State* state)
{
    const char* name = checkstring(state, 1);
    if (name)
        host_mark_deprecated_argmatcher(name);
    return 0;
}

//------------------------------------------------------------------------------
static int signal_delayed_init(lua_State* state)
{
    lua_input_idle::signal_delayed_init();
    return 0;
}

//------------------------------------------------------------------------------
static int is_cmd_command(lua_State* state)
{
    const char* word = checkstring(state, 1);
    if (!word)
        return 0;

    lua_pushboolean(state, is_cmd_command(word));
    return 1;
}

//------------------------------------------------------------------------------
static int get_installation_type(lua_State* state)
{
    // Open the Uninstall key.

    HKEY hkey;
    wstr<> where(c_uninstall_key);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, where.c_str(), 0, MAXIMUM_ALLOWED, &hkey))
    {
failed:
        lua_pushliteral(state, "zip");
        return 1;
    }

    // Get binaries path.

    WCHAR long_bin_dir[MAX_PATH * 2];
    {
        str<> tmp;
        if (!os::get_env("=clink.bin", tmp))
            goto failed;

        wstr<> bin_dir(tmp.c_str());
        DWORD len = GetLongPathNameW(bin_dir.c_str(), long_bin_dir, sizeof_array(long_bin_dir));
        if (!len || len >= sizeof_array(long_bin_dir))
            goto failed;

        long_bin_dir[len] = '\0';
    }

    // Enumerate installed programs.

    bool found = false;
    WCHAR install_key[MAX_PATH];
    install_key[0] = '\0';

    for (DWORD index = 0; true; ++index)
    {
        DWORD size = sizeof_array(install_key); // Characters, not bytes, for RegEnumKeyExW.
        if (ERROR_NO_MORE_ITEMS == RegEnumKeyExW(hkey, index, install_key, &size, 0, nullptr, nullptr, nullptr))
            break;

        if (size >= sizeof_array(install_key))
            size = sizeof_array(install_key) - 1;
        install_key[size] = '\0';

        // Ignore if not a Clink installation.
        if (_wcsnicmp(install_key, L"clink_", 6))
            continue;

        HKEY hsubkey;
        if (RegOpenKeyExW(hkey, install_key, 0, MAXIMUM_ALLOWED, &hsubkey))
            continue;

        DWORD type;
        WCHAR location[280];
        DWORD len = sizeof(location); // Bytes, not characters, for RegQueryValueExW.
        LSTATUS status = RegQueryValueExW(hsubkey, L"InstallLocation", NULL, &type, LPBYTE(&location), &len);
        RegCloseKey(hsubkey);

        if (status)
            continue;

        len = len / 2;
        if (len >= sizeof_array(location))
            continue;
        location[len] = '\0';

        // If the uninstall location matches the current binaries directory,
        // then this is a match.
        WCHAR long_location[MAX_PATH * 2];
        len = GetLongPathNameW(location, long_location, sizeof_array(long_location));
        if (len && len < sizeof_array(long_location) && !_wcsicmp(long_bin_dir, long_location))
        {
            found = true;
            break;
        }
    }

    RegCloseKey(hkey);

    if (!found)
        goto failed;

    str<> tmp(install_key);
    lua_pushliteral(state, "exe");
    lua_pushstring(state, tmp.c_str());
    return 2;
}

//------------------------------------------------------------------------------
static int set_install_version(lua_State* state)
{
    const char* key = checkstring(state, 1);
    const char* ver = checkstring(state, 2);
    if (!key || !ver || _strnicmp(key, "clink_", 6))
        return 0;

    if (ver[0] == 'v')
        ver++;

    wstr<> where(c_uninstall_key);
    wstr<> wkey(key);
    where << L"\\" << wkey.c_str();

    HKEY hkey;
    LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, where.c_str(), 0, MAXIMUM_ALLOWED, &hkey);
    if (status)
        return 0;

    wstr<> name;
    wstr<> version(ver);
    name << L"Clink v" << version.c_str();

    bool ok = true;
    ok = ok && !RegSetValueExW(hkey, L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE*>(name.c_str()), (name.length() + 1) * sizeof(*name.c_str()));
    ok = ok && !RegSetValueExW(hkey, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE*>(version.c_str()), (version.length() + 1) * sizeof(*version.c_str()));
    RegCloseKey(hkey);

    if (!ok)
        return 0;

    lua_pushboolean(state, true);
    return 1;
}

//------------------------------------------------------------------------------
#ifdef TRACK_LOADED_LUA_FILES
static int clink_is_lua_file_loaded(lua_State* state)
{
    const char* filename = checkstring(state, 1);
    if (!filename)
        return 0;

    int loaded = is_lua_file_loaded(state, filename);
    lua_pushboolean(state, loaded);
    return 1;
}
#endif



//------------------------------------------------------------------------------
extern int set_current_dir(lua_State* state);
extern int get_aliases(lua_State* state);
extern int get_current_dir(lua_State* state);
extern int get_env(lua_State* state);
extern int get_env_names(lua_State* state);
extern int get_screen_info(lua_State* state);
extern int is_dir(lua_State* state);
extern int explode(lua_State* state);

//------------------------------------------------------------------------------
void clink_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        // APIs in the "clink." namespace.
        { "lower",                  &to_lowercase },
        { "print",                  &clink_print },
        { "upper",                  &to_uppercase },
        { "popuplist",              &popup_list },
        { "getsession",             &get_session },
        { "getansihost",            &get_ansi_host },
        { "translateslashes",       &translate_slashes },
        { "reload",                 &reload },
        { "reclassifyline",         &reclassify_line },
        { "refilterprompt",         &refilter_prompt },
        // Backward compatibility with the Clink 0.4.8 API.  Clink 1.0.0a1 had
        // moved these APIs away from "clink.", but backward compatibility
        // requires them here as well.
        { "chdir",                  &set_current_dir },
        { "execute",                &lua_execute },
        { "find_dirs",              &old_glob_dirs },
        { "find_files",             &old_glob_files },
        { "get_console_aliases",    &get_aliases },
        { "get_cwd",                &get_current_dir },
        { "get_env",                &get_env },
        { "get_env_var_names",      &get_env_names },
        { "get_host_process",       &get_host_process },
        { "get_rl_variable",        &get_rl_variable },
        { "get_screen_info",        &get_screen_info },
        { "get_setting_int",        &get_setting_int },
        { "get_setting_str",        &get_setting_str },
        { "is_dir",                 &is_dir },
        { "is_rl_variable_true",    &is_rl_variable_true },
        { "slash_translation",      &slash_translation },
        { "split",                  &explode },
        // UNDOCUMENTED; internal use only.
        { "istransientpromptfilter", &is_transient_prompt_filter },
        { "get_refilter_redisplay_count", &get_refilter_redisplay_count },
        { "history_suggester",      &history_suggester },
        { "set_suggestion_result",  &set_suggestion_result },
        { "kick_idle",              &kick_idle },
        { "matches_ready",          &matches_ready },
        { "_recognize_command",     &recognize_command },
        { "_generate_from_history", &generate_from_history },
        { "_reset_generate_matches", &api_reset_generate_matches },
        { "_mark_deprecated_argmatcher", &mark_deprecated_argmatcher },
        { "_signal_delayed_init",   &signal_delayed_init },
        { "is_cmd_command",         &is_cmd_command },
        { "_get_installation_type", &get_installation_type },
        { "_set_install_version",   &set_install_version },
#ifdef TRACK_LOADED_LUA_FILES
        { "is_lua_file_loaded",     &clink_is_lua_file_loaded },
#endif
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_pushliteral(state, "version_encoded");
    lua_pushinteger(state, CLINK_VERSION_MAJOR * 10000000 +
                           CLINK_VERSION_MINOR *    10000 +
                           CLINK_VERSION_PATCH);
    lua_rawset(state, -3);

    lua_pushliteral(state, "version_major");
    lua_pushinteger(state, CLINK_VERSION_MAJOR);
    lua_rawset(state, -3);

    lua_pushliteral(state, "version_minor");
    lua_pushinteger(state, CLINK_VERSION_MINOR);
    lua_rawset(state, -3);

    lua_pushliteral(state, "version_patch");
    lua_pushinteger(state, CLINK_VERSION_PATCH);
    lua_rawset(state, -3);

    lua_pushliteral(state, "version_commit");
    lua_pushstring(state, AS_STR(CLINK_COMMIT));
    lua_rawset(state, -3);


#ifdef DEBUG
    lua_pushliteral(state, "DEBUG");
    lua_pushboolean(state, true);
    lua_rawset(state, -3);
#endif

    lua_setglobal(state, "clink");
}
