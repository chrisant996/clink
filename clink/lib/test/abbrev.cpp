// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/os.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>

extern "C" {
#include <lua.h>
}

//------------------------------------------------------------------------------
struct testcase_abbrev
{
    const char* in;
    const char* expected;
    bool all;
    bool git;
};

//------------------------------------------------------------------------------
static bool verify_abbrev(lua_state& lua, const testcase_abbrev& t, str_base& out, bool transform)
{
    lua_State *state = lua.get_state();

    str<> msg;
    if (!lua.push_named_function(state, "os.abbreviatepath", &msg))
    {
put_msg:
        puts("");
        puts(msg.c_str());
        REQUIRE(false);
        return false;
    }

    lua_pushstring(state, t.in);

    const char* decide_name = nullptr;
    if (t.all && t.git)
        decide_name = "abbrev_all_git";
    else if (t.all)
        decide_name = "abbrev_all";
    else if (t.git)
        decide_name = "abbrev_git";
    if (!decide_name)
        lua_pushnil(state);
    else if (!lua.push_named_function(state, decide_name, &msg))
        goto put_msg;

    if (transform && !lua.push_named_function(state, "abbrev_transform", &msg))
        goto put_msg;

    bool success = (lua.pcall_silent(transform ? 3 : 2, 1) == LUA_OK);
    if (!success)
    {
        if (const char* error = lua_tostring(state, -1))
        {
            puts("");
            puts("error executing function 'os.abbreviatepath':");
            puts(error);
        }
        REQUIRE(false);
        return false;
    }

    const char* result = lua_tostring(state, -1);
    if (!result)
    {
        out.clear();
        REQUIRE(false);
        return false;
    }

    str<> expected;
    if (transform)
        expected = t.expected;
    else
    {
        const size_t len = strlen(t.expected);
        for (size_t ii = 0; ii < len; ++ii)
        {
            if (t.expected[ii] != '<' && t.expected[ii] != '>')
                expected.concat(t.expected + ii, 1);
        }
    }

    out = result;
    REQUIRE(strcmp(out.c_str(), expected.c_str()) == 0, [&] () {
        printf("      in:  \"%s\"\n     out:  \"%s\"\nexpected:  \"%s\"",
                t.in, out.c_str(), expected.c_str());
    });

    return true;
}

