// Copyright (c) 2017 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "history_db.h"

#include <core/base.h>
#include <core/globber.h>
#include <core/os.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <core/str_map.h>
#include <core/auto_free_str.h>
#include <core/path.h>
#include <core/log.h>
#include <assert.h>

#include <new>
extern "C" {
#include <readline/readline.h>  // Needed by rlprivate.h.
#include <readline/rldefs.h>    // Needed by rlmbutil.h in rlprivate.h.
#include <readline/rlprivate.h> // Needed for _rl_free_undo_list().
#include <readline/history.h>
#include <readline/histlib.h>   // Depends on config.h.
}

#include <algorithm>
#include <memory>
#include <unordered_set>

#include <core/debugheap.h>

//------------------------------------------------------------------------------
static setting_bool g_shared(
    "history.shared",
    "Share history between instances",
    "",
    false);

namespace use_get_max_history_instead {
static setting_int g_max_history(
    "history.max_lines",
    "The number of history lines to save",
    "The number of history lines to save, or 0 for unlimited.\n"
    "\n"
    "Warning:  The larger the history file becomes, the longer it takes to reload\n"
    "at each prompt.  If it starts taking too long, then lower this setting.  Or\n"
    "you can use 'clink history compact <num_lines>' to force shrinking the\n"
    "history file to a smaller size.",
    10000);
};

static setting_bool g_ignore_space(
    "history.ignore_space",
    "Skip adding lines prefixed with whitespace",
    "Ignore lines that begin with whitespace when adding lines in to\n"
    "the history.",
    true);

setting_enum g_dupe_mode(
    "history.dupe_mode",
    "Controls how duplicate entries are handled",
    "If a line is a duplicate of an existing history entry Clink will erase\n"
    "the duplicate when this is set to 'erase_prev'.  A value of 'ignore' will\n"
    "not add a line to the history if it already exists, and a value of 'add'\n"
    "will always add lines.\n"
    "Note that history is not deduplicated when reading/writing to disk.",
    "add,ignore,erase_prev",
    2);

setting_enum g_expand_mode(
    "history.expand_mode",
    "Sets how command history expansion is applied",
    "The '!' character in an entered line can be interpreted to introduce\n"
    "words from the history.  That can be enabled and disable by setting this\n"
    "value to 'on' or 'off'.  Or set this to 'not_squoted', 'not_dquoted', or\n"
    "'not_quoted' to skip any '!' character in single, double, or both quotes\n"
    "respectively.",
    "off,on,not_squoted,not_dquoted,not_quoted",
    4);

setting_enum g_history_timestamp(
    "history.time_stamp",
    "History item timestamps",
    "The default is 'off'.  When set to 'save', timestamps are saved for each\n"
    "history item but are only shown in the 'history' command when the\n"
    "'--show-time' flag is used.  When set to 'show', timestamps are saved and\n"
    "are shown in 'history' unless the '--bare' or '--no-show-time' flag is used.",
    "off,save,show",
    0);

static constexpr int32 c_max_max_history_lines = 999999;
static int32 get_max_history()
{
    int32 limit = use_get_max_history_instead::g_max_history.get();
    if (limit <= 0 || limit > c_max_max_history_lines)
        limit = c_max_max_history_lines;
    return limit;
}



//------------------------------------------------------------------------------
static int32 history_expand_control(char* line, int32 marker_pos)
{
    int32 setting, in_quote, i;

    setting = g_expand_mode.get();
    if (setting <= 1)
    {
        // Shouldn't even reach here when history.expand_mode is off.  Only
        // certain history expansion syntax calls this -- for example '^' does
        // not call this, so this is too late to short circuit out of history
        // expansion.
        assert(setting > 0);
        return (setting <= 0);
    }

    // Is marker_pos inside a quote of some kind?
    in_quote = 0;
    for (i = 0; i < marker_pos && *line; ++i, ++line)
    {
        int32 c = *line;
        if (c == '\'' || c == '\"')
            in_quote = (c == in_quote) ? 0 : c;
    }

    switch (setting)
    {
    case 2: return (in_quote == '\'');
    case 3: return (in_quote == '\"');
    case 4: return (in_quote == '\"' || in_quote == '\'');
    }

    return 0;
}

