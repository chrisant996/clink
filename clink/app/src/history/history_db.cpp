// Copyright (c) 2017 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "history_db.h"
#include "utils/app_context.h"

#include <core/base.h>
#include <core/globber.h>
#include <core/os.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <core/path.h>
#include <core/log.h>
#include <assert.h>

#include <new>
#include <Windows.h>
extern "C" {
#include <readline/readline.h>
#include <readline/history.h>
}

#include <algorithm>
#include <unordered_set>

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
    "The number of history lines to save if history.save is enabled (1 to 50000).",
    2500);
};

static setting_bool g_ignore_space(
    "history.ignore_space",
    "Skip adding lines prefixed with whitespace",
    "Ignore lines that begin with whitespace when adding lines in to\n"
    "the history.",
    true);

static setting_enum g_dupe_mode(
    "history.dupe_mode",
    "Controls how duplicate entries are handled",
    "If a line is a duplicate of an existing history entry Clink will erase\n"
    "the duplicate when this is set to 'erase_prev'. A value of 'ignore' will\n"
    "not add a line to the history if it already exists, and a value of 'add'\n"
    "will always add lines.\n"
    "Note that history is not deduplicated when reading/writing to disk.",
    "add,ignore,erase_prev",
    2);

static setting_enum g_expand_mode(
    "history.expand_mode",
    "Sets how command history expansion is applied",
    "The '!' character in an entered line can be interpreted to introduce\n"
    "words from the history. That can be enabled and disable by setting this\n"
    "value to 'on' or 'off'. Or set this to 'not_squoted', 'not_dquoted', or\n"
    "'not_quoted' to skip any '!' character in single, double, or both quotes\n"
    "respectively.",
    "off,on,not_squoted,not_dquoted,not_quoted",
    4);

static size_t get_max_history()
{
    size_t limit = use_get_max_history_instead::g_max_history.get();
    if (!limit || limit > 50000)
        limit = 50000;
    return limit;
}



