// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "bind_resolver.h"
#include "binder.h"
#include "editor_module.h"

//------------------------------------------------------------------------------
TEST_CASE("Binder")
{
    binder binder;

    SECTION("Group")
    {
        REQUIRE(binder.create_group("") == -1);
        REQUIRE(binder.create_group(nullptr) == -1);

        REQUIRE(binder.get_group() == 1);

        int groups[] = {
            binder.create_group("group1"),
            binder.create_group("group2"),
        };

        REQUIRE(groups[0] != -1);
        REQUIRE(groups[1] != -1);
        REQUIRE(binder.get_group("group0") == -1);
        REQUIRE(binder.get_group("group1") == groups[0]);
        REQUIRE(binder.get_group("group2") == groups[1]);
    }

    SECTION("Overflow : group")
    {
        for (int i = 1; i < 256; ++i)
            REQUIRE(binder.create_group("group") == (i * 2) + 1);

        REQUIRE(binder.create_group("group") == -1);
    }

    SECTION("Overflow : module")
    {
        int group = binder.get_group();
        for (int i = 0; i < 64; ++i)
            REQUIRE(binder.bind(group, "", ((editor_module*)0)[i], char(i)));

        auto& module = ((editor_module*)0)[0xff];
        REQUIRE(!binder.bind(group, "", module, 0xff));
    }

    SECTION("Overflow : bind")
    {
        auto& null_module = *(editor_module*)0;
        int default_group = binder.get_group();

        for (int i = 0; i < 508; ++i)
        {
            char chord[] = { char((i > 0xff) + 1), char((i % 0xfe) + 1), 0 };
            REQUIRE(binder.bind(default_group, chord, null_module, 0x12));
        }

        REQUIRE(!binder.bind(default_group, "\x01\x02\x03", null_module, 0x12));
    }

    SECTION("Valid chords")
    {
        struct {
            const char* bind;
            const char* input;
        } chords[] = {
            "a",        "a",
            "^",        "^",
            "\\",       "\\",
            "\\t",      "\t",
#ifdef CLINK_CHRISANT_MODS
            "\\e",      "\x1b[27;27~",
#else
            "\\e",      "\x1b",
#endif
            "abc",      "abc",
            "ab",       "abd",
            "^a",       "\x01",
            "\\C-b",    "\x02",
            "\\C-C",    "\x03",
            "\\M-c",    "\x1b""c",
            "\\M-C",    "\x1b""C",
            "\\M-C-d",  "\x1b\x04",
            "",         "z",
        };

        int group = binder.get_group();
        for (const auto& chord : chords)
        {
            auto& module = *(editor_module*)(&chord);
            REQUIRE(binder.bind(group, chord.bind, module, 123));

            bind_resolver resolver(binder);
            for (const char* c = chord.input; *c; ++c)
                if (resolver.step(*c))
                    break;

            auto binding = resolver.next();
            REQUIRE(binding);
            REQUIRE(binding.get_id() == 123);
            REQUIRE(binding.get_module() == &module);
        }
    }

    SECTION("Invalid chords")
    {
        const char* chords[] = {
            "\\C",   "\\Cx",   "\\C-",
            "\\M",   "\\Mx",   "\\M-",
                               "\\M-C-",
        };

        int group = binder.get_group();
        for (const char* chord : chords)
        {
            REQUIRE(!binder.bind(group, chord, *(editor_module*)0, 234));
        }
    }
}