//------------------------------------------------------------------------------
static void* open_file(const char* path, bool if_exists=false)
{
    wstr<> wpath(path);

    DWORD share_flags = FILE_SHARE_READ|FILE_SHARE_WRITE;
    void* handle = CreateFileW(wpath.c_str(), GENERIC_READ|GENERIC_WRITE, share_flags,
        nullptr, if_exists ? OPEN_EXISTING : OPEN_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

    return (handle == INVALID_HANDLE_VALUE) ? nullptr : handle;
}

//------------------------------------------------------------------------------
static void* open_file(const char* path, DWORD& err, bool if_exists=false)
{
    void* handle = open_file(path, if_exists);
    err = handle ? NOERROR : GetLastError();
    return handle;
}

//------------------------------------------------------------------------------
static void* make_removals_file(const char* path, const char* ctag)
{
    void* handle = open_file(path);
    if (handle)
    {
        DWORD written;
        DWORD len = DWORD(strlen(ctag));
        WriteFile(handle, ctag, len, &written, nullptr);
        WriteFile(handle, "\n", 1, &written, nullptr);
    }
    return handle;
}



//------------------------------------------------------------------------------
const int32 max_ctag_size = 6 + 10 + 1 + 10 + 1 + 10 + 1 + 10 + 1 + 1;
void concurrency_tag::generate_new_tag()
{
    static uint32 disambiguate = 0;

    assert(m_tag.empty());
    time_t now = time(nullptr);
    unsigned now32 = unsigned(now);
    m_tag.format("|CTAG_%u_%u_%u_%u", now32, GetTickCount(), GetProcessId(GetCurrentProcess()), disambiguate++);
}

//------------------------------------------------------------------------------
void concurrency_tag::set(const char* tag)
{
    assert(m_tag.empty());
    m_tag = tag;
}



//------------------------------------------------------------------------------
union line_id_impl
{
    explicit  line_id_impl()                { outer = 0; }
    explicit  line_id_impl(uint32 o)        { offset = o; bank_index = 0; active = 1; }
    explicit  operator bool () const        { return !!outer; }
    operator history_db::line_id () const   { return outer; }
    struct {
        uint32          offset : 29;
        uint32          bank_index : 2;
        uint32          active : 1;
    };
    history_db::line_id outer;
};

static const line_id_impl c_max_line_id(uint32(-1));



//------------------------------------------------------------------------------
bank_handles::operator bool () const
{
    return (m_handle_lines != nullptr);
}

//------------------------------------------------------------------------------
void bank_handles::close()
{
    if (m_handle_removals)
    {
        CloseHandle(m_handle_removals);
        m_handle_removals = nullptr;
    }
    if (m_handle_lines)
    {
        CloseHandle(m_handle_lines);
        m_handle_lines = nullptr;
    }
}



//------------------------------------------------------------------------------
class bank_lock
    : public no_copy
{
public:
    explicit        operator bool () const;

protected:
                    bank_lock() = default;
                    bank_lock(const bank_handles& handles, bool exclusive);
                    bank_lock(bank_lock&& other);
                    ~bank_lock();
    bank_lock&      operator = (bank_lock&& other);
    void*           m_handle_lines = nullptr;       // From bank_master or bank_session.
    void*           m_handle_removals = nullptr;    // Always from bank_session, or nullptr.
};

//------------------------------------------------------------------------------
bank_lock::bank_lock(const bank_handles& handles, bool exclusive)
: m_handle_lines(handles.m_handle_lines)
, m_handle_removals(handles.m_handle_removals)
{
    if (m_handle_lines == nullptr)
        return;

    // WARNING: ALWAYS LOCK MASTER BEFORE SESSION!
    //
    // Because m_handle_lines and m_handle_removals can be from different banks,
    // there is a potential deadlock if the nested lock order of bank_master vs
    // bank_session are not always the same order.

    OVERLAPPED overlapped = {};
    int32 flags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
    LockFileEx(m_handle_lines, flags, 0, ~0u, ~0u, &overlapped);
    if (m_handle_removals)
        LockFileEx(m_handle_removals, flags, 0, ~0u, ~0u, &overlapped);
}

//------------------------------------------------------------------------------
bank_lock::bank_lock(bank_lock&& other)
{
    *this = std::move(other);
}

//------------------------------------------------------------------------------
bank_lock::~bank_lock()
{
    if (m_handle_lines != nullptr)
    {
        OVERLAPPED overlapped = {};
        if (m_handle_removals)
            UnlockFileEx(m_handle_removals, 0, ~0u, ~0u, &overlapped);
        UnlockFileEx(m_handle_lines, 0, ~0u, ~0u, &overlapped);
    }
}

//------------------------------------------------------------------------------
bank_lock& bank_lock::operator = (bank_lock&& other)
{
    m_handle_lines = other.m_handle_lines;
    m_handle_removals = other.m_handle_removals;
    other.m_handle_lines = nullptr;
    other.m_handle_removals = nullptr;
    return *this;
}

//------------------------------------------------------------------------------
bank_lock::operator bool () const
{
    return (m_handle_lines != nullptr);
}



//------------------------------------------------------------------------------
class write_lock;

//------------------------------------------------------------------------------
class read_lock
    : public bank_lock
{
public:
    class file_iter : public no_copy
    {
    public:
                            file_iter() = default;
                            file_iter(const read_lock& lock, char* buffer, int32 buffer_size);
                            file_iter(void* handle, char* buffer, int32 buffer_size);
        template <int32 S>  file_iter(const read_lock& lock, char (&buffer)[S]);
        template <int32 S>  file_iter(void* handle, char (&buffer)[S]);
        uint32              next(uint32 rollback=0);
        unsigned __int64    get_buffer_offset() const   { return m_buffer_offset; }
        char*               get_buffer() const          { return m_buffer; }
        uint32              get_buffer_size() const     { return m_buffer_size; }
        uint32              get_remaining() const       { return m_remaining; }
        void                set_file_offset(uint32 offset);

    private:
        char*               m_buffer = nullptr;
        void*               m_handle = nullptr;
        unsigned __int64    m_buffer_offset = 0;
        uint32              m_buffer_size = 0;
        uint32              m_remaining = 0;
    };

    class line_iter : public no_copy
    {
    public:
                            line_iter() = default;
                            line_iter(const read_lock& lock, char* buffer, int32 buffer_size);
                            line_iter(void* handle, char* buffer, int32 buffer_size);
        template <int32 S>  line_iter(const read_lock& lock, char (&buffer)[S]);
        template <int32 S>  line_iter(void* handle, char (&buffer)[S]);
                            ~line_iter() = default;
        line_id_impl        next(str_iter& out, str_base* timestamp=nullptr, history_db::line_id* timestamp_id=nullptr);
        void                set_file_offset(uint32 offset);
        uint32              get_deleted_count() const { return m_deleted; }

    private:
        bool                provision();
        file_iter           m_file_iter;
        uint32              m_remaining = 0;
        uint32              m_deleted = 0;
        bool                m_first_line = true;
        bool                m_eating_ctag = false;
        std::unordered_set<uint32> m_removals;
    };

    explicit                read_lock() = default;
    explicit                read_lock(const bank_handles& handles, bool exclusive=false);
    line_id_impl            find(const char* line) const;
    template <class T> void find(const char* line, T&& callback) const;
    int32                   apply_removals(write_lock& lock) const;
    int32                   collect_removals(write_lock& lock, std::vector<line_id_impl>& removals) const;

private:
    template <typename T> int32 for_each_removal(const read_lock& target, T&& callback) const;
};

//------------------------------------------------------------------------------
class write_lock
    : public read_lock
{
public:
                    write_lock() = default;
    explicit        write_lock(const bank_handles& handles);
    void            clear();
    line_id_impl    add(const char* line);
    bool            remove(line_id_impl id);
    void            append(const read_lock& src);
};

//------------------------------------------------------------------------------
static bool extract_ctag(read_lock::file_iter& iter, char* buffer, int32 buffer_size, concurrency_tag& tag);
static bool extract_ctag(const read_lock& lock, concurrency_tag& tag);



//------------------------------------------------------------------------------
read_lock::read_lock(const bank_handles& handles, bool exclusive)
: bank_lock(handles, exclusive)
{
}

//------------------------------------------------------------------------------
template <class T> void read_lock::find(const char* line, T&& callback) const
{
    history_read_buffer buffer;
    line_iter iter(*this, buffer.data(), buffer.size());

    line_id_impl id;
    for (str_iter read; id = iter.next(read);)
    {
        if (strncmp(line, read.get_pointer(), read.length()) != 0)
            continue;

        if (line[read.length()] != '\0')
            continue;

        uint32 file_ptr = SetFilePointer(m_handle_lines, 0, nullptr, FILE_CURRENT);
        bool more = callback(id);
        SetFilePointer(m_handle_lines, file_ptr, nullptr, FILE_BEGIN);

        if (!more)
            break;
    }
}

//------------------------------------------------------------------------------
line_id_impl read_lock::find(const char* line) const
{
    line_id_impl id;
    find(line, [&] (line_id_impl inner_id) {
        id = inner_id;
        return false;
    });
    return id;
}

//------------------------------------------------------------------------------
int32 read_lock::apply_removals(write_lock& lock) const
{
    return for_each_removal(lock, [&] (uint32 offset)
    {
        line_id_impl id(offset);
        lock.remove(id);
    });
}

//------------------------------------------------------------------------------
int32 read_lock::collect_removals(write_lock& lock, std::vector<line_id_impl>& removals) const
{
    return for_each_removal(lock, [&] (uint32 offset)
    {
        removals.emplace_back(offset);
    });
}

//------------------------------------------------------------------------------
template <typename T> int32 read_lock::for_each_removal(const read_lock& target, T&& callback) const
{
    if (!m_handle_removals)
        return 0;

    assert(m_handle_lines);
    char tmp[512];

    // Verify ctags match; don't continue otherwise!
    {
        bank_handles verify_handles;
        verify_handles.m_handle_lines = target.m_handle_lines;
        verify_handles.m_handle_removals = this->m_handle_removals;

        uint32 lines_ptr = SetFilePointer(verify_handles.m_handle_lines, 0, nullptr, FILE_CURRENT);
        uint32 removals_ptr = SetFilePointer(verify_handles.m_handle_removals, 0, nullptr, FILE_CURRENT);

#ifdef DEBUG
        {
            char sz[MAX_PATH];
            DWORD path_len = GetFinalPathNameByHandle(verify_handles.m_handle_lines, sz, sizeof_array(sz), 0);
            assert(path_len);
            const char* name = path::get_name(sz);
            const int32 is_master_history = (strnicmp(name, "clink_history", 13) == 0 && name[13] != '_');
            if (!is_master_history)
            {
                LOG("m_handle_lines is for '%s'; expected master history instead!", sz);
                assert(is_master_history);
            }
        }
#endif

        concurrency_tag master_ctag;
        file_iter iter_lines(verify_handles.m_handle_lines, tmp);
        extract_ctag(iter_lines, tmp, int32(sizeof(tmp)), master_ctag);

        concurrency_tag removals_ctag;
        file_iter iter_removals(verify_handles.m_handle_removals, tmp);
        extract_ctag(iter_removals, tmp, int32(sizeof(tmp)), removals_ctag);

        SetFilePointer(verify_handles.m_handle_lines, lines_ptr, nullptr, FILE_BEGIN);
        SetFilePointer(verify_handles.m_handle_removals, removals_ptr, nullptr, FILE_BEGIN);

        if (strcmp(master_ctag.get(), removals_ctag.get()) != 0)
        {
            LOG("can't apply removals; ctag mismatch: required ctag: %s, removals ctag: %s", master_ctag.get(), removals_ctag.get());
            return -1;
        }
    }

    // Read removal offsets; call the specified callback for each offset.
    str_iter value;
    line_iter iter(m_handle_removals, tmp);
    while (iter.next(value))
    {
        unsigned __int64 offset = 0;
        uint32 len = value.length();
        for (const char *s = value.get_pointer(); len--; s++)
        {
            if (*s < '0' || *s > '9')
                break;
            offset *= 10;
            offset += *s - '0';
        }

        if (offset >= c_max_line_id.offset)
        {
            // Should be unreachable because too-large ids should not have
            // gotten in the removals file in the first place.
            LOG("removal offset %zu is too large", offset);
            assert(false);
        }
        else if (offset > 0)
        {
            callback(uint32(offset));
        }
    }

    return 1;
}



//------------------------------------------------------------------------------
template <int32 S> read_lock::file_iter::file_iter(const read_lock& lock, char (&buffer)[S])
: file_iter(lock.m_handle_lines, buffer, S)
{
}

//------------------------------------------------------------------------------
template <int32 S> read_lock::file_iter::file_iter(void* handle, char (&buffer)[S])
: file_iter(handle, buffer, S)
{
}

//------------------------------------------------------------------------------
read_lock::file_iter::file_iter(const read_lock& lock, char* buffer, int32 buffer_size)
: m_buffer(buffer)
, m_handle(lock.m_handle_lines)
, m_buffer_size(buffer_size)
{
    set_file_offset(0);
}

//------------------------------------------------------------------------------
read_lock::file_iter::file_iter(void* handle, char* buffer, int32 buffer_size)
: m_buffer(buffer)
, m_handle(handle)
, m_buffer_size(buffer_size)
{
    set_file_offset(0);
}

//------------------------------------------------------------------------------
uint32 read_lock::file_iter::next(uint32 rollback)
{
    if (!m_remaining)
    {
        if (m_buffer)
            m_buffer[0] = '\0';
        return 0;
    }

    rollback = min<unsigned>(rollback, m_buffer_size);
    if (rollback)
        memmove(m_buffer, m_buffer + m_buffer_size - rollback, rollback);

    m_buffer_offset += m_buffer_size - rollback;

    char* target = m_buffer + rollback;
    int32 needed = min(m_remaining, m_buffer_size - rollback);

    DWORD read = 0;
    ReadFile(m_handle, target, needed, &read, nullptr);

    m_remaining -= read;
    m_buffer_size = read + rollback;
    return m_buffer_size;
}

//------------------------------------------------------------------------------
void read_lock::file_iter::set_file_offset(uint32 offset)
{
    m_remaining = GetFileSize(m_handle, nullptr);
    offset = clamp(offset, (uint32)0, m_remaining);
    m_remaining -= offset;
    // BUGBUG: Should this be `offset - m_buffer_offset`?
    m_buffer_offset = static_cast<unsigned __int64>(0) - m_buffer_size;
    SetFilePointer(m_handle, offset, nullptr, FILE_BEGIN);
    m_buffer[0] = '\0';
}



//------------------------------------------------------------------------------
template <int32 S> read_lock::line_iter::line_iter(const read_lock& lock, char (&buffer)[S])
: line_iter(lock.m_handle_lines, buffer, S)
{
}

//------------------------------------------------------------------------------
template <int32 S> read_lock::line_iter::line_iter(void* handle, char (&buffer)[S])
: line_iter(handle, buffer, S)
{
}

//------------------------------------------------------------------------------
read_lock::line_iter::line_iter(const read_lock& lock, char* buffer, int32 buffer_size)
: m_file_iter(lock.m_handle_lines, buffer, buffer_size)
{
    lock.for_each_removal(lock, [&] (uint32 offset)
    {
        m_removals.insert(offset);
    });
}

//------------------------------------------------------------------------------
read_lock::line_iter::line_iter(void* handle, char* buffer, int32 buffer_size)
: m_file_iter(handle, buffer, buffer_size)
{
}

//------------------------------------------------------------------------------
bool read_lock::line_iter::provision()
{
    return !!(m_remaining = m_file_iter.next(m_remaining));
}

//------------------------------------------------------------------------------
inline bool is_line_breaker(uint8 c)
{
    return c == 0x00 || c == 0x0a || c == 0x0d;
}

//------------------------------------------------------------------------------
line_id_impl read_lock::line_iter::next(str_iter& out, str_base* timestamp, history_db::line_id* timestamp_id)
{
    if (timestamp)
        timestamp->clear();
    if (timestamp_id)
        *timestamp_id = 0;

    while (m_remaining || provision())
    {
        const char* last = m_file_iter.get_buffer() + m_file_iter.get_buffer_size();
        const char* start = last - m_remaining;

        bool first_line = m_first_line || m_eating_ctag;
        bool eating_ctag = m_eating_ctag;

        for (; start != last; ++start, --m_remaining)
            if (!is_line_breaker(*start))
            {
                if (m_first_line)
                {
                    if (*start == '|')
                    {
                        // The <6 is a concession for the history tests.  They
                        // can read with a buffer smaller than 6 characters, but
                        // they don't really understand the CTAG and need the
                        // CTAG line completely hidden from their view, even if
                        // they're using pathologically small buffers.
                        bool eat = (last - start < 6 || strncmp(start, "|CTAG_", 6) == 0);
                        m_eating_ctag = eating_ctag = eat;
                    }
                    m_first_line = false;
                }
                break;
            }

        const char* end = start;
        for (; end != last; ++end)
            if (is_line_breaker(*end))
            {
                m_eating_ctag = false;
                break;
            }

        if (end == last && start != m_file_iter.get_buffer())
        {
            provision();
            continue;
        }

        int32 bytes = int32(end - start);
        m_remaining -= bytes;

        bool was_first_line = m_first_line;
        m_first_line = false;

        uint32 offset_in_buffer = int32(start - m_file_iter.get_buffer());
        const unsigned __int64 real_offset = m_file_iter.get_buffer_offset() + offset_in_buffer;
        const bool too_big = (real_offset >= c_max_line_id.offset);
        assert(!too_big);
        const uint32 offset = too_big ? c_max_line_id.offset : uint32(real_offset);

        // Timestamps precede the line they're associated with, so that the
        // iterator can easily determine whether there's a timestamp and return
        // both the line and the timestamp in a single call.
        if (*start == '|')
        {
            if (strncmp(start, "|\ttime=", 7) == 0)
            {
                if (timestamp)
                {
                    start += 7;
                    timestamp->clear();
                    timestamp->concat(start, int32(end - start));
                }
                if (timestamp_id)
                    *timestamp_id = line_id_impl(offset).outer;
                continue;
            }
            if (timestamp)
                timestamp->clear();
            if (timestamp_id)
                *timestamp_id = 0;
        }

        // Removals from master are deferred when `history.shared` is false, so
        // also test for deferred removals here.
        if (*start == '|' || eating_ctag || (!too_big && m_removals.find(offset) != m_removals.end()))
        {
            if (!eating_ctag)
                ++m_deleted;
            continue;
        }

        new (&out) str_iter(start, int32(end - start));

        return line_id_impl(offset);
    }

    return line_id_impl();
}

//------------------------------------------------------------------------------
void read_lock::line_iter::set_file_offset(uint32 offset)
{
    m_file_iter.set_file_offset(offset);
    m_eating_ctag = false;
}



//------------------------------------------------------------------------------
write_lock::write_lock(const bank_handles& handles)
: read_lock(handles, true)
{
}

//------------------------------------------------------------------------------
void write_lock::clear()
{
    SetFilePointer(m_handle_lines, 0, nullptr, FILE_BEGIN);
    SetEndOfFile(m_handle_lines);
    if (m_handle_removals)
    {
        SetFilePointer(m_handle_removals, 0, nullptr, FILE_BEGIN);
        SetEndOfFile(m_handle_removals);
    }
}

//------------------------------------------------------------------------------
line_id_impl write_lock::add(const char* line)
{
    DWORD written;
    const DWORD offset = SetFilePointer(m_handle_lines, 0, nullptr, FILE_END);
    if (offset == INVALID_SET_FILE_POINTER)
        return line_id_impl();
    WriteFile(m_handle_lines, line, int32(strlen(line)), &written, nullptr);
    WriteFile(m_handle_lines, "\n", 1, &written, nullptr);
    if (offset >= c_max_line_id.offset)
        return c_max_line_id;
    return line_id_impl(offset);
}

//------------------------------------------------------------------------------
bool write_lock::remove(line_id_impl id)
{
    if (id.offset == c_max_line_id.offset)
    {
        assert(false);
        LOG("can't remove history line; offset is too large");
        return false;
    }

    if (m_handle_removals && id.bank_index == bank_master)
    {
        str<> s;
        s.format("%d\n", id.offset);

        DWORD written;
        SetFilePointer(m_handle_removals, 0, nullptr, FILE_END);
        WriteFile(m_handle_removals, s.c_str(), s.length(), &written, nullptr);
    }
    else
    {
        DWORD written;
        SetFilePointer(m_handle_lines, id.offset, nullptr, FILE_BEGIN);
        WriteFile(m_handle_lines, "|", 1, &written, nullptr);
    }

    return true;
}

//------------------------------------------------------------------------------
void write_lock::append(const read_lock& src)
{
    DWORD written;

    SetFilePointer(m_handle_lines, 0, nullptr, FILE_END);

    history_read_buffer buffer;
    read_lock::file_iter src_iter(src, buffer.data(), buffer.size());
    while (int32 bytes_read = src_iter.next())
        WriteFile(m_handle_lines, buffer.data(), bytes_read, &written, nullptr);
}



//------------------------------------------------------------------------------
class read_line_iter
{
public:
                            read_line_iter(const history_db& db, uint32 this_size);
    history_db::line_id     next(str_iter& out, str_base* timestamp=nullptr, history_db::line_id* timestamp_id=nullptr);
    uint32                  get_bank() const { return m_bank_index; }

private:
    bool                    next_bank();
    const history_db&       m_db;
    read_lock               m_lock;
    read_lock::line_iter    m_line_iter;
    uint32                  m_buffer_size;
    uint32                  m_bank_index = bank_none;
};

//------------------------------------------------------------------------------
read_line_iter::read_line_iter(const history_db& db, uint32 this_size)
: m_db(db)
, m_buffer_size(this_size - sizeof(*this))
{
    next_bank();
}

//------------------------------------------------------------------------------
bool read_line_iter::next_bank()
{
    while (++m_bank_index < sizeof_array(m_db.m_bank_handles))
    {
        bank_handles handles = m_db.get_bank(m_bank_index);
        if (handles)
        {
            char* buffer = (char*)(this + 1);
            m_lock.~read_lock();
            m_line_iter.~line_iter();
            new (&m_lock) read_lock(handles);
            new (&m_line_iter) read_lock::line_iter(m_lock, buffer, m_buffer_size);
            return true;
        }
    }

    return false;
}

//------------------------------------------------------------------------------
history_db::line_id read_line_iter::next(str_iter& out, str_base* timestamp, history_db::line_id* timestamp_id)
{
    if (m_bank_index >= sizeof_array(m_db.m_bank_handles))
        return 0;

    do
    {
        if (line_id_impl ret = m_line_iter.next(out, timestamp, timestamp_id))
        {
            ret.bank_index = m_bank_index;
            return ret.outer;
        }
    }
    while (next_bank());

    return 0;
}



//------------------------------------------------------------------------------
history_db::iter::iter(iter&& other)
{
    impl = other.impl;
    other.impl = 0;
}

//------------------------------------------------------------------------------
history_db::iter::~iter()
{
    if (impl)
        ((read_line_iter*)impl)->~read_line_iter();
}

//------------------------------------------------------------------------------
history_db::line_id history_db::iter::next(str_iter& out, str_base* timestamp, history_db::line_id* timestamp_id)
{
    return impl ? ((read_line_iter*)impl)->next(out, timestamp, timestamp_id) : 0;
}

//------------------------------------------------------------------------------
uint32 history_db::iter::get_bank() const
{
    return impl ? ((read_line_iter*)impl)->get_bank() : bank_none;
}



//------------------------------------------------------------------------------
static bool extract_ctag(read_lock::file_iter& iter, char* buffer, int32 buffer_size, concurrency_tag& tag)
{
    int32 bytes_read = iter.next();
    if (bytes_read <= 0)
    {
        LOG("read %d bytes", bytes_read);
        return false;
    }

    if (bytes_read >= buffer_size)
        bytes_read = buffer_size - 1;
    buffer[bytes_read] = 0;

    if (strncmp(buffer, "|CTAG_", 6) != 0)
    {
        LOG("first line not a ctag");
        return false;
    }

    char* eol = strpbrk(buffer, "\r\n");
    if (!eol)
    {
        LOG("first line has no line ending");
        return false;
    }
    *eol = '\0';

    tag.set(buffer);
    return true;
}

//------------------------------------------------------------------------------
static bool extract_ctag(const read_lock& lock, concurrency_tag& tag)
{
    char buffer[max_ctag_size];
    read_lock::file_iter iter(lock, buffer);
    return extract_ctag(iter, buffer, sizeof(buffer), tag);
}

//------------------------------------------------------------------------------
static void rewrite_master_bank(write_lock& lock, size_t limit=0, size_t* _kept=nullptr, size_t* _deleted=nullptr, bool uniq=false, size_t* _dups=nullptr, std::map<line_id_impl, line_id_impl>* remap=nullptr)
{
    history_read_buffer buffer;
    str_map_case<size_t>::type seen;

    if (_dups)
        *_dups = 0;

    struct remap_history_line
    {
        auto_free_str   m_line;
        line_id_impl    m_old;
        line_id_impl    m_new;
    };
    struct keep_line_pair
    {
        remap_history_line m_line;
        remap_history_line m_timestamp;
    };

    // Read lines to keep into vector.
    str_iter out;
    str<> tmp;
    str<> timestamp;
    line_id_impl timestamp_id;
    read_lock::line_iter iter(lock, buffer.data(), buffer.size());
    std::vector<std::unique_ptr<keep_line_pair>> lines_to_keep;
    while (const line_id_impl id = iter.next(out, &timestamp, &timestamp_id.outer))
    {
        std::unique_ptr<keep_line_pair> keep = std::make_unique<keep_line_pair>();

        // Initialize the line to keep.  The string pointer allocated here
        // must live until the loop is finished, because its raw pointer is
        // used as the key in the seen map.
        keep->m_line.m_line.set(out.get_pointer(), out.length());

        // Initialize the timestamp to keep, if any.
        tmp.clear();
        if (!timestamp.empty())
            tmp.format("|\ttime=%s", timestamp.c_str());

        // Maybe apply uniq and keep only the latest.
        if (uniq)
        {
            auto const lookup = seen.find(keep->m_line.m_line.get());
            if (lookup != seen.end())
            {
                // Reuse the old entry so the map stays valid.  Leave the old
                // entry present but empty, so the indices don't shift.
                keep = std::move(lines_to_keep[lookup->second]);
                assert(!lines_to_keep[lookup->second].get());
                // Update number of duplicate entries.
                if (_dups)
                    ++(*_dups);
            }
            seen.insert_or_assign(keep->m_line.m_line.get(), lines_to_keep.size());
        }

        // Initialize the rest of the keep struct.
        keep->m_timestamp.m_line.set(tmp.empty() ? nullptr : tmp.c_str());
        keep->m_timestamp.m_old = timestamp_id;
        keep->m_line.m_old = id;

        // And keep them.
        lines_to_keep.emplace_back(std::move(keep));
        assert(!keep.get());
    }

    if (_kept)
        *_kept = lines_to_keep.size();
    if (_deleted)
        *_deleted = iter.get_deleted_count();

    // Clear and write new tag.
    concurrency_tag tag;
    tag.generate_new_tag();
    lock.clear();
    lock.add(tag.get());

    // Decide how many lines to keep.
    size_t start = 0;
    if (0 < limit && limit < lines_to_keep.size())
    {
        for (start = lines_to_keep.size(); limit && start--;)
            if (lines_to_keep[start].get())
                --limit;
    }

    // Write lines from vector.
    for (size_t ii = start; ii < lines_to_keep.size(); ++ii)
    {
        const auto& keep = lines_to_keep[ii];
        if (keep)
        {
            if (keep->m_timestamp.m_line.get())
                keep->m_timestamp.m_new = lock.add(keep->m_timestamp.m_line.get());
            else
                keep->m_timestamp.m_new.outer = 0;
            keep->m_line.m_new = lock.add(keep->m_line.m_line.get());
        }
    }

    // Verify ids monotonically increase.
#ifdef DEBUG
    const keep_line_pair* prev = nullptr;
    for (size_t ii = start; ii < lines_to_keep.size(); ++ii)
    {
        const auto& keep = lines_to_keep[ii];
        if (keep)
        {
            if (prev)
            {
                if (keep->m_timestamp.m_line.get())
                {
                    assert(keep->m_timestamp.m_old.outer > prev->m_line.m_old.outer);
                    assert(keep->m_line.m_old.outer > keep->m_timestamp.m_old.outer);
                    assert(keep->m_timestamp.m_new.outer > prev->m_line.m_new.outer);
                    assert(keep->m_line.m_new.outer > keep->m_timestamp.m_new.outer);
                }
                assert(keep->m_line.m_old.outer > prev->m_line.m_old.outer);
                assert(keep->m_line.m_new.outer > prev->m_line.m_new.outer);
            }
            prev = keep.get();
        }
    }
#endif

    if (remap)
    {
        for (size_t ii = start; ii < lines_to_keep.size(); ++ii)
        {
            const auto& keep = lines_to_keep[ii];
            if (keep)
            {
                if (keep->m_timestamp.m_line.get())
                    remap->emplace(keep->m_timestamp.m_old.outer, keep->m_timestamp.m_new.outer);
                remap->emplace(keep->m_line.m_old.outer, keep->m_line.m_new.outer);
            }
        }
    }
}

//------------------------------------------------------------------------------
static void migrate_history(const char* path, bool m_diagnostic)
{
    bank_handles handles;
    handles.m_handle_lines = open_file(path);
    if (!handles)
        return;

    // First lock the history bank.
    write_lock lock(handles);

    // Then test if it's empty -- only migrate if it's empty.
    DWORD high = 0;
    DWORD low = GetFileSize(handles.m_handle_lines, &high);
    if (!low && !high)
    {
        // Build the old history file name.
        str<> old_file;
        path::get_directory(path, old_file);
        path::append(old_file, ".history");
        DIAG("... migrate from '%s'\n", old_file.c_str());

        // Open the old history file and try to migrate.
        FILE* old = fopen(old_file.c_str(), "r");
        if (old)
        {
            // Clear and write new tag.
            concurrency_tag tag;
            tag.generate_new_tag();
            lock.clear();
            lock.add(tag.get());

            // Copy old history.
            int32 buffer_size = 8192;
            char* buffer = static_cast<char*>(malloc(buffer_size));
            while (fgets(buffer, buffer_size, old))
            {
                size_t len = strlen(buffer);
                while (len--)
                {
                    char c = buffer[len];
                    if (c != '\n' && c != '\r')
                        break;
                    buffer[len] = '\0';
                }
                lock.add(buffer);
            }

            fclose(old);
        }
    }

    handles.close();
}



//------------------------------------------------------------------------------
history_db::history_db(const char* path, int32 id, bool use_master_bank)
: m_path(path)
, m_id(id)
, m_use_master_bank(use_master_bank)
{
    static_assert(sizeof(line_id) == sizeof(line_id_impl), "");

    memset(m_bank_handles, 0, sizeof(m_bank_handles));
    m_master_len = 0;
    m_master_deleted_count = 0;

    history_inhibit_expansion_function = history_expand_control;

    if (path::is_device(m_path.c_str()))
    {
        m_path.clear();
        return;
    }

    // Remember the bank file names so they are stable for the lifetime of this
    // history_db.  Otherwise changing %CLINK_HISTORY_LABEL% can change the file
    // name prematurely and history can bleed across during reap().  This also
    // enables is_stale_name() to identify when the history_db instance needs to
    // be recreated.
    get_file_path(m_bank_filenames[bank_master], false);
    get_file_path(m_bank_filenames[bank_session], true);

    // Create a self-deleting file to used to indicate this session's alive
    str<280> alive(m_bank_filenames[bank_session].c_str());
    alive << "~";

    wstr<> walive(alive.c_str());
    DWORD flags = FILE_FLAG_DELETE_ON_CLOSE|FILE_ATTRIBUTE_HIDDEN;
    m_alive_file = CreateFileW(walive.c_str(), 0, 0, nullptr, CREATE_ALWAYS, flags, nullptr);
    m_alive_file = (m_alive_file == INVALID_HANDLE_VALUE) ? nullptr : m_alive_file;
}

//------------------------------------------------------------------------------
history_db::~history_db()
{
    // Close alive handle
    if (m_alive_file)
        CloseHandle(m_alive_file);

    // Close all but the master bank. We're going to append to the master one.
    for (int32 i = 1; i < sizeof_array(m_bank_handles); ++i)
        m_bank_handles[i].close();

    reap();

    m_bank_handles[bank_master].close();
}

//------------------------------------------------------------------------------
void history_db::reap()
{
    if (!is_valid())
        return;

    dbg_ignore_scope(snapshot, "History");

    str<280> removals;

    for_each_session([&](str_base& path, bool local)
    {
        path << "~";
        if (os::get_path_type(path.c_str()) == os::path_type_file)
            if (!os::unlink(path.c_str())) // abandoned alive files will unlink
                return;

        path.truncate(path.length() - 1);
        DIAG("... reap session file '%s'\n", path.c_str());

        if (local)
        {
            os::unlink(path.c_str()); // simply delete local files, i.e. `history.save` is false.
            return;
        }

        removals = path.c_str();
        removals << ".removals";

        if (!m_use_master_bank)
        {
            // Don't copy; only delete.
        }
        else if (os::get_file_size(path.c_str()) > 0 ||
                 os::get_file_size(removals.c_str()) > 0)
        {
            bank_handles reap_handles;
            reap_handles.m_handle_lines = open_file(path.c_str());
            reap_handles.m_handle_removals = open_file(removals.c_str(), true/*if_exists*/);

            if (reap_handles.m_handle_removals)
                DIAG("... reap session file '%s'\n", removals.c_str());

            {
                // WARNING: ALWAYS LOCK MASTER BEFORE SESSION!
                bank_handles master_handles = get_bank(bank_master);
                master_handles.m_handle_removals = nullptr; // Don't redirect removals.
                write_lock dest(master_handles);
                read_lock src(reap_handles);
                if (src && dest)
                {
                    dest.append(src);
                    src.apply_removals(dest);
                }
            }

            reap_handles.close();
        }

        os::unlink(removals.c_str());
        os::unlink(path.c_str());
    });
}

//------------------------------------------------------------------------------
void history_db::initialise(str_base* error_message)
{
    if (m_bank_handles[bank_master] || m_bank_handles[bank_session] || !is_valid())
        return;

    str<280> path;
    path << m_bank_filenames[bank_master];

    if (m_use_master_bank)
    {
        DIAG("... master file '%s'\n", path.c_str());

        // Migrate existing history.
        if (os::get_path_type(path.c_str()) == os::path_type_invalid)
            migrate_history(path.c_str(), m_diagnostic);

        // Open the master bank file.
        m_bank_handles[bank_master].m_handle_lines = open_file(path.c_str(), m_bank_error[bank_master]);
        make_open_error(error_message, bank_master);

        // Retrieve concurrency tag from start of master bank.
        m_master_ctag.clear();
        {
            read_lock lock(get_bank(bank_master), false);
            extract_ctag(lock, m_master_ctag);
        }

        // No concurrency tag?  Inject one.
        if (m_master_ctag.empty())
        {
            write_lock lock(get_bank(bank_master));
            if (!extract_ctag(lock, m_master_ctag))
            {
                rewrite_master_bank(lock);
                extract_ctag(lock, m_master_ctag);
            }
        }
        LOG("master bank ctag: %s", m_master_ctag.get());

        // If history is shared, there is only the master bank.
        if (g_shared.get())
            return;
    }
    else
    {
        DIAG("... no master file\n");
        assert(!m_bank_handles[bank_master].m_handle_lines);
        assert(!m_bank_handles[bank_master].m_handle_removals);
        m_master_ctag.clear();
    }

    path.clear();
    path << m_bank_filenames[bank_session];
    if (!m_use_master_bank)
        path << ".local";
    DIAG("... session file '%s'\n", path.c_str());

    assert(!m_bank_handles[bank_session].m_handle_lines);
    assert(!m_bank_handles[bank_session].m_handle_removals);

    m_bank_handles[bank_session].m_handle_lines = open_file(path.c_str(), m_bank_error[bank_session]);
    make_open_error(error_message, bank_session);
    if (m_use_master_bank && g_dupe_mode.get() == 2) // 'erase_prev'
    {
        str<280> removals;
        removals << path << ".removals";
        DIAG("... removals file '%s'\n", removals.c_str());

        m_bank_handles[bank_session].m_handle_removals = make_removals_file(removals.c_str(), m_master_ctag.get());
    }

    reap(); // collects orphaned history files.
}

//------------------------------------------------------------------------------
bank_t history_db::get_active_bank() const
{
    return (m_use_master_bank && !m_bank_handles[bank_session].m_handle_lines) ? bank_master : bank_session;
}

//------------------------------------------------------------------------------
bank_handles history_db::get_bank(uint32 index) const
{
    // Reading master needs master lines and session removals.
    // Reading session needs session lines.
    // Writing master needs master lines and session removals.
    //   - EXCEPT in apply_removals(), but the caller adjusts that case.
    // Writing session needs session lines.
    bank_handles handles;
    if (index < sizeof_array(m_bank_handles) && is_valid())
    {
        handles.m_handle_lines = m_bank_handles[index].m_handle_lines;
        if (index == bank_master)
            handles.m_handle_removals = m_bank_handles[bank_session].m_handle_removals;
    }
    return handles;
}

//------------------------------------------------------------------------------
template <typename T> void history_db::for_each_bank(T&& callback)
{
    for (int32 i = 0; i < sizeof_array(m_bank_handles); ++i)
    {
        write_lock lock(get_bank(i));
        if (lock && !callback(i, lock))
            break;
    }
}

//------------------------------------------------------------------------------
template <typename T> void history_db::for_each_bank(T&& callback) const
{
    for (int32 i = 0; i < sizeof_array(m_bank_handles); ++i)
    {
        read_lock lock(get_bank(i));
        if (lock && !callback(i, lock))
            break;
    }
}

//------------------------------------------------------------------------------
template <typename T> void history_db::for_each_session(T&& callback) const
{
    assert(is_valid());

    // Fold each session found that has no valid alive file.
    str<280> path;
    path << m_bank_filenames[bank_master] << "_*";

    for (globber i(path.c_str()); i.next(path);)
    {
        // History files have no extension.  (E.g. don't reap supplement files
        // such as *.removals files).
        const char* ext = path::get_extension(path.c_str());
        bool local = (ext && _stricmp(ext, ".local") == 0);
        if (ext && !local)
            continue;

        callback(path, local);
    }
}

//------------------------------------------------------------------------------
bool history_db::is_valid() const
{
    return !m_path.empty();
}

//------------------------------------------------------------------------------
void history_db::get_file_path(str_base& out, bool session) const
{
    out = m_path.c_str();

    if (session && is_valid())
    {
        str<16> suffix;
        suffix.format("_%d", m_id);
        out << suffix;
    }
}

//------------------------------------------------------------------------------
static void __clear_history()
{
    rl_clear_history();
    assert(!rl_undo_list);

    history_prev_use_curr = 0;

    free(const_cast<char*>(history_event_lookup_cache.search_string));
    memset(&history_event_lookup_cache, 0, sizeof(history_event_lookup_cache));

#ifdef UNDO_LIST_HEAP_DIAGNOSTICS
    clink_check_undo_entry_leaks();
#endif
}

//------------------------------------------------------------------------------
void history_db::load_internal()
{
    __clear_history();
    m_index_map.clear();
    m_master_len = 0;
    m_master_deleted_count = 0;

    history_read_buffer buffer;

    DIAG("... loading history\n");

    const history_db& const_this = *this;
    const_this.for_each_bank([&] (uint32 bank_index, const read_lock& lock)
    {
        DIAG("... ... %s bank", bank_index == bank_master ? "master" : "session");

        if (bank_index == bank_master)
        {
            m_master_ctag.clear();
            extract_ctag(lock, m_master_ctag);
        }

        // Subtract 1 from the size to accommodate the forced NUL termination
        // prior to calling add_history.
        read_lock::line_iter iter(lock, buffer.data(), buffer.size() - 1);

        dbg_snapshot_heap(snapshot);

        str_iter out;
        str<32> time;
        line_id_impl id;
        uint32 num_lines = 0;
        while (id = iter.next(out, &time))
        {
            const char* line = out.get_pointer();
            int32 buffer_offset = int32(line - buffer.data());
            buffer.data()[buffer_offset + out.length()] = '\0';
            add_history(line);
            if (!time.empty())
                add_history_time(time.c_str());

            num_lines++;

            id.bank_index = bank_index;
            m_index_map.push_back(id.outer);
            if (bank_index == bank_master)
            {
                //LOG("load:  bank %u, offset %u, active %u:  '%s', len %u", id.bank_index, id.offset, id.active, line, out.length());
                m_master_len = m_index_map.size();
            }
        }

        dbg_ignore_since_snapshot(snapshot, "History");

        if (bank_index == bank_master)
            m_master_deleted_count = iter.get_deleted_count();

        DIAG(":  lines active %u / deleted %u\n", num_lines, iter.get_deleted_count());

        return true;
    });

    DIAG("... total lines active %zu\n", m_index_map.size());
}

//------------------------------------------------------------------------------
void history_db::load_rl_history(bool can_clean)
{
    if (!is_valid())
        return;

    load_internal();

    // The `clink history` command needs to be able to avoid cleaning the master
    // history file.
    if (can_clean && m_use_master_bank)
    {
        if (compact())
            load_internal();
    }
}

//------------------------------------------------------------------------------
void history_db::clear()
{
    if (!is_valid())
        return;

    DIAG("... clearing history\n");

    for_each_bank([&] (uint32 bank_index, write_lock& lock)
    {
        DIAG("... ... %s bank\n", bank_index == bank_master ? "master" : "session");

        lock.clear();
        if (bank_index == bank_master)
        {
            m_master_ctag.clear();
            m_master_ctag.generate_new_tag();
            lock.add(m_master_ctag.get());
        }
        return true;
    });

    m_index_map.clear();
    m_master_len = 0;
    m_master_deleted_count = 0;
}

//------------------------------------------------------------------------------
bool history_db::compact(bool force, bool uniq, int32 _limit)
{
    if (!is_valid())
        return false;

    if (!m_use_master_bank)
    {
        assert(false);
        LOG("History:  compact is disabled because master bank is disabled");
        DIAG("... compact:  nothing to do because master bank is disabled");
        return false;
    }

    const bool explicit_limit = (_limit >= 0);

    size_t limit;
    if (_limit < 0)
        limit = get_max_history();
    else
        limit = _limit;
    if (limit > c_max_max_history_lines)
        limit = c_max_max_history_lines;

    // When force is true, load_internal() was not called, so m_master_len is 0,
    // this loop can't remove entries, and rewrite_master_bank() does instead.
    if (limit > 0 && !force)
    {
        LOG("History:  %zu active, %zu deleted", m_master_len, m_master_deleted_count);
        DIAG("... prune:  lines active %zu / limit %zu\n", m_master_len, limit);

        // Delete oldest history entries that exceed it.  This only marks them as
        // deleted; compacting is a separate operation.
        if (m_master_len > limit)
        {
            uint32 removed = 0;
            while (m_master_len > limit)
            {
                line_id_impl id;
                id.outer = m_index_map[0];
                if (id.bank_index != bank_master)
                {
                    LOG("tried to trim from non-master bank");
                    break;
                }
                //LOG("remove bank %u, offset %u, active %u (master len was %u)", id.bank_index, id.offset, id.active, m_master_len);
                if (!remove(id))
                {
                    LOG("failed to remove");
                    DIAG("... ... failed to remove line at offset %u\n", id.offset);
                    break;
                }
                removed++;
            }
            LOG("History:  removed %u", removed);
            DIAG("... ... lines removed %u\n", removed);
        }
    }

    // Since the ratio of deleted lines to active lines is already known here,
    // this is the most convenient/performant place to compact the master bank.
    size_t threshold = (limit ? max(limit, m_min_compact_threshold) : 5000);
    if (!(force || m_master_deleted_count > threshold))
    {
        DIAG("... skip compact; threshold is %zu, actual marked for delete is %zu\n", threshold, m_master_deleted_count);
        return false;
    }

    DIAG("... compact:  rewrite master bank\n");

    size_t kept, deleted, dups;
    assert(!m_master_ctag.empty());

    bank_handles master_handles = get_bank(bank_master);
    master_handles.m_handle_removals = nullptr; // Don't redirect removals.
    write_lock dest(master_handles);

    struct removal_file_data
    {
        str_moveable                m_file;
        std::vector<line_id_impl>   m_lines;
    };

    std::vector<removal_file_data> removals_files;
    str_moveable removals;

    // Collect line ids from all removals files that match the current
    // master.  After the master bank gets a new concurreny tag the
    // collected line ids will be translated to their corresponding new ids
    // and written back to the respective removals files with the updated
    // concurrency tag.
    for_each_session([&](str_base& path, bool local)
    {
        if (m_use_master_bank)
        {
            removals = path.c_str();
            removals << ".removals";

            if (os::get_file_size(path.c_str()) > 0 ||
                os::get_file_size(removals.c_str()) > 0)
            {
                bank_handles compact_handles;
                compact_handles.m_handle_lines = open_file(path.c_str());
                compact_handles.m_handle_removals = open_file(removals.c_str(), true/*if_exists*/);

                if (compact_handles.m_handle_removals)
                {
                    DIAG("... compact:  apply removals from '%s'\n", removals.c_str());

                    // WARNING: ALWAYS LOCK MASTER BEFORE SESSION!
                    read_lock src(compact_handles);
                    if (src && dest)
                    {
                        removal_file_data data;
                        if (src.collect_removals(dest, data.m_lines) > 0)
                        {
                            data.m_file = std::move(removals);
                            removals_files.emplace_back(std::move(data));
                        }
                    }
                }

                compact_handles.close();
            }
        }
    });

    // Rewrite the master bank and apply the limit (if any).  This may also
    // optionally enforce uniqueness.  The result counters are written to
    // the log file.
    std::map<line_id_impl, line_id_impl> remap_removals;
    rewrite_master_bank(dest, limit, &kept, &deleted, uniq, &dups, &remap_removals);

    // Extract the new master concurrency tag.
    str<64> old_ctag(m_master_ctag.get());
    m_master_ctag.clear();
    extract_ctag(dest, m_master_ctag);
    assert(!old_ctag.iequals(m_master_ctag.get())); // It should be different.

    // Rewrite each removals files with the new master concurrency tag and
    // the translated line ids.
    str<64> tmp;
    DWORD written;
    for (const auto& r : removals_files)
    {
        assert(os::get_path_type(r.m_file.c_str()) == os::path_type_file);
        void* handle = make_removals_file(r.m_file.c_str(), m_master_ctag.get());

        // Truncate file immedately after the ctag to keep the file in a
        // consistent state even while being rewritten.
        SetEndOfFile(handle);

        // Look up the ids and write the new ids for ones that were kept.
        for (const auto& id : r.m_lines)
        {
            const auto iter = remap_removals.find(id);
            if (iter != remap_removals.end())
            {
                tmp.format("%u\n", iter->second.offset);
                WriteFile(handle, tmp.c_str(), tmp.length(), &written, nullptr);
            }
        }

        CloseHandle(handle);
    }

    if (uniq)
    {
        LOG("Compacted history:  %zu active, %zu deleted, %zu duplicates removed", kept, deleted, dups);
        DIAG("... ... lines active %zu / purged %zu / duplicates removed %zu\n", kept, deleted, dups);
    }
    else
    {
        LOG("Compacted history:  %zu active, %zu deleted", kept, deleted);
        DIAG("... ... lines active %zu / purged %zu\n", kept, deleted);
    }

    return true;
}

//------------------------------------------------------------------------------
bool history_db::add(const char* line, time_t* out_timestamp)
{
    // Ignore empty and/or whitespace prefixed lines?
    if (!line[0] || (g_ignore_space.get() && (line[0] == ' ' || line[0] == '\t')))
        return false;

    // Handle duplicates.
    switch (g_dupe_mode.get())
    {
    case 1:
        // 'ignore'
        if (line_id find_result = find(line))
            return true;
        break;

    case 2:
        // 'erase_prev'
        remove(line);
        break;
    }

    // Add the line.
    write_lock lock(get_bank(get_active_bank()));
    if (!lock)
        return false;

    if (g_history_timestamp.get() > 0)
    {
        str<32> timestamp;
        const time_t now = time(0);
        if (out_timestamp)
            *out_timestamp = now;
        timestamp.format("|\ttime=%u", now);
        lock.add(timestamp.c_str());
    }

    lock.add(line);
    return true;
}

//------------------------------------------------------------------------------
int32 history_db::remove(const char* line)
{
    int32 count = 0;
    for_each_bank([line, &count] (uint32 index, write_lock& lock)
    {
        lock.find(line, [&] (line_id_impl id) {
            // The line id was retrieved inside this lock scope, so it's still
            // valid; no need to guard the ctag.
            lock.remove(id);
            count++;
            return true;
        });

        return true;
    });

    return count;
}

//------------------------------------------------------------------------------
bool history_db::remove_internal(line_id id, bool guard_ctag)
{
    if (!id)
    {
        LOG("blank history id");
        return false;
    }

    line_id_impl id_impl;
    id_impl.outer = id;

    write_lock lock(get_bank(id_impl.bank_index));
    if (!lock)
    {
        ERR("couldn't lock");
        return false;
    }

    if (guard_ctag && id_impl.bank_index == bank_master)
    {
        concurrency_tag tag;
        if (!extract_ctag(lock, tag))
        {
            LOG("no ctag");
            return false;
        }
        if (strcmp(tag.get(), m_master_ctag.get()) != 0)
        {
            LOG("ctag '%s' doesn't match '%s'", tag.get(), m_master_ctag.get());
            return false;
        }
    }

    if (!lock.remove(id_impl))
        return false;

    if (id_impl.bank_index == bank_master)
    {
        auto last = m_index_map.begin() + m_master_len;
        auto nth = std::lower_bound(m_index_map.begin(), last, id);
        if (nth != last && id == *nth)
        {
            m_index_map.erase(nth);
            --m_master_len;
            ++m_master_deleted_count;
        }
        else
            assert(m_index_map.empty()); // Index map is empty when using `clink history delete`.
    }
    else
    {
        auto first = m_index_map.begin() + m_master_len;
        auto nth = std::lower_bound(first, m_index_map.end(), id);
        if (nth != m_index_map.end() && id == *nth)
            m_index_map.erase(nth);
        else
            assert(m_index_map.empty()); // Index map is empty when using `clink history delete`.
    }

    return true;
}

//------------------------------------------------------------------------------
void history_db::make_open_error(str_base* error_message, bank_t bank) const
{
    assert(bank == bank_master || bank == bank_session);

    const DWORD code = m_bank_error[bank];
    if (code != NOERROR &&
        code != ERROR_FILE_NOT_FOUND &&
        error_message &&
        error_message->empty())
    {
        error_message->format("Unable to open history file \"%s\".\n", m_bank_filenames[bank].c_str());

        wchar_t buf[1024];
        const DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD cch = FormatMessageW(flags, 0, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, sizeof_array(buf), nullptr);

        str<> tmp;
        if (cch)
            tmp = buf;
        else if (code < 65536)
            tmp.format("Error %u.", code);
        else
            tmp.format("Error 0x%08X.", code);
        error_message->concat(tmp.c_str());

        while (error_message->length() && strchr("\r\n", error_message->c_str()[error_message->length() - 1]))
            error_message->truncate(error_message->length() - 1);
    }
}

//------------------------------------------------------------------------------
bool history_db::remove(int32 rl_history_index, const char* /*line*/)
{
    if (rl_history_index < 0)
        return false;

    if (size_t(rl_history_index) >= m_index_map.size())
    {
        // It may be an in-memory-only entry, so allow Readline to remove it.
        return true;
    }

    return remove(m_index_map[rl_history_index]);
}

//------------------------------------------------------------------------------
history_db::line_id history_db::find(const char* line) const
{
    line_id_impl ret;

    for_each_bank([line, &ret] (uint32 index, const read_lock& lock)
    {
        if (ret = lock.find(line))
            ret.bank_index = index;
        return !ret;
    });

    return ret.outer;
}

//------------------------------------------------------------------------------
history_db::expand_result history_db::expand(const char* line, str_base& out)
{
    // The history expansion library can have side effects on the global
    // history variables.  Must save and restore them.
    save_history_expansion_state();

    // Reset history offset so expansion is always relative to the end of
    // the history list.
    using_history();

    // Perform history expansion.
    char* expanded = nullptr;
    int32 result = history_expand((char*)line, &expanded);
    if (result >= 0 && expanded != nullptr)
        out.copy(expanded);
    free(expanded);

    // Restore the global history variables.
    restore_history_expansion_state();

    return expand_result(result);
}

//------------------------------------------------------------------------------
void history_db::get_history_path(str_base& out) const
{
    get_file_path(out, false);
}

//------------------------------------------------------------------------------
history_db::iter history_db::read_lines(char* buffer, uint32 size)
{
    iter ret;
    if (size > sizeof(read_line_iter))
        ret.impl = uintptr_t(new (buffer) read_line_iter(*this, size));

    return ret;
}

//------------------------------------------------------------------------------
bool history_db::has_bank(bank_t bank) const
{
    assert(uint8(bank) < sizeof_array(m_bank_handles));
    return !!m_bank_handles[bank].m_handle_lines;
}

//------------------------------------------------------------------------------
bool history_db::is_stale_name(const char* path) const
{
    if (!is_valid())
        return false;

    return str_cmp(path, m_bank_filenames[bank_master].c_str()) != 0;
}



//------------------------------------------------------------------------------
history_database::history_database(const char* path, int32 id, bool use_master_bank)
: history_db(path, id, use_master_bank)
{
}
