#include "pch.h"
#include "rl/rl_line_editor.h"

#include <core/array.h>
#include <core/base.h>
#include <core/singleton.h>
#include <core/str_compare.h>
#include <core/str_tokeniser.h>
#include <line_state.h>
#include <matches/column_printer.h>
#include <matches/match_pipeline.h>
#include <matches/matches.h>
#include <terminal/win_terminal.h>

void draw_matches(const matches&);
#if 1
#define log(fmt, ...) printf("\n" fmt, __VA_ARGS__)
#else
#define log(fmt, ...)
#endif



class editor_buffer;
class editor_backend;

//------------------------------------------------------------------------------
class key_bind_resolver
{
public:
    void            reset()                   { new (this) key_bind_resolver(); }
    bool            is_resolved() const       { return m_backend != nullptr; }
    void            set_resolve_id(int id)    { m_resolve_id = id; }
    editor_backend* get_backend() const       { return m_backend; }
    int             get_resolve_id() const    { return m_resolve_id; }

private:
    friend class    key_binder;
    int             get_node_index() const { return m_node_index; }
    void            set_node_index(int index) { m_node_index = index; }
    void            resolve(editor_backend* backend, int id)
    {
        reset();
        if (backend != nullptr)
        {
            m_backend = backend;
            m_resolve_id = id;
        }
    }

private:
    editor_backend* m_backend = nullptr;
    int             m_resolve_id = -1;
    int             m_node_index = -1;
};

//------------------------------------------------------------------------------
class key_binder
{
public:
    key_binder()
    : m_root({})
    , m_default_backend(-1)
    , m_next_node(0)
    {
    }

    void set_default_backend(editor_backend* backend)
    {
        m_default_backend = add_backend(backend);
    }

    bool bind(const char* chord, editor_backend* backend, unsigned char id)
    {
        // Validate input
        const char* c = chord;
        while (*c)
            if (*c++ < 0)
                return false;

        // Store the backend pointer
        int index = add_backend(backend);
        if (index < 0)
            return false;

        // Add the chord of keys into the node graph.
        node* parent = get_root();
        for (; *chord && parent != nullptr; ++chord)
            parent = insert_child(parent, *chord);

        if (parent == nullptr || parent->usage)
            return false;

        node new_parent = *parent;
        new_parent.usage = node_use_bound;
        new_parent.backend = index;
        new_parent.id_or_child = id;

        *parent = new_parent;
        return true;
    }

    void update_resolver(unsigned char key, key_bind_resolver& resolver)
    {
        if (resolver.is_resolved())
            resolver.reset();

        int node_index = resolver.get_node_index();
        node* current = (node_index >= 0) ? get_node(node_index) : get_root();

        if (node* next = find_child(current, key))
        {
            // More tree to follow?
            if (next->usage == node_use_parent)
                resolver.set_node_index(next - m_nodes);

            // Key binding found?
            else if (next->usage == node_use_bound)
                resolver.resolve(get_backend(next->backend), next->id_or_child);

            return;
        }

        // Unbound, or something went wrong...
        resolver.resolve(get_backend(m_default_backend), -1);
    }

private:
    enum
    {
        node_use_none,
        node_use_bound,
        node_use_parent,
        // node.usage.bits
    };

    struct node
    {
        unsigned int key            : 7;
        unsigned int sibling        : 7;
        unsigned int usage          : 2;
        unsigned int id_or_child    : 8;
        unsigned int backend        : 4;
        unsigned int                : 4;
    };

    static const unsigned int sentinal = 0x7f;
    typedef fixed_array<editor_backend*, 16> backends; // node.backend.bits - 1

    node* find_child(node* parent, unsigned char key)
    {
        node* child = nullptr;
        if (parent->usage == node_use_parent)
            child = get_node(parent->id_or_child);

        for (; child != nullptr; child = get_node(child->sibling))
            if (child->key == key)
                return child;

        return nullptr;
    }

    node* insert_child(node* parent, unsigned char key)
    {
        node* child = find_child(parent, key);
        return (child != nullptr) ? child : add_child(parent, key);
    }

    node* add_child(node* parent, unsigned char key)
    {
        int index = alloc_node();
        if (index == sentinal)
            return nullptr;

        node new_child = {};
        new_child.key = key;
        new_child.sibling = (parent->usage == node_use_parent) ? parent->id_or_child : sentinal;

        node* child = get_node(index);
        *child = new_child;

        parent->usage = node_use_parent;
        parent->id_or_child = index;

        return child;
    }

