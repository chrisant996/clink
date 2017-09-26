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

#include <new>
#include <Windows.h>
extern "C" {
#include <readline/history.h>
}

//------------------------------------------------------------------------------
static setting_bool g_shared(
    "history.shared",
    "Share history between instances",
    "",
    false);

static setting_bool g_ignore_space(
    "history.ignore_space",
    "Skip adding lines prefixed with whitespace",
    "Ignore lines that begin with whitespace when adding lines in to\n"
    "the history.",
    true);

static setting_enum g_dupe_mode(
    "history.dupe_mode",
    "Controls how duplicate entries are handled",
    "If a line is a duplicate of an existing history entry Clink will\n"
    "erase the duplicate when this is set 2. A value of 1 will not add\n"
    "duplicates to the history and a value of 0 will always add lines.\n"
    "Note that history is not deduplicated when reading/writing to disk.",
    "add,ignore,erase_dupe",
    2);

static setting_enum g_expand_mode(
    "history.expand_mode",
    "Sets how command history expansion is applied",
    "The '!' character in an entered line can be interpreted to introduce\n"
    "words from the history. This can be enabled and disable by setting this\n"
    "value to 1 or 0. Values or 2, 3 or 4 will skip any ! character quoted\n"
    "in single, double, or both quotes respectively.",
    "off,on,not_squoted,not_dquoted,not_quoted",
    4);



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
static void* open_file(const char* path)
{
    DWORD share_flags = FILE_SHARE_READ|FILE_SHARE_WRITE;
    void* handle = CreateFile(path, GENERIC_READ|GENERIC_WRITE, share_flags,
        nullptr, OPEN_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

    return (handle == INVALID_HANDLE_VALUE) ? nullptr : handle;
}



//------------------------------------------------------------------------------
union line_id_impl
{
    explicit            line_id_impl()                  { outer = 0; }
    explicit            line_id_impl(unsigned int o)    { offset = o; active = 1; }
    explicit            operator bool () const          { return !!outer; }
    struct {
        unsigned int    offset : 29;
        unsigned int    bank_index : 2;
        unsigned int    active : 1;
    };
    history_db::line_id outer;
};



//------------------------------------------------------------------------------
class bank_lock
    : public no_copy
{
public:
    explicit        operator bool () const;

protected:
                    bank_lock() = default;
                    bank_lock(void* handle, bool exclusive);
                    ~bank_lock();
    void*           m_handle = nullptr;
};

//------------------------------------------------------------------------------
bank_lock::bank_lock(void* handle, bool exclusive)
: m_handle(handle)
{
    if (m_handle == nullptr)
        return;

    OVERLAPPED overlapped = {};
    int flags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
    LockFileEx(m_handle, flags, 0, ~0u, ~0u, &overlapped);
}

//------------------------------------------------------------------------------
bank_lock::~bank_lock()
{
    if (m_handle != nullptr)
    {
        OVERLAPPED overlapped = {};
        UnlockFileEx(m_handle, 0, ~0u, ~0u, &overlapped);
    }
}

//------------------------------------------------------------------------------
bank_lock::operator bool () const
{
    return (m_handle != nullptr);
}



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
        template <int S>    file_iter(const read_lock& lock, char (&buffer)[S]);
        unsigned int        next(unsigned int rollback=0);
        unsigned int        get_buffer_offset() const   { return m_buffer_offset; }
        char*               get_buffer() const          { return m_buffer; }
        unsigned int        get_buffer_size() const     { return m_buffer_size; }
        unsigned int        get_remaining() const       { return m_remaining; }

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
        template <int S>    line_iter(const read_lock& lock, char (&buffer)[S]);
        line_id_impl        next(str_iter& out);

    private:
        bool                provision();
        file_iter           m_file_iter;
        unsigned int        m_remaining = 0;
    };

    explicit                read_lock() = default;
    explicit                read_lock(void* handle, bool exclusive=false);
    line_id_impl            find(const char* line) const;
};

//------------------------------------------------------------------------------
read_lock::read_lock(void* handle, bool exclusive)
: bank_lock(handle, exclusive)
{
}

//------------------------------------------------------------------------------
line_id_impl read_lock::find(const char* line) const
{
    char buffer[history_db::max_line_length];
    line_iter iter(*this, buffer);

    str_iter read;
    line_id_impl id;
    while (id = iter.next(read))
        if (strncmp(line, read.get_pointer(), read.length()) == 0)
            break;

    return id;
}



//------------------------------------------------------------------------------
template <int S> read_lock::file_iter::file_iter(const read_lock& lock, char (&buffer)[S])
: file_iter(lock, buffer, S)
{
}

