// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "intercept.h"
#include "reclassify.h"
#include "recognizer.h"

#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/str_tokeniser.h>
#include <core/str_unordered_set.h>
#include <core/settings.h>
#include <core/linear_allocator.h>
#include <core/debugheap.h>

#include <memory>
#include <thread>
#include <mutex>
#include <shlwapi.h>

extern "C" {
#include <readline/readline.h>
}

//------------------------------------------------------------------------------
extern setting_color g_color_unrecognized;
extern setting_color g_color_executable;

//------------------------------------------------------------------------------
static bool search_for_extension(str_base& full, const char* word, str_base& out)
{
    path::append(full, "");
    const uint32 trunc = full.length();

    str<> pathext;
    if (!os::get_env("pathext", pathext))
        return false;

    str_tokeniser tokens(pathext.c_str(), ";");
    const char *start;
    int32 length;

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

        full.truncate(trunc);
        path::append(full, word);
        full.concat(start, length);
        if (os::get_path_type(full.c_str()) == os::path_type_file)
        {
            out = full.c_str();
            return true;
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
static bool s_immediate = false;
void set_noasync_recognizer()
{
    s_immediate = true;
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
    int32                   find(const char* key, recognition& cached, str_base* file) const;
    bool                    enqueue(const char* key, const char* word, const char* cwd, recognition* cached=nullptr);
    bool                    need_refresh();
    void                    end_line();

    void                    wait_while_busy();

private:
    bool                    usable() const;
    bool                    busy() const;
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
int32 recognizer::find(const char* key, recognition& cached, str_base* file) const
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
    if (!key || !*key || !word || !*word)
    {
        assert(false);
        return false;
    }

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
void recognizer::wait_while_busy()
{
    if (usable())
    {
        while (busy() && WaitForSingleObject(s_ready_event, INFINITE) == WAIT_OBJECT_0)
        {}
    }
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
        processing = busy() && !m_zombie;
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
        const int32 c_brief_wait = 1250; // 1.25 seconds
        while (true)
        {
            const volatile DWORD tick_now = GetTickCount();
            const int32 timeout = int32(tick_begin) + c_brief_wait - int32(tick_now);
            if (timeout < 0)
                break;

            if (WaitForSingleObject(ready_event, DWORD(timeout)) != WAIT_OBJECT_0)
                break;

            reclassify(reclassify_reason::recognizer);

            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (!busy() || !usable())
                break;
        }
    }

    reclassify(reclassify_reason::recognizer);
}

//------------------------------------------------------------------------------
bool recognizer::usable() const
{
    return !m_zombie && s_ready_event;
}

//------------------------------------------------------------------------------
bool recognizer::busy() const
{
    return m_processing || !m_pending.empty();
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
}



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
recognition recognize_command(const char* line, const char* word, bool quoted, bool& ready, str_base* file)
{
    assert(word);

    ready = true;

    str<> tmp;
    if (!quoted)
    {
        bool caret = false;
        str_iter iter(word);
        while (iter.more())
        {
            const char* ptr = iter.get_pointer();
            const int32 c = iter.next();
            if (!caret && c == '^' && iter.peek())
            {
                caret = true;
            }
            else
            {
                tmp.concat(ptr, int32(iter.get_pointer() - ptr));
                caret = false;
            }
        }
        word = tmp.c_str();
    }

    str<> tmp2;
    if (os::expand_env(word, -1, tmp2))
        word = tmp2.c_str();

    if (!*word)
        return recognition::unknown;

    // Ignore device names.
    if (path::is_device(word))
        return recognition::unknown;

    // Ignore UNC paths, because they can take up to 2 minutes to time out.
    // Even running that on a thread would either starve the consumers or
    // accumulate threads faster than they can finish.
    if (path::is_unc(word))
        return recognition::unknown;

    // Check for directory intercepts (-, ..., ...., dir\, and so on).
    if (line && *line && intercept_directory(line) != intercept_result::none)
        return recognition::navigate;

    // Check for drive letter.
    if (word[0] && word[1] == ':' && !word[2])
    {
        int32 type = os::get_drive_type(word);
        if (type > os::drive_type_invalid)
            return recognition::navigate;
    }

    // Check for cached result.
    recognition cached;
    const int32 found = s_recognizer.find(word, cached, file);
    if (found)
    {
        ready = (found > 0);
        return cached;
    }

    // Expand environment variables.
    str<32> expanded;
    const char* orig_word = word;
    uint32 len = uint32(strlen(word));
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
    if (s_immediate)
    {
        s_recognizer.wait_while_busy();
        ready = (s_recognizer.find(orig_word, cached, file) > 0);
    }

    return cached;
}