    int add_backend(editor_backend* backend)
    {
        for (int i = 0, n = m_backends.size(); i < n; ++i)
            if (*(m_backends[i]) == backend)
                return i;

        editor_backend** slot = m_backends.push_back();
        if (slot == nullptr)
            return -1;

        *slot = backend;
        return slot - m_backends.front();
    }

    node*       get_root()                   { return &m_root; }
    node*       get_node(unsigned int index) { return (index < sentinal) ? m_nodes + index : nullptr; }
    int         alloc_node()                 { return (m_next_node < sizeof_array(m_nodes)) ? m_next_node++ : sentinal; }
    editor_backend* get_backend(unsigned int index) const { auto b = m_backends[index]; return b ? *b : nullptr; }
    backends    m_backends;
    node        m_root;
    node        m_nodes[127]; // node.sibling.bits - 1
    int         m_default_backend;
    char        m_next_node;
};



//------------------------------------------------------------------------------
class editor_backend
{
public:
    struct context
    {
        terminal&       terminal;
        editor_buffer&  buffer;
        const matches&  matches;
    };

    virtual void        bind(key_binder& binder) = 0;
    virtual void        begin_line() = 0;
    virtual void        end_line() = 0;
    virtual int         on_input(int id, const char* keys, const context& context) = 0;
};



//------------------------------------------------------------------------------
class editor_buffer
{
public:
    virtual const char* get_buffer() const = 0;
    virtual int         get_cursor_pos() const = 0;
};



//------------------------------------------------------------------------------
class classic_match_ui
    : public editor_backend
{
public:
    virtual void bind(key_binder& binder) override
    {
        binder.bind("\t", this, 1);
    }

    virtual int on_input(int id, const char* keys, const context& context) override
    {
        auto& terminal = context.terminal;
        auto& matches = context.matches;

        unsigned int key = matches.get_match_key();
        if (key != m_prev_key)
        {
            m_waiting = false;
            m_prev_key = key;
        }

        if (m_waiting)
        {
            column_printer p(&terminal);
            puts("");
            p.print(matches);

            return -2;
        }

        // One match? Accept it.
        if (matches.get_match_count() == 1)
        {
            log("accept; %s", matches.get_match(0));
            return -2;
        }

        // Valid LCD? Append it.
        str<> lcd;
        matches.get_match_lcd(lcd);
        if (lcd.length())
        {
            log("append; %s", lcd.c_str());
            return -2;
        }

        m_waiting = true;
        return -2;
    }

    virtual void        begin_line() override {}
    virtual void        end_line() override {}

private:
    bool                m_waiting = false;
    unsigned int        m_prev_key = ~0u;
};



//------------------------------------------------------------------------------
class rl_backend
    : public editor_backend
    , public editor_buffer
    , public singleton<rl_backend>
{
public:
    virtual void bind(key_binder& binder) override
    {
    }

    virtual void begin_line() override
    {
        auto handler = [] (char* line) { rl_backend::get()->done(line); };
        rl_callback_handler_install("testbed $ ", handler);

        m_done = false;
        m_eof = false;
    }

    virtual void end_line() override
    {
        rl_callback_handler_remove();
    }

    virtual int on_input(int id, const char* keys, const context& context) override
    {
        if (id != -1)
            return 0;

        // MODE4
        static struct : public terminal_in
        {
            virtual void select() {}
            virtual int read() { return *(unsigned char*)(data++); }
            const char* data; 
        } term_in;
        term_in.data = keys;
        rl_instream = (FILE*)(&term_in);

        while (*term_in.data)
        // MODE4

        rl_callback_read_char();

        int rl_state = rl_readline_state;
        rl_state &= ~RL_STATE_CALLBACK;
        rl_state &= ~RL_STATE_INITIALIZED;
        rl_state &= ~RL_STATE_OVERWRITE;
        rl_state &= ~RL_STATE_VICMDONCE;

        if (m_done)
            return -3;

        return (rl_state ? -1 : -2);
    }

    virtual const char* get_buffer() const override
    {
        return (m_eof ? nullptr : rl_line_buffer);
    }

    virtual int get_cursor_pos() const override
    {
        return rl_point;
    }

private:
    void done(const char* line)
    {
        m_done = true;
        m_eof = (line == nullptr);
    }

    bool m_done = false;
    bool m_eof = false;
};



//------------------------------------------------------------------------------
class line_editor_2
{
public:
    struct desc
    {
        const char*     quote_char;
        const char*     word_delims;
        const char*     partial_delims;
        terminal*       terminal;
        editor_backend* backend;
        editor_buffer*  buffer;
    };

                        line_editor_2(const desc& desc);
    bool                add_backend(editor_backend* backend);
    match_system&       get_match_system();
    bool                get_line(char* out, int out_size);
    bool                edit(char* out, int out_size);
    bool                update();

private:
    typedef fixed_array<editor_backend*, 16> backends;