//------------------------------------------------------------------------------
TEST_CASE("Abbreviated paths.")
{
    static const char* dir_fs[] = {
        "xyz/bag/foo/leaf",
        "xyz/bookkeeper/foo/leaf",
        "xyz/bookkeeping/foo/leaf",
        "xyz/box/foo/leaf",
        "xyz/boxes/foo/leaf",
        "xyz/repo/.git/leaf",
        "xyz/repo/foo/leaf",
        "xyz/notrepo/foo/leaf",
        nullptr,
    };

    fs_fixture fs(dir_fs);

    lua_state lua;
    line_editor_tester tester;

    str<> tmp;
    str<> out;
    SECTION("Expand")
    {
        struct testcase
        {
            const char* in;
            const char* expanded;
            const char* remaining;
            bool unique;
        };

        static const testcase c_testcases[] =
        {
            { "x/b/",                   "xyz\\b", "/", false },
            { "x/boo/l",                "xyz\\bookkeep", "/l", false },
            { "x/box/l",                "xyz\\box", "/l", true },
            { "x/boxe/l",               "xyz\\boxes", "/l", true },
            { "x/r/l",                  "xyz\\repo", "/l", true },
            { "xy/no/x",                "xyz\\notrepo", "/x", true },
            { "x/bag/leaf/x",           "", "x/bag/leaf/x", false },
        };

        SECTION("Relative")
        {
            for (auto const& t : c_testcases)
            {
                const char* in = t.in;
                const bool unique = os::disambiguate_abbreviated_path(in, out);
                const bool ok = (unique == t.unique &&
                                 strcmp(in, t.remaining) == 0 &&
                                 out.equals(t.expanded));

                REQUIRE(ok, [&] () {
                    printf("      in:  \"%s\"\n     out:  \"%s\", \"%s\", %s\nexpected:  \"%s\", \"%s\", %s",
                           t.in,
                           out.c_str(), in, unique ? "unique" : "ambiguous",
                           t.expanded, t.remaining, t.unique ? "unique" : "ambiguous");
                });

            }
        }

        for (int pass = 2; pass--;)
        {
            SECTION(pass ? "Absolute" : "Drive")
            {
                str<> cwd;
                os::get_current_dir(cwd);

                if (pass)
                {
                    str<> drive;
                    path::get_drive(cwd.c_str(), drive);
                    cwd = drive.c_str();
                }

                str<> expect;
                for (auto const& t : c_testcases)
                {
                    path::join(cwd.c_str(), t.in, tmp);
                    if (*t.expanded)
                        path::join(cwd.c_str(), t.expanded, expect);
                    else
                        expect.clear();

                    const char* in = tmp.c_str();
                    const bool unique = os::disambiguate_abbreviated_path(in, out);
                    const char* remaining = *t.expanded ? tmp.c_str() + tmp.length() - strlen(t.remaining) : tmp.c_str();
                    const bool ok = (unique == t.unique &&
                                    strcmp(in, remaining) == 0 &&
                                    out.equals(expect.c_str()));

                    REQUIRE(ok, [&] () {
                        printf("     in:  \"%s\"\n    out:  \"%s\", \"%s\", %s\nexpected:  \"%s\", \"%s\", %s",
                            tmp.c_str(),
                            out.c_str(), in, unique ? "unique" : "ambiguous",
                            expect.c_str(), remaining, t.unique ? "unique" : "ambiguous");
                    });

                }
            }
        }
    }

    SECTION("Abbreviate")
    {
        const char* script = "\
            function abbrev_all_git(dir) \
                return not os.isdir(path.join(dir, '.git')) \
            end \
            \
            function abbrev_all(dir) \
                return true \
            end \
            \
            function abbrev_git(dir) \
                if os.isdir(path.join(dir, '.git')) then \
                    return false \
                end \
            end \
            \
            function abbrev_transform(name, abbrev) \
                if abbrev then \
                    return '<'..name..'>' \
                end \
            end \
        ";

        REQUIRE(lua.do_string(script));

        static const testcase_abbrev c_testcases[] =
        {
            { "xyz/bag/foo",            "xyz\\<ba>\\foo" },
            { "xyz/bookkeeper/foo",     "xyz\\<bookkeepe>\\foo" },
            { "xyz/bookkeeping/foo",    "xyz\\<bookkeepi>\\foo" },
            { "xyz/box/foo",            "xyz\\box\\foo" },
            { "xyz/boxes/foo",          "xyz\\<boxe>\\foo" },
            { "xyz/repo/foo",           "xyz\\<r>\\foo" },
            { "xyz/repo/foo",           "xyz\\repo\\foo",           false/*all*/,   true/*git*/ },
            { "xyz/notrepo/foo",        "xyz\\<n>\\foo" },

            { "xyz/bag/foo",            "<x>\\<ba>\\<f>",           true/*all*/ },
            { "xyz/bookkeeper/foo",     "<x>\\<bookkeepe>\\<f>",    true/*all*/ },
            { "xyz/bookkeeping/foo",    "<x>\\<bookkeepi>\\<f>",    true/*all*/ },
            { "xyz/box/foo",            "<x>\\box\\<f>",            true/*all*/ },
            { "xyz/boxes/foo",          "<x>\\<boxe>\\<f>",         true/*all*/ },
            { "xyz/repo/foo",           "<x>\\<r>\\<f>",            true/*all*/ },
            { "xyz/repo/foo",           "<x>\\repo\\<f>",           true/*all*/,    true/*git*/ },
            { "xyz/notrepo/foo",        "<x>\\<n>\\<f>",            true/*all*/ },
        };

        SECTION("Normal")
        {
            for (auto const& t : c_testcases)
                verify_abbrev(lua, t, out, false/*transform*/);
        }

        SECTION("Transform")
        {
            for (auto const& t : c_testcases)
                verify_abbrev(lua, t, out, true/*transform*/);
        }
    }
}
