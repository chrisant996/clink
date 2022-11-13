// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/os.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>

//------------------------------------------------------------------------------
struct testcase
{
    const char* in;
    const char* expanded;
    const char* remaining;
    bool unique;
};

//------------------------------------------------------------------------------
TEST_CASE("Abbreviated paths.")
{
    static const char* dir_fs[] = {
        "xyz/bag/leaf",
        "xyz/bookkeeper/leaf",
        "xyz/bookkeeping/leaf",
        "xyz/box/leaf",
        "xyz/boxes/leaf",
        "xyz/repo/.git/leaf",
        "xyz/notrepo/leaf",
        nullptr,
    };

    fs_fixture fs(dir_fs);

    lua_state lua;
    line_editor_tester tester;

    str<> tmp;
    str<> out;
    SECTION("Expand abbreviated paths")
    {
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
                    printf("       in:  \t\"%s\"\n      out:  \"%s\", \"%s\", %s\nexpected:  \"%s\", \"%s\", %s",
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
                        printf("      in:  \"%s\"\n     out:  \"%s\", \"%s\", %s\nexpected:  \"%s\", \"%s\", %s",
                            tmp.c_str(),
                            out.c_str(), in, unique ? "unique" : "ambiguous",
                            expect.c_str(), remaining, t.unique ? "unique" : "ambiguous");
                    });

                }
            }
        }
    }
}
