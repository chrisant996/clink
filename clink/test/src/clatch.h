// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include <stdio.h>
#include <stdlib.h>
#include <exception>
#include <functional>

#include <core/os.h>

namespace clatch {

//------------------------------------------------------------------------------
struct colors
{
    static void initialize();
    static bool get_colored() { return *get_colored_storage(); }
    static const char* get_ok() { return get_colored() ? "\x1b[92m" : ""; }
    static const char* get_error() { return get_colored() ? "\x1b[91m" : ""; }
    static const char* get_warning() { return get_colored() ? "\x1b[93m" : ""; }
    static const char* get_normal() { return get_colored() ? "\x1b[m" : ""; }

private:
    static bool* get_colored_storage() { static bool s_colored = false; return &s_colored; }
};

//------------------------------------------------------------------------------
struct section
{
    void add_child(section* child) {
        child->m_parent = this;
        if (section* tail = m_child)
        {
            for (; tail->m_sibling != nullptr; tail = tail->m_sibling);
            tail->m_sibling = child;
        }
        else
            m_child = child;
    }

    void enter(section*& tree_iter, const char* name) {
        m_name = name;

        if (m_parent == nullptr)
            if (section* parent = get_outer_store())
                parent->add_child(this);

        for (; tree_iter->m_child != nullptr; tree_iter = tree_iter->m_child)
            tree_iter->m_active = true;
        tree_iter->m_active = true;

        get_outer_store() = this;
    }

    static section*&    get_outer_store() { static section* section; return section; }
    void                leave() { get_outer_store() = m_parent; }
    explicit            operator bool () { return m_active; }
    const char*         m_name = "";
    section*            m_parent = nullptr;
    section*            m_child = nullptr;
    section*            m_sibling = nullptr;
    uint32              m_assert_count = 0;
    bool                m_active = false;

    struct scope
    {
                        scope(section*& tree_iter, section& section, const char* name) : m_section(&section) { m_section->enter(tree_iter, name); }
                        ~scope() { m_section->leave(); }
        explicit        operator bool () { return bool(*m_section); }
        section*        m_section;
    };
};

//------------------------------------------------------------------------------
struct test
{
    typedef void        (test_func)(section*&);
    static test*&       get_head() { static test* head; return head; }
    static test*&       get_tail() { static test* tail; return tail; }
    test*               m_next = nullptr;
    test_func*          m_func;
    const char*         m_name;

    test(const char* name, test_func* func)
    : m_func(func)
    , m_name(name)
    {
        if (get_head() == nullptr)
            get_head() = this;

        if (test* tail = get_tail())
            tail->m_next = this;
        get_tail() = this;
    }
};

//------------------------------------------------------------------------------
struct cleanup
{
                        cleanup(std::function<void(void)>&& func) : m_func(std::move(func)) {}
                        ~cleanup() { if (m_func) m_func(); }
    std::function<void(void)> m_func;
};

//------------------------------------------------------------------------------
inline void list()
{
    for (test* test = test::get_head(); test != nullptr; test = test->m_next)
        puts(test->m_name);
}

//------------------------------------------------------------------------------
inline bool run(const char* prefix="", bool times=false)
{
    int32 fail_count = 0;
    int32 test_count = 0;
    int32 assert_count = 0;

    for (test* test = test::get_head(); test != nullptr; test = test->m_next)
    {
        // Cheap lower-case prefix test.
        const char* a = prefix, *b = test->m_name;
        for (; *a && (*a & ~0x20) == (*b & ~0x20); ++a, ++b);
        if (*a)
            continue;

        ++test_count;
        printf(".........%s %s", times ? "........" : "", test->m_name);

        section root;
        const double clock = os::clock();

        try
        {
            section* tree_iter = &root;
            do
            {
                section::scope x = section::scope(tree_iter, root, test->m_name);

                (test->m_func)(tree_iter);

                for (section* parent; parent = tree_iter->m_parent; tree_iter = parent)
                {
                    tree_iter->m_active = false;
                    if (tree_iter = tree_iter->m_sibling)
                        break;
                }
            }
            while (tree_iter != &root);
        }
        catch (...)
        {
            ++fail_count;
            assert_count += root.m_assert_count;
            continue;
        }

        assert_count += root.m_assert_count;
        printf("\r%sok%s ", colors::get_ok(), colors::get_normal());
        if (times)
        {
            const uint32 elapsed = uint32((os::clock() - clock) * 1000);
            const char* time_color = (elapsed >= 500) ? colors::get_warning() : colors:: get_normal();
            printf("%s%5u ms%s ", time_color, elapsed, colors::get_normal());
        }
        printf("\n");
    }

    const char* tests_color = fail_count ?  colors::get_normal() : colors::get_ok();
    const char* failed_color = fail_count ? colors::get_error() : colors::get_normal();
    printf("\n %stests:%d%s  %sfailed:%d%s  asserts:%d\n",
           tests_color, test_count, colors::get_normal(),
           failed_color, fail_count, colors::get_normal(),
           assert_count);

    return (fail_count == 0);
}

//------------------------------------------------------------------------------
inline void fail(const char* expr, const char* file, int32 line)
{
    section* failed_section = clatch::section::get_outer_store();

    puts("\n");
    printf(colors::get_error());
    printf(" expr; %s\n", expr);
    printf("where; %s(%d)\n", file, line);
    printf("trace; ");
    for (; failed_section != nullptr; failed_section = failed_section->m_parent)
        printf("%s\n       ", failed_section->m_name);
    printf(colors::get_normal());
    puts("");

    throw std::exception();
}

//------------------------------------------------------------------------------
template <typename callback>
void fail(const char* expr, const char* file, int32 line, callback&& cb)
{
    puts("\n");
    printf(colors::get_error());
    cb();
    printf(colors::get_normal());
    fail(expr, file, line);
}

} // namespace clatch

//------------------------------------------------------------------------------
#define CLATCH_IDENT__(d, b) _clatch_##d##_##b
#define CLATCH_IDENT_(d, b)  CLATCH_IDENT__(d, b)
#define CLATCH_IDENT(d)      CLATCH_IDENT_(d, __LINE__)

#define TEST_CASE(name)\
    static void CLATCH_IDENT(test_func)(clatch::section*&);\
    static clatch::test CLATCH_IDENT(test)(name, CLATCH_IDENT(test_func));\
    static void CLATCH_IDENT(test_func)(clatch::section*& _clatch_tree_iter)

#define SECTION(name)\
    static clatch::section CLATCH_IDENT(section);\
    if (clatch::section::scope CLATCH_IDENT(scope) = clatch::section::scope(_clatch_tree_iter, CLATCH_IDENT(section), name))

#define MAKE_CLEANUP(lambda)\
    clatch::cleanup CLATCH_IDENT(cleanup)(std::move(lambda));


#define SECTIONNAME()\
    clatch::section::get_outer_store()->m_name

#define REQUIRE(expr, ...)\
    do {\
        auto* _clatch_s = clatch::section::get_outer_store();\
        for (; _clatch_s->m_parent != nullptr; _clatch_s = _clatch_s->m_parent);\
        ++_clatch_s->m_assert_count;\
        \
        if (!(expr))\
            clatch::fail(#expr, __FILE__, __LINE__, ##__VA_ARGS__);\
    } while (0)

#define REQUIRE_LUA_DO_STRING(lua, script)\
    do {\
        str<> errmsg;\
        REQUIRE(lua.do_string(script, -1, &errmsg), [&]() {\
            puts(errmsg.length() ? errmsg.c_str() : "<no error message>");\
        });\
    } while (0)