    void                initialise();
    void                begin_line();
    void                end_line();
    void                collect_words(array_base<word>& words) const;
    void                update_internal();
    void                record_input(unsigned char key);
    void                dispatch();
    char                m_keys[8];
    desc                m_desc;
    match_system        m_match_system; // MODE4 : poor, remove!
    backends            m_backends;
    matches             m_matches;
    key_binder          m_key_binder;
    key_bind_resolver   m_key_bind_resolver;
    unsigned char       m_keys_size;
    bool                m_begun;
    bool                m_initialised;
};

//------------------------------------------------------------------------------
line_editor_2::line_editor_2(const desc& desc)
: m_desc(desc)
, m_initialised(false)
, m_begun(false)
{
    add_backend(m_desc.backend);
    m_key_binder.set_default_backend(m_desc.backend);
}

//------------------------------------------------------------------------------
void line_editor_2::initialise()
{
    if (m_initialised)
        return;

    for (auto backend : m_backends)
        backend->bind(m_key_binder);

    m_initialised = true;
}

//------------------------------------------------------------------------------
void line_editor_2::begin_line()
{
    m_begun = true;

    m_key_bind_resolver.reset();
    m_keys_size = 0;

    match_pipeline pipeline(m_match_system, m_matches);
    pipeline.reset();

    m_desc.terminal->begin();

    for (auto backend : m_backends)
        backend->begin_line();
}

//------------------------------------------------------------------------------
void line_editor_2::end_line()
{
    for (auto backend : m_backends)
        backend->end_line();

    m_desc.terminal->end();

    m_begun = false;
}

//------------------------------------------------------------------------------
bool line_editor_2::add_backend(editor_backend* backend)
{
    editor_backend** slot = m_backends.push_back();
    return (slot != nullptr) ? *slot = backend, true : false;
}

//------------------------------------------------------------------------------
match_system& line_editor_2::get_match_system()
{
    return m_match_system;
}

