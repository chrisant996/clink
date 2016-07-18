// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>
#include <core/settings.h>

TEST_CASE("settings : basic")
{
    auto* first = settings::first();
    {
        setting_bool test("one", "", "", false);
        REQUIRE(settings::first() == &test);
    }
    REQUIRE(settings::first() == first);
}

TEST_CASE("settings : list")
{
    auto* first = settings::first();

    auto* one   = new setting_bool("one",   "", "", false);
    auto* two   = new setting_bool("two",   "", "", false);
    auto* three = new setting_bool("three", "", "", false);
    auto* four  = new setting_bool("four",  "", "", false);

    // All
    setting* iter = settings::first();
    REQUIRE(strcmp(iter->get_name(), "four") == 0); iter = iter->next();
    REQUIRE(strcmp(iter->get_name(), "three") == 0); iter = iter->next();
    REQUIRE(strcmp(iter->get_name(), "two") == 0); iter = iter->next();
    REQUIRE(strcmp(iter->get_name(), "one") == 0); iter = iter->next();
    REQUIRE(iter == first);

    // Middle
    delete three;
    iter = settings::first();
    REQUIRE(strcmp(iter->get_name(), "four") == 0); iter = iter->next();
    REQUIRE(strcmp(iter->get_name(), "two") == 0); iter = iter->next();
    REQUIRE(strcmp(iter->get_name(), "one") == 0); iter = iter->next();
    REQUIRE(iter == first);

    // End
    delete one;
    iter = settings::first();
    REQUIRE(strcmp(iter->get_name(), "four") == 0); iter = iter->next();
    REQUIRE(strcmp(iter->get_name(), "two") == 0); iter = iter->next();
    REQUIRE(iter == first);

    // First
    delete four;
    iter = settings::first();
    REQUIRE(strcmp(iter->get_name(), "two") == 0); iter = iter->next();
    REQUIRE(iter == first);

    // Last
    delete two;
    REQUIRE(settings::first() == first);
}

TEST_CASE("settings : bool")
{
    setting_bool test("one", "", "", true);

    REQUIRE(test.set("0"));     REQUIRE(!test.get());
    REQUIRE(test.set("000"));   REQUIRE(!test.get());
    REQUIRE(test.set("false")); REQUIRE(!test.get());
    REQUIRE(test.set("False")); REQUIRE(!test.get());
    REQUIRE(test.set("FaLsE")); REQUIRE(!test.get());

    REQUIRE(test.set("1"));    REQUIRE(test.get());
    REQUIRE(test.set("101"));  REQUIRE(test.get());
    REQUIRE(test.set("true")); REQUIRE(test.get());
    REQUIRE(test.set("True")); REQUIRE(test.get());
    REQUIRE(test.set("TrUe")); REQUIRE(test.get());

    str<> out;
    test.get(out);
    REQUIRE(out.equals("True"));

    REQUIRE(!test.set("abc")); REQUIRE(test.get());
    REQUIRE(test.set("0abc")); REQUIRE(!test.get());

    test.get(out);
    REQUIRE(out.equals("False"));
}

TEST_CASE("settings : int")
{
    setting_int test("one", "", "", 1);

    REQUIRE(test.set("100")); REQUIRE(test.get() == 100);
    REQUIRE(test.set("101")); REQUIRE(test.get() == 101);
    REQUIRE(test.set("102")); REQUIRE(test.get() == 102);

    str<> out;
    test.get(out);
    REQUIRE(out.equals("102"));

    REQUIRE(!test.set("abc")); REQUIRE(test.get() == 102);
    REQUIRE(test.set("0abc")); REQUIRE(test.get() == 0);
}

TEST_CASE("settings : str")
{
    setting_str test("one", "", "", "abc");

    REQUIRE(strcmp(test.get(), "abc") == 0);
    REQUIRE(test.set("Abc")); REQUIRE(strcmp(test.get(), "Abc") == 0);
    REQUIRE(test.set("ABc")); REQUIRE(strcmp(test.get(), "ABc") == 0);
    REQUIRE(test.set("ABC")); REQUIRE(strcmp(test.get(), "ABC") == 0);
}

TEST_CASE("settings : enum")
{
    setting_enum test("one", "", "", "zero,one,two", 1);

    REQUIRE(test.get() == 1);

    str<> out;
    const char* options[] = { "zero", "one", "two" };
    for (int i = 0; i < sizeof_array(options); ++i)
    {
        REQUIRE(test.set(options[i]));
        REQUIRE(test.get() == i);
        
        test.get(out);
        REQUIRE(out.equals(options[i]));
    }

    REQUIRE(!test.set("abc"));  REQUIRE(test.get() == 2);
    REQUIRE(!test.set("0abc")); REQUIRE(test.get() == 2);
}