//------------------------------------------------------------------------------
static int history_expand_control(char* line, int marker_pos)
{
    int setting, in_quote, i;

    setting = g_expand_mode.get();
    if (setting <= 1)
        return (setting <= 0);

    // Is marker_pos inside a quote of some kind?
    in_quote = 0;
    for (i = 0; i < marker_pos && *line; ++i, ++line)
    {
        int c = *line;
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
static void get_file_path(str_base& out, bool session)
{
    out.clear();

    const auto* app = app_context::get();
    app->get_history_path(out);

    if (session)
    {
        str<16> suffix;
        suffix.format("_%d", app->get_id());
        out << suffix;
    }
}

//------------------------------------------------------------------------------
static void* open_file(const char* path, bool if_exists=false)
{
    DWORD share_flags = FILE_SHARE_READ|FILE_SHARE_WRITE;
    void* handle = CreateFile(path, GENERIC_READ|GENERIC_WRITE, share_flags,
        nullptr, if_exists ? OPEN_EXISTING : OPEN_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

    return (handle == INVALID_HANDLE_VALUE) ? nullptr : handle;
}



//------------------------------------------------------------------------------
const int max_ctag_size = 6 + 10 + 1 + 10 + 1 + 10 + 1 + 10 + 1 + 1;
void concurrency_tag::generate_new_tag()
{
    static unsigned int disambiguate = 0;

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
class auto_free_str
{
public:
                        auto_free_str() = default;
                        auto_free_str(const char* s, int len) { set(s, len); }
                        auto_free_str(auto_free_str&& other) : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
                        ~auto_free_str() { free(m_ptr); }

    auto_free_str&      operator=(const char* s) { set(s); return *this; }
    auto_free_str&      operator=(auto_free_str&& other) { m_ptr = other.m_ptr; other.m_ptr = nullptr; return *this; }
    void                set(const char* s, int len = -1);
    const char*         get() const { return m_ptr; }

private:
    char*               m_ptr = nullptr;
};

//------------------------------------------------------------------------------
void auto_free_str::set(const char* s, int len)
{
    if (s == m_ptr)
    {
        if (len < int(strlen(m_ptr)))
            m_ptr[len] = '\0';
    }
    else
    {
        char* old = m_ptr;
        if (len < 0)
            len = int(strlen(s));
        m_ptr = (char*)malloc(len + 1);
        memcpy(m_ptr, s, len);
        m_ptr[len] = '\0';
        free(old);
    }
}



//------------------------------------------------------------------------------
union line_id_impl
{
    explicit            line_id_impl()                  { outer = 0; }
    explicit            line_id_impl(unsigned int o)    { offset = o; bank_index = 0; active = 1; }
    explicit            operator bool () const          { return !!outer; }
    struct {
        unsigned int    offset : 29;
        unsigned int    bank_index : 2;
        unsigned int    active : 1;
    };
    history_db::line_id outer;
};



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
    int flags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
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
                            file_iter(const read_lock& lock, char* buffer, int buffer_size);
                            file_iter(void* handle, char* buffer, int buffer_size);
        template <int S>    file_iter(const read_lock& lock, char (&buffer)[S]);
        template <int S>    file_iter(void* handle, char (&buffer)[S]);
        unsigned int        next(unsigned int rollback=0);
        unsigned int        get_buffer_offset() const   { return m_buffer_offset; }
        char*               get_buffer() const          { return m_buffer; }
        unsigned int        get_buffer_size() const     { return m_buffer_size; }
        unsigned int        get_remaining() const       { return m_remaining; }
        void                set_file_offset(unsigned int offset);

    private:
        char*               m_buffer;
        void*               m_handle;
        unsigned int        m_buffer_size;
        unsigned int        m_buffer_offset;
        unsigned int        m_remaining;
    };

    class line_iter : public no_copy
    {
    public:
                            line_iter() = default;
                            line_iter(const read_lock& lock, char* buffer, int buffer_size);
                            line_iter(void* handle, char* buffer, int buffer_size);
        template <int S>    line_iter(const read_lock& lock, char (&buffer)[S]);
        template <int S>    line_iter(void* handle, char (&buffer)[S]);
                            ~line_iter() = default;
        line_id_impl        next(str_iter& out);
        void                set_file_offset(unsigned int offset);
        unsigned int        get_deleted_count() const { return m_deleted; }

    private:
        bool                provision();
        file_iter           m_file_iter;
        unsigned int        m_remaining = 0;
        unsigned int        m_deleted = 0;
        bool                m_first_line = true;
        bool                m_eating_ctag = false;
        std::unordered_set<unsigned int> m_removals;
    };

    explicit                read_lock() = default;
    explicit                read_lock(const bank_handles& handles, bool exclusive=false);
    line_id_impl            find(const char* line) const;
    template <class T> void find(const char* line, T&& callback) const;
    void                    apply_removals(write_lock& lock) const;

private:
    template <typename T> static void for_each_removal(void* handle_removals, T&& callback);
};

//------------------------------------------------------------------------------
class write_lock
    : public read_lock
{
public:
                    write_lock() = default;
    explicit        write_lock(const bank_handles& handles);
    void            clear();
    void            add(const char* line);
    void            remove(line_id_impl id);
    void            append(const read_lock& src);
};



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

        unsigned int file_ptr = SetFilePointer(m_handle_lines, 0, nullptr, FILE_CURRENT);
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
void read_lock::apply_removals(write_lock& lock) const
{
    if (m_handle_removals)
        for_each_removal(m_handle_removals, [&] (unsigned int offset)
        {
            line_id_impl id(offset);
            lock.remove(id);
        });
}

//------------------------------------------------------------------------------
template <typename T> void read_lock::for_each_removal(void* handle_removals, T&& callback)
{
    char tmp[512];
    line_iter iter(handle_removals, tmp);

    str_iter value;
    while (iter.next(value))
    {
        unsigned int offset = 0;
        unsigned int len = value.length();
        for (const char *s = value.get_pointer(); len--; s++)
        {
            if (*s < '0' || *s > '9')
                break;
            offset *= 10;
            offset += *s - '0';
        }

        if (offset > 0)
            callback(offset);
    }
}



//------------------------------------------------------------------------------
template <int S> read_lock::file_iter::file_iter(const read_lock& lock, char (&buffer)[S])
: file_iter(lock.m_handle_lines, buffer, S)
{
}

//------------------------------------------------------------------------------
template <int S> read_lock::file_iter::file_iter(void* handle, char (&buffer)[S])
: file_iter(handle, buffer, S)
{
}

//------------------------------------------------------------------------------
read_lock::file_iter::file_iter(const read_lock& lock, char* buffer, int buffer_size)
: m_handle(lock.m_handle_lines)
, m_buffer(buffer)
, m_buffer_size(buffer_size)
{
    set_file_offset(0);
}

//------------------------------------------------------------------------------
read_lock::file_iter::file_iter(void* handle, char* buffer, int buffer_size)
: m_handle(handle)
, m_buffer(buffer)
, m_buffer_size(buffer_size)
{
    set_file_offset(0);
}

//------------------------------------------------------------------------------
unsigned int read_lock::file_iter::next(unsigned int rollback)
{
    if (!m_remaining)
        return (m_buffer[0] = '\0');

    rollback = min<unsigned>(rollback, m_buffer_size);
    if (rollback)
        memmove(m_buffer, m_buffer + m_buffer_size - rollback, rollback);

    m_buffer_offset += m_buffer_size - rollback;

    char* target = m_buffer + rollback;
    int needed = min(m_remaining, m_buffer_size - rollback);

    DWORD read = 0;
    ReadFile(m_handle, target, needed, &read, nullptr);

    m_remaining -= read;
    m_buffer_size = read + rollback;
    return m_buffer_size;
}

//------------------------------------------------------------------------------
void read_lock::file_iter::set_file_offset(unsigned int offset)
{
    m_remaining = GetFileSize(m_handle, nullptr);
    offset = clamp(offset, (unsigned int)0, m_remaining);
    m_remaining -= offset;
    m_buffer_offset = 0 - m_buffer_size;
    SetFilePointer(m_handle, offset, nullptr, FILE_BEGIN);
    m_buffer[0] = '\0';
}



//------------------------------------------------------------------------------
template <int S> read_lock::line_iter::line_iter(const read_lock& lock, char (&buffer)[S])
: line_iter(lock.m_handle_lines, buffer, S)
{
}

//------------------------------------------------------------------------------
template <int S> read_lock::line_iter::line_iter(void* handle, char (&buffer)[S])
: line_iter(handle, buffer, S)
{
}

//------------------------------------------------------------------------------
read_lock::line_iter::line_iter(const read_lock& lock, char* buffer, int buffer_size)
: m_file_iter(lock.m_handle_lines, buffer, buffer_size)
{
    if (lock.m_handle_removals)
    {
        for_each_removal(lock.m_handle_removals, [&] (unsigned int offset)
        {
            m_removals.insert(offset);
        });
    }
}

//------------------------------------------------------------------------------
read_lock::line_iter::line_iter(void* handle, char* buffer, int buffer_size)
: m_file_iter(handle, buffer, buffer_size)
{
}

//------------------------------------------------------------------------------
bool read_lock::line_iter::provision()
{
    return !!(m_remaining = m_file_iter.next(m_remaining));
}

//------------------------------------------------------------------------------
inline bool is_line_breaker(unsigned char c)
{
    return c == 0x00 || c == 0x0a || c == 0x0d;
}

//------------------------------------------------------------------------------
line_id_impl read_lock::line_iter::next(str_iter& out)
{
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

        int bytes = int(end - start);
        m_remaining -= bytes;

        bool was_first_line = m_first_line;
        m_first_line = false;

        unsigned int offset_in_buffer = int(start - m_file_iter.get_buffer());
        unsigned int offset = m_file_iter.get_buffer_offset() + offset_in_buffer;

        // Removals from master are deferred when `history.shared` is false, so
        // also test for deferred removals here.
        if (*start == '|' || eating_ctag || m_removals.find(offset) != m_removals.end())
        {
            if (!eating_ctag)
                ++m_deleted;
            continue;
        }

        new (&out) str_iter(start, int(end - start));

        return line_id_impl(offset);
    }

    return line_id_impl();
}

//------------------------------------------------------------------------------
void read_lock::line_iter::set_file_offset(unsigned int offset)
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
void write_lock::add(const char* line)
{
    DWORD written;
    SetFilePointer(m_handle_lines, 0, nullptr, FILE_END);
    WriteFile(m_handle_lines, line, int(strlen(line)), &written, nullptr);
    WriteFile(m_handle_lines, "\n", 1, &written, nullptr);
}

//------------------------------------------------------------------------------
void write_lock::remove(line_id_impl id)
{
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
}

//------------------------------------------------------------------------------
void write_lock::append(const read_lock& src)
{
    DWORD written;

    SetFilePointer(m_handle_lines, 0, nullptr, FILE_END);

    history_read_buffer buffer;
    read_lock::file_iter src_iter(src, buffer.data(), buffer.size());
    while (int bytes_read = src_iter.next())
        WriteFile(m_handle_lines, buffer.data(), bytes_read, &written, nullptr);
}



//------------------------------------------------------------------------------
class read_line_iter
{
public:
                            read_line_iter(const history_db& db, unsigned int this_size);
    history_db::line_id     next(str_iter& out);
    unsigned int            get_bank() const { return m_bank_index; }

private:
    bool                    next_bank();
    const history_db&       m_db;
    read_lock               m_lock;
    read_lock::line_iter    m_line_iter;
    unsigned int            m_buffer_size;
    unsigned int            m_bank_index = bank_none;
};

//------------------------------------------------------------------------------
read_line_iter::read_line_iter(const history_db& db, unsigned int this_size)
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
history_db::line_id read_line_iter::next(str_iter& out)
{
    if (m_bank_index > sizeof_array(m_db.m_bank_handles))
        return 0;

    do
    {
        if (line_id_impl ret = m_line_iter.next(out))
        {
            ret.bank_index = m_bank_index - 1;
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
history_db::line_id history_db::iter::next(str_iter& out)
{
    return impl ? ((read_line_iter*)impl)->next(out) : 0;
}

//------------------------------------------------------------------------------
unsigned int history_db::iter::get_bank() const
{
    return impl ? ((read_line_iter*)impl)->get_bank() : bank_none;
}



//------------------------------------------------------------------------------
static bool extract_ctag(const read_lock& lock, concurrency_tag& tag)
{
    char buffer[max_ctag_size];
    read_lock::file_iter iter(lock, buffer);

    int bytes_read = iter.next();
    if (bytes_read <= 0)
    {
        LOG("read %d bytes", bytes_read);
        return false;
    }

    if (bytes_read >= sizeof(buffer))
        bytes_read = sizeof(buffer) - 1;
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
static void rewrite_master_bank(write_lock& lock, size_t* _kept=nullptr, size_t* _deleted=nullptr)
{
    history_read_buffer buffer;

    // Read lines to keep into vector.
    str_iter out;
    read_lock::line_iter iter(lock, buffer.data(), buffer.size());
    std::vector<auto_free_str> lines_to_keep;
    while (iter.next(out))
        lines_to_keep.push_back(std::move(auto_free_str(out.get_pointer(), out.length())));

    if (_kept)
        *_kept = lines_to_keep.size();
    if (_deleted)
        *_deleted = iter.get_deleted_count();

    // Clear and write new tag.
    concurrency_tag tag;
    tag.generate_new_tag();
    lock.clear();
    lock.add(tag.get());

    // Write lines from vector.
    for (auto const& line : lines_to_keep)
        lock.add(line.get());
}

//------------------------------------------------------------------------------
static void migrate_history(const char* path, bool m_diagnostic)
{
    str<280> removals;
    removals = path;
    removals << ".removals";

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
            int buffer_size = 8192;
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
history_db::history_db(bool use_master_bank)
: m_use_master_bank(use_master_bank)
{
    memset(m_bank_handles, 0, sizeof(m_bank_handles));
    m_master_len = 0;
    m_master_deleted_count = 0;

    // Remember the bank file names so they are stable for the lifetime of this
    // history_db.  Otherwise changing %CLINK_HISTORY_LABEL% can change the file
    // name prematurely and history can bleed across during reap().  This also
    // enables is_stale_name() to identify when the history_db instance needs to
    // be recreated.
    get_file_path(m_bank_filenames[bank_master], false);
    get_file_path(m_bank_filenames[bank_session], true);

    // Create a self-deleting file to used to indicate this session's alive
    str<280> path(m_bank_filenames[bank_session].c_str());
    path << "~";

    DWORD flags = FILE_FLAG_DELETE_ON_CLOSE|FILE_ATTRIBUTE_HIDDEN;
    m_alive_file = CreateFile(path.c_str(), 0, 0, nullptr, CREATE_ALWAYS, flags, nullptr);
    m_alive_file = (m_alive_file == INVALID_HANDLE_VALUE) ? nullptr : m_alive_file;

    history_inhibit_expansion_function = history_expand_control;

    static_assert(sizeof(line_id) == sizeof(line_id_impl), "");
}

//------------------------------------------------------------------------------
history_db::~history_db()
{
    // Close alive handle
    CloseHandle(m_alive_file);

    // Close all but the master bank. We're going to append to the master one.
    for (int i = 1; i < sizeof_array(m_bank_handles); ++i)
        m_bank_handles[i].close();

    reap();

    m_bank_handles[bank_master].close();
}

//------------------------------------------------------------------------------
void history_db::reap()
{
    // Fold each session found that has no valid alive file.
    str<280> path;
    path << m_bank_filenames[bank_master] << "_*";

    str<280> removals;
    for (globber i(path.c_str()); i.next(path);)
    {
        // History files have no extension.  Don't reap supplement files such as
        // *.removals files.
        const char* ext = path::get_extension(path.c_str());
        bool local = (ext && _stricmp(ext, ".local") == 0);
        if (ext && !local)
            continue;

        path << "~";
        if (os::get_path_type(path.c_str()) == os::path_type_file)
            if (!os::unlink(path.c_str())) // abandoned alive files will unlink
                continue;

        path.truncate(path.length() - 1);
        DIAG("... reap session file '%s'\n", path.c_str());

        if (local)
        {
            os::unlink(path.c_str()); // simply delete local files, i.e. `history.save` is false.
            continue;
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
    }
}

//------------------------------------------------------------------------------
void history_db::initialise()
{
    if (m_bank_handles[bank_master])
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
        m_bank_handles[bank_master].m_handle_lines = open_file(path.c_str());

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

    m_bank_handles[bank_session].m_handle_lines = open_file(path.c_str());
    if (m_use_master_bank && g_dupe_mode.get() == 2) // 'erase_prev'
    {
        str<280> removals;
        removals << path << ".removals";
        DIAG("... removals file '%s'\n", removals.c_str());

        m_bank_handles[bank_session].m_handle_removals = open_file(removals.c_str());
    }

    reap(); // collects orphaned history files.
}

//------------------------------------------------------------------------------
unsigned int history_db::get_active_bank() const
{
    return (m_use_master_bank && g_shared.get()) ? bank_master : bank_session;
}

//------------------------------------------------------------------------------
bank_handles history_db::get_bank(unsigned int index) const
{
    // Reading master needs master lines and session removals.
    // Reading session needs session lines.
    // Writing master needs master lines and session removals.
    //   - EXCEPT in apply_removals(), but the caller adjusts that case.
    // Writing session needs session lines.
    bank_handles handles;
    if (index < sizeof_array(m_bank_handles))
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
    for (int i = 0; i < sizeof_array(m_bank_handles); ++i)
    {
        write_lock lock(get_bank(i));
        if (lock && !callback(i, lock))
            break;
    }
}

//------------------------------------------------------------------------------
template <typename T> void history_db::for_each_bank(T&& callback) const
{
    for (int i = 0; i < sizeof_array(m_bank_handles); ++i)
    {
        read_lock lock(get_bank(i));
        if (lock && !callback(i, lock))
            break;
    }
}

//------------------------------------------------------------------------------
void history_db::load_internal()
{
    clear_history();
    m_index_map.clear();
    m_master_len = 0;
    m_master_deleted_count = 0;

    history_read_buffer buffer;

    DIAG("... loading history\n");

    const history_db& const_this = *this;
    const_this.for_each_bank([&] (unsigned int bank_index, const read_lock& lock)
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

        str_iter out;
        line_id_impl id;
        unsigned int num_lines = 0;
        while (id = iter.next(out))
        {
            const char* line = out.get_pointer();
            int buffer_offset = int(line - buffer.data());
            buffer.data()[buffer_offset + out.length()] = '\0';
            add_history(line);

            num_lines++;

            id.bank_index = bank_index;
            m_index_map.push_back(id.outer);
            if (bank_index == bank_master)
            {
                //LOG("load:  bank %u, offset %u, active %u:  '%s', len %u", id.bank_index, id.offset, id.active, line, out.length());
                m_master_len = m_index_map.size();
            }
        }

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
    load_internal();

    // The `clink history` command needs to be able to avoid cleaning the master
    // history file.
    if (can_clean && m_use_master_bank)
    {
        compact();
        load_internal();
    }
}

//------------------------------------------------------------------------------
void history_db::clear()
{
    DIAG("... clearing history\n");

    for_each_bank([&] (unsigned int bank_index, write_lock& lock)
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
void history_db::compact(bool force)
{
    if (!m_use_master_bank)
    {
        assert(false);
        LOG("History:  compact is disabled because master bank is disabled");
        DIAG("... compact:  nothing to do because master bank is disabled");
        return;
    }

    size_t limit = get_max_history();
    if (limit > 0 && !force)
    {
        LOG("History:  %zu active, %zu deleted", m_master_len, m_master_deleted_count);
        DIAG("... prune:  lines active %zu / limit %zu\n", m_master_len, limit);

        // Delete oldest history entries that exceed it.  This only marks them as
        // deleted; compacting is a separate operation.
        if (m_master_len > limit)
        {
            unsigned int removed = 0;
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
                if (!remove(m_index_map[0]))
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
    size_t threshold = (limit ? max(limit, m_min_compact_threshold) : 2500);
    if (force || m_master_deleted_count > threshold)
    {
        DIAG("... compact:  rewrite master bank\n");

        size_t kept, deleted;
        write_lock lock(get_bank(bank_master));
        rewrite_master_bank(lock, &kept, &deleted);
        LOG("Compacted history:  %zu active, %zu deleted", kept, deleted);
        DIAG("... ... lines active %zu / purged %zu\n", kept, deleted);
    }
    else
    {
        DIAG("... skip compact; threshold is %zu, actual marked for delete is %zu\n", threshold, m_master_deleted_count);
    }
}

//------------------------------------------------------------------------------
bool history_db::add(const char* line)
{
    // Ignore empty and/or whitespace prefixed lines?
    if (!line[0] || (g_ignore_space.get() && (line[0] == ' ' || line[0] == '\t')))
        return false;

    // Ignore when operate-and-get-next was used, so that we don't rearrange history while it's
    // trying to set the history context.
    if (rl_has_saved_history())
        return false;

    // Handle duplicates.
    int mode = g_dupe_mode.get();
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

    lock.add(line);
    return true;
}

//------------------------------------------------------------------------------
int history_db::remove(const char* line)
{
    int count = 0;
    for_each_bank([line, &count] (unsigned int index, write_lock& lock)
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

    lock.remove(id_impl);

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
bool history_db::remove(int rl_history_index, const char* line)
{
    if (rl_history_index < 0 || size_t(rl_history_index) >= m_index_map.size())
        return false;

#ifdef CLINK_DEBUG
    // Verify the file content matches the expected state.
    line_id_impl id_impl;
    id_impl.outer = m_index_map[rl_history_index];
    {
        read_lock lock(get_bank(id_impl.bank_index));
        assert(lock);

        str_iter out;
        history_read_buffer buffer;
        read_lock::line_iter iter(lock, buffer.data(), buffer.size());

        iter.set_file_offset(id_impl.offset);
        assert(iter.next(out));

        assert(out.length());
        assert(strlen(line) == out.length());
        if (*out.get_pointer() != '|')
            assert(memcmp(line, out.get_pointer(), out.length()) == 0);
    }
#endif

    return remove(m_index_map[rl_history_index]);
}

//------------------------------------------------------------------------------
history_db::line_id history_db::find(const char* line) const
{
    line_id_impl ret;

    for_each_bank([line, &ret] (unsigned int index, const read_lock& lock)
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
    using_history();

    char* expanded = nullptr;
    int result = history_expand((char*)line, &expanded);
    if (result >= 0 && expanded != nullptr)
        out.copy(expanded);

    free(expanded);
    return expand_result(result);
}

//------------------------------------------------------------------------------
history_db::iter history_db::read_lines(char* buffer, unsigned int size)
{
    iter ret;
    if (size > sizeof(read_line_iter))
        ret.impl = uintptr_t(new (buffer) read_line_iter(*this, size));

    return ret;
}

//------------------------------------------------------------------------------
bool history_db::has_bank(unsigned char bank) const
{
    assert(bank < sizeof_array(m_bank_handles));
    return !!m_bank_handles[bank].m_handle_lines;
}

//------------------------------------------------------------------------------
bool history_db::is_stale_name() const
{
    str<280> path;
    get_file_path(path, false);
    return !path.equals(m_bank_filenames[bank_master].c_str());
}
