// Copyright (c) 2017 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str_iter.h>
#include <core/singleton.h>

#include <vector>

//------------------------------------------------------------------------------
class concurrency_tag
{
public:
    void            generate_new_tag();
    void            clear() { m_tag.clear(); }

    bool            empty() const { return m_tag.empty(); }
    const char*     get() const { return m_tag.c_str(); }
    void            set(const char* tag);

    unsigned int    size() const { return m_tag.length() + 1; }

private:
    str<64,false>   m_tag;
};

//------------------------------------------------------------------------------
enum : char
{
    bank_none       = -1,
    bank_master,
    bank_session,
    bank_count,
};

//------------------------------------------------------------------------------
struct bank_handles
{
                    bank_handles() = default;
    void            close();
    explicit        operator bool () const;
    void*           m_handle_lines = nullptr;
    void*           m_handle_removals = nullptr;
};

//------------------------------------------------------------------------------
class history_read_buffer
{
    // CMD input buffer size is 8192 UTF16 WCHARs.
    // ReadConsole disallows more input than fits in the input buffer.
    //
    // However, Readline allows infinite input of UTF8 chars.
    // So with Clink/Readline longer input can be silently truncated on
    // returning from ReadConsole, but still gets into the history as the full
    // length.
    //
    // BUGBUG:  Long term, ideally Clink should resolve the truncation problem.
    // However, even CMD silently fails to run an inputted command longer than
    // 8100 characters, despite allowing 8191 characters to be input.
    //
    // Short term, Clink will use a large internal buffer so that valid length
    // lines should never be split in the history (I'm comfortable ignoring the
    // pathological case where the UTF16 string expands to the maximum possible
    // UTF8 string size due to repeating the same max-byte-encoding characters
    // over and over).
    //
    // The buffer size is intentionally smaller than 65536 to ensure the buffer
    // fits in one Natural Page regardless of the underlying memory allocator's
    // internal design (control structures embedded as prologue/epiloque in the
    // memory block, or control structures stored separately in memory).
    static const unsigned int buffer_size = 64000;

public:
    history_read_buffer() : m_buffer((char*)malloc(buffer_size)) {}
    ~history_read_buffer() { free(m_buffer); }

    char*           data() { return m_buffer; }
    unsigned int    size() const { return buffer_size; }

private:
    char*           m_buffer;
};

//------------------------------------------------------------------------------
class history_db
{
    friend struct test_history_db;

public:
    enum expand_result
    {
        // values match Readline's history_expand() return value.
        expand_error            = -1,
        expand_none             = 0,
        expand_ok               = 1,
        expand_print            = 2,
    };

    typedef unsigned int        line_id;

    class iter
    {
    public:
                                iter(iter&& other);
                                ~iter();
        line_id                 next(str_iter& out, str_base* timestamp=nullptr);
        unsigned int            get_bank() const;

    private:
                                iter() = default;
        friend                  history_db;
        uintptr_t               impl = 0;
    };

                                history_db(const char* path, int id, bool use_master_bank);
                                ~history_db();
    void                        initialise(str_base* error_message=nullptr);
    void                        load_rl_history(bool can_clean=true);
    void                        clear();
    void                        compact(bool force=false, bool uniq=false, int limit=-1);
    bool                        add(const char* line);
    int                         remove(const char* line);
    bool                        remove(line_id id) { return remove_internal(id, true); }
    bool                        remove(int rl_history_index, const char* line);
    line_id                     find(const char* line) const;
    template <int S> iter       read_lines(char (&buffer)[S]);
    iter                        read_lines(char* buffer, unsigned int buffer_size);

    void                        enable_diagnostic_output() { m_diagnostic = true; }
    bool                        has_bank(unsigned char bank) const;
    bool                        is_stale_name() const;

    static expand_result        expand(const char* line, str_base& out);

private:
    friend                      class read_line_iter;
    bool                        is_valid() const;
    void                        get_file_path(str_base& out, bool session) const;
    void                        load_internal();
    void                        reap();
    template <typename T> void  for_each_bank(T&& callback);
    template <typename T> void  for_each_bank(T&& callback) const;
    template <typename T> void  for_each_session(T&& callback) const;
    unsigned int                get_active_bank() const;
    bank_handles                get_bank(unsigned int index) const;
    bool                        remove_internal(line_id id, bool guard_ctag);
    void                        make_open_error(str_base* error_message, unsigned char bank) const;
    void*                       m_alive_file = nullptr;
    str_moveable                m_path;
    int                         m_id;
    bank_handles                m_bank_handles[bank_count];
    str<32>                     m_bank_filenames[bank_count];
    DWORD                       m_bank_error[bank_count];
    concurrency_tag             m_master_ctag;
    std::vector<line_id>        m_index_map;
    size_t                      m_master_len;
    size_t                      m_master_deleted_count;

    size_t                      m_min_compact_threshold = 200;

    bool                        m_use_master_bank = false;
    bool                        m_diagnostic = false;
};

//------------------------------------------------------------------------------
template <int S> history_db::iter history_db::read_lines(char (&buffer)[S])
{
    return read_lines(buffer, S);
}

//------------------------------------------------------------------------------
class history_database : public history_db, public singleton<history_database>
{
public:
    history_database(const char* path, int id, bool use_master_bank);
};

#define DIAG(fmt, ...)          do { if (m_diagnostic) fprintf(stderr, fmt, ##__VA_ARGS__); } while (false)