//------------------------------------------------------------------------------
bool line_editor_2::get_line(char* out, int out_size)
{
    if (m_begun)
        end_line();

    if (const char* line = m_desc.buffer->get_buffer())
    {
        str_base(out, out_size).copy(line);
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
bool line_editor_2::edit(char* out, int out_size)
{
    // Update first so the init state goes through.
    while (update())
        m_desc.terminal->select();

    return get_line(out, out_size);
}

//------------------------------------------------------------------------------
bool line_editor_2::update()
{
    if (!m_initialised)
        initialise();

    if (!m_begun)
    {
        begin_line();
        update_internal();
        return true;
    }

    int key = m_desc.terminal->read();
    record_input(key);

    if (!m_key_bind_resolver.is_resolved())
        m_key_binder.update_resolver(key, m_key_bind_resolver);

    dispatch();

    if (!m_begun)
        return false;

    if (!m_key_bind_resolver.is_resolved())
        update_internal();

    return true;
}

//------------------------------------------------------------------------------
void line_editor_2::record_input(unsigned char key)
{
    if (m_keys_size < sizeof_array(m_keys) - 1)
        m_keys[m_keys_size++] = key;
}

//------------------------------------------------------------------------------
void line_editor_2::dispatch()
{
    if (!m_key_bind_resolver.is_resolved())
        return;

    editor_backend::context context = {
        *m_desc.terminal,
        *m_desc.buffer,
        m_matches,
    };

    editor_backend* backend = m_key_bind_resolver.get_backend();
    int id = m_key_bind_resolver.get_resolve_id();

    m_keys[m_keys_size] = '\0';
    int result = backend->on_input(id, m_keys, context);
    m_keys_size = 0;

    // MODE4 : magic numbers!
    if (result < -1)
        if (result < -2)
            end_line();
        else
            m_key_bind_resolver.reset();
    else
        m_key_bind_resolver.set_resolve_id(result);
}

//------------------------------------------------------------------------------
void line_editor_2::collect_words(array_base<word>& words) const
{
    const char* line_buffer = m_desc.buffer->get_buffer();
    const int line_cursor = m_desc.buffer->get_cursor_pos();

    str_iter token_iter(line_buffer, line_cursor);
    str_tokeniser tokens(token_iter, m_desc.word_delims);
    tokens.add_quote_pair(m_desc.quote_char);
    while (1)
    {
        const char* start = nullptr;
        int length = 0;
        if (!tokens.next(start, length))
            break;

        // Add the word.
        words.push_back();
        *(words.back()) = { short(start - line_buffer), length };

        // Find the best-fit delimiter.
        /* MODE4
        const char* best_delim = word_delims;
        const char* c = words
        while (c > line_buffer)
        {
            const char* delim = strchr(word_delims, *c);
            if (delim == nullptr)
                break;

            best_delim = max(delim, best_delim);
            --c;
        }
        word->delim = *best_delim;
        MODE4 */
    }

    // Add an empty word if the cursor is at the beginning of one.
    word* end_word = words.back();
    if (!end_word || end_word->offset + end_word->length < line_cursor)
    {
        words.push_back();
        *(words.back()) = { line_cursor };
    }

    // Adjust for quotes.
    for (word& word : words)
    {
        const char* start = line_buffer + word.offset;

        int start_quoted = (start[0] == m_desc.quote_char[0]);
        int end_quoted = 0;
        if (word.length > 1)
            end_quoted = (start[word.length - 1] == m_desc.quote_char[0]);

        word.offset += start_quoted;
        word.length -= start_quoted + end_quoted;
    }

    // Adjust the completing word for partiality.
    end_word = words.back();
    int partial = 0;
    for (int j = end_word->length - 1; j >= 0; --j)
    {
        int c = line_buffer[end_word->offset + j];
        if (strchr(m_desc.partial_delims, c) == nullptr)
            continue;

        partial = j + 1;
        break;
    }
    end_word->length = partial;

    // MODE4
    int j = 0;
    puts("");
    for (auto word : words)
        printf("%02d:%02d,%02d ", j++, word.offset, word.length);
    puts("");
    // MODE4
}

//------------------------------------------------------------------------------
void line_editor_2::update_internal()
{
    // Collect words.
    fixed_array<word, 128> words;
    collect_words(words);
    const word& end_word = *(words.back());

    union key_t {
        struct {
            unsigned int word_offset : 11;
            unsigned int word_length : 10;
            unsigned int cursor_pos  : 11;
        };
        unsigned int value;
    };

    key_t next_key = { end_word.offset, end_word.length };

    key_t prev_key;
    prev_key.value = m_matches.get_match_key();
    prev_key.cursor_pos = 0;

    // Should we generate new matches?
    if (next_key.value != prev_key.value)
    {
        match_pipeline pipeline(m_match_system, m_matches);
        pipeline.reset();
        pipeline.generate({ words, m_desc.buffer->get_buffer() });

        log("generate: %d\n", m_matches.get_match_count());
    }

    next_key.cursor_pos = m_desc.buffer->get_cursor_pos();
    prev_key.value = m_matches.get_match_key();

    // Should we sort and select matches?
    if (next_key.value != prev_key.value)
    {
        str<64> needle;
        int needle_start = end_word.offset + end_word.length;
        const char* line = m_desc.buffer->get_buffer();
        needle.concat(line + needle_start, next_key.cursor_pos - needle_start);

        match_pipeline pipeline(m_match_system, m_matches);
        pipeline.select("normal", needle.c_str());
        pipeline.sort("alpha");
        pipeline.finalise(next_key.value);

        log("select & sort: '%s'\n", needle.c_str());
    }

    /* MODE4 */ draw_matches(m_matches);
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void draw_matches(const matches& result)
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(handle, &csbi);

    COORD cur = { csbi.srWindow.Left, csbi.srWindow.Top };
    SetConsoleCursorPosition(handle, cur);
    SetConsoleTextAttribute(handle, 0x70);

    for (int i = 0, n = result.get_match_count(); i < 35; ++i)
    {
        const char* match = "";
        if (i < n)
            match = result.get_match(i);

        printf("%02d : %48s\n", i, match);
    }

    SetConsoleTextAttribute(handle, csbi.wAttributes);

    rl_forced_update_display();
}

int testbed(int, char**)
{
    str_compare_scope _(str_compare_scope::relaxed);

    win_terminal terminal;

    // MODE4
    column_printer printer(&terminal);
    line_editor::desc _desc = { "testbed", &terminal, &printer };
    auto* line_editor = create_rl_line_editor(_desc);
    // MODE4

    classic_match_ui ui;
    rl_backend backend;

    line_editor_2::desc desc = {};
    desc.quote_char = "\"";
    desc.word_delims = " \t=";
    desc.partial_delims = "\\/:";
    desc.terminal = &terminal;
    desc.backend = &backend;
    desc.buffer = &backend;
    line_editor_2 editor(desc);
    editor.add_backend(&ui);

    // MODE4 : different API!
    match_system& system = editor.get_match_system();
    system.add_generator(0, file_match_generator());
    system.add_selector("normal", normal_match_selector());
    system.add_sorter("alpha", alpha_match_sorter());

    char out[64];
    editor.edit(out, sizeof_array(out));
    editor.edit(out, sizeof_array(out));

    return 0;
}