//------------------------------------------------------------------------------
read_lock::file_iter::file_iter(const read_lock& lock, char* buffer, int buffer_size)
: m_handle(lock.m_handle)
, m_buffer(buffer)
, m_buffer_size(buffer_size)
, m_buffer_offset(-buffer_size)
, m_remaining(GetFileSize(lock.m_handle, nullptr))
{
    SetFilePointer(m_handle, 0, nullptr, FILE_BEGIN);
    m_buffer[0] = '\0';
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
template <int S> read_lock::line_iter::line_iter(const read_lock& lock, char (&buffer)[S])
: line_iter(lock, buffer, S)
{
}

//------------------------------------------------------------------------------
read_lock::line_iter::line_iter(const read_lock& lock, char* buffer, int buffer_size)
: m_file_iter(lock, buffer, buffer_size)
{
}

//------------------------------------------------------------------------------
bool read_lock::line_iter::provision()
{
    return !!(m_remaining = m_file_iter.next(m_remaining));
}

//------------------------------------------------------------------------------
line_id_impl read_lock::line_iter::next(str_iter& out)
{
    while (m_remaining || provision())
    {
        const char* last = m_file_iter.get_buffer() + m_file_iter.get_buffer_size();
        const char* start = last - m_remaining;

        for (; start != last; ++start, --m_remaining)
            if (unsigned(*start) > 0x1f)
                break;

        const char* end = start;
        for (; end != last; ++end)
            if (unsigned(*end) <= 0x1f)
                break;

        if (end == last && start != m_file_iter.get_buffer())
        {
            provision();
            continue;
        }

        int bytes = int(end - start);
        m_remaining -= bytes;

        if (*start == '|')
            continue;

        new (&out) str_iter(start, int(end - start));

        unsigned int offset = int(start - m_file_iter.get_buffer());
        return line_id_impl(m_file_iter.get_buffer_offset() + offset);
    }

    return line_id_impl();
}



//------------------------------------------------------------------------------
class write_lock
    : public read_lock
{
public:
                    write_lock() = default;
    explicit        write_lock(void* handle);
    void            clear();
    void            add(const char* line);
    void            remove(line_id_impl id);
    void            append(const read_lock& src);
};

//------------------------------------------------------------------------------
write_lock::write_lock(void* handle)
: read_lock(handle, true)
{
}

//------------------------------------------------------------------------------
void write_lock::clear()
{
    SetFilePointer(m_handle, 0, nullptr, FILE_BEGIN);
    SetEndOfFile(m_handle);
}

//------------------------------------------------------------------------------
void write_lock::add(const char* line)
{
    DWORD written;
    SetFilePointer(m_handle, 0, nullptr, FILE_END);
    WriteFile(m_handle, line, int(strlen(line)), &written, nullptr);
    WriteFile(m_handle, "\n", 1, &written, nullptr);
}

//------------------------------------------------------------------------------
void write_lock::remove(line_id_impl id)
{
    DWORD written;
    SetFilePointer(m_handle, id.offset, nullptr, FILE_BEGIN);
    WriteFile(m_handle, "|", 1, &written, nullptr);
}

//------------------------------------------------------------------------------
void write_lock::append(const read_lock& src)
{
    DWORD written;

    SetFilePointer(m_handle, 0, nullptr, FILE_END);

    char buffer[history_db::max_line_length];
    read_lock::file_iter src_iter(src, buffer);
    while (int bytes_read = src_iter.next())
        WriteFile(m_handle, buffer, bytes_read, &written, nullptr);
}



//------------------------------------------------------------------------------
class read_line_iter
{
public:
                            read_line_iter(const history_db& db, unsigned int this_size);
    history_db::line_id     next(str_iter& out);

private:
    bool                    next_bank();
    const history_db&       m_db;
    read_lock               m_lock;
    read_lock::line_iter    m_line_iter;
    unsigned int            m_buffer_size;
    unsigned int            m_bank_index = 0;
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
    while (m_bank_index < m_db.get_bank_count())
    {
        if (void* bank_handle = m_db.m_bank_handles[m_bank_index++])
        {
            char* buffer = (char*)(this + 1);
            m_lock.~read_lock();
            new (&m_lock) read_lock(bank_handle);
            new (&m_line_iter) read_lock::line_iter(m_lock, buffer, m_buffer_size);
            return true;
        }
    }

    return false;
}

//------------------------------------------------------------------------------
history_db::line_id read_line_iter::next(str_iter& out)
{
    if (m_bank_index > m_db.get_bank_count())
        return 0;

    do
        if (line_id_impl ret = m_line_iter.next(out))
            return ret.outer;
    while (next_bank());

    return 0;
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
history_db::history_db()
{
    memset(m_bank_handles, 0, sizeof(m_bank_handles));

    // Create a self-deleting file to used to indicate this session's alive
    str<280> path;
    get_file_path(path, true);
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
    for (int i = 1, n = get_bank_count(); i < n; ++i)
        CloseHandle(m_bank_handles[i]);

    reap();

    CloseHandle(m_bank_handles[bank_master]);
}

//------------------------------------------------------------------------------
void history_db::reap()
{
    // Fold each session found that has no valid alive file.
    str<280> path;
    get_file_path(path, false);
    path << "_*";

    for (globber i(path.c_str()); i.next(path);)
    {
        path << "~";
        if (os::get_path_type(path.c_str()) == os::path_type_file)
            if (!os::unlink(path.c_str())) // abandoned alive files will unlink
                continue;

        path.truncate(path.length() - 1);

        int file_size = os::get_file_size(path.c_str());
        if (file_size > 0)
        {
            void* src_handle = open_file(path.c_str());
            {
                read_lock src(src_handle);
                write_lock dest(m_bank_handles[bank_master]);
                if (src && dest)
                    dest.append(src);
            }
            CloseHandle(src_handle);
        }

        os::unlink(path.c_str());
    }
}

//------------------------------------------------------------------------------
void history_db::initialise()
{
    if (m_bank_handles[bank_master] != nullptr)
        return;

    str<280> path;
    get_file_path(path, false);
    m_bank_handles[bank_master] = open_file(path.c_str());

    if (g_shared.get())
        return;

    get_file_path(path, true);
    m_bank_handles[bank_session] = open_file(path.c_str());

    reap(); // collects orphaned history files.
}

//------------------------------------------------------------------------------
unsigned int history_db::get_bank_count() const
{
    int count = 0;
    for (void* handle : m_bank_handles)
        count += (handle != nullptr);

    return count;
}

//------------------------------------------------------------------------------
void* history_db::get_bank(unsigned int index) const
{
    if (index >= get_bank_count())
        return nullptr;

    return m_bank_handles[index];
}

//------------------------------------------------------------------------------
template <typename T> void history_db::for_each_bank(T&& callback)
{
    for (int i = 0, n = get_bank_count(); i < n; ++i)
    {
        write_lock lock(get_bank(i));
        if (lock && !callback(i, lock))
            break;
    }
}

//------------------------------------------------------------------------------
template <typename T> void history_db::for_each_bank(T&& callback) const
{
    for (int i = 0, n = get_bank_count(); i < n; ++i)
    {
        read_lock lock(get_bank(i));
        if (lock && !callback(i, lock))
            break;
    }
}

//------------------------------------------------------------------------------
void history_db::load_rl_history()
{
    clear_history();

    char buffer[max_line_length + 1];

    const history_db& const_this = *this;
    const_this.for_each_bank([&] (unsigned int, const read_lock& lock)
    {
        str_iter out;
        read_lock::line_iter iter(lock, buffer, sizeof_array(buffer) - 1);
        while (iter.next(out))
        {
            const char* line = out.get_pointer();
            int buffer_offset = int(line - buffer);
            buffer[buffer_offset + out.length()] = '\0';
            add_history(line);
        }

        return true;
    });
}

//------------------------------------------------------------------------------
void history_db::clear()
{
    for_each_bank([] (unsigned int, write_lock& lock)
    {
        lock.clear();
        return true;
    });
}

//------------------------------------------------------------------------------
bool history_db::add(const char* line)
{
    // Ignore empty and/or whitespace prefixed lines?
    if (!line[0] || (g_ignore_space.get() && (line[0] == ' ' || line[0] == '\t')))
        return false;

    // Handle duplicates.
    if (int dupe_mode = g_dupe_mode.get())
    {
        if (line_id find_result = find(line))
        {
            if (dupe_mode == 1)
                return true;
            else
                remove(find_result);
        }
    }

    // Add the line.
    void* handle = get_bank(get_bank_count() - 1);
    write_lock lock(handle);
    if (!lock)
        return false;

    lock.add(line);
    return true;
}

//------------------------------------------------------------------------------
bool history_db::remove(line_id id)
{
    if (!id)
        return false;

    line_id_impl id_impl;
    id_impl.outer = id;

    void* handle = get_bank(id_impl.bank_index);
    write_lock lock(handle);
    if (!lock)
        return false;

    lock.remove(id_impl);
    return true;
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
history_db::expand_result history_db::expand(const char* line, str_base& out) const
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
