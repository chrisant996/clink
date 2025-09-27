// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include <core/linear_allocator.h>

//------------------------------------------------------------------------------
TEST_CASE("linear_allocator: basic")
{
    linear_allocator allocator(8 + sizeof(void*));
    REQUIRE(allocator.fits(0));
    REQUIRE(!allocator.fits(1));
    REQUIRE(!allocator.fits(9));
    REQUIRE(allocator.alloc(0) == nullptr);

    uint32 a_size = 7;
    void* a = allocator.alloc(a_size);
    REQUIRE(a != nullptr);
    REQUIRE(allocator.unittest_at_end(a, a_size));

    uint32 b_size = 1;
    void* b = allocator.alloc(b_size);
    REQUIRE(b != nullptr);
    REQUIRE(allocator.unittest_at_end(b, b_size));
    REQUIRE(!allocator.unittest_in_prev_page(a, a_size));

    uint32 c_size = 1;
    void* c = allocator.alloc(c_size);
    REQUIRE(c != nullptr);
    REQUIRE(allocator.unittest_at_end(c, c_size));
    REQUIRE(allocator.unittest_in_prev_page(a, a_size));
    REQUIRE(allocator.unittest_in_prev_page(b, b_size));
}

//------------------------------------------------------------------------------
TEST_CASE("linear_allocator: oversize")
{
    linear_allocator allocator(8 + sizeof(void*));

    uint32 oversize = 9;
    void* o = allocator.alloc(oversize);
    REQUIRE(o != nullptr);
    REQUIRE(!allocator.unittest_at_end(o, oversize));
    REQUIRE(allocator.unittest_in_prev_page(o, oversize));
    REQUIRE(allocator.fits(8));
    REQUIRE(!allocator.fits(9));
}

//------------------------------------------------------------------------------
TEST_CASE("linear_allocator: calloc")
{
    linear_allocator allocator(sizeof(int32) * 8 + sizeof(void*));
    REQUIRE(allocator.calloc<int32>(0) == nullptr);

    REQUIRE(allocator.calloc<int32>() != nullptr);
    REQUIRE(allocator.fits(sizeof(int32) * 7));
    REQUIRE(allocator.calloc<int32>(7) != nullptr);
    REQUIRE(!allocator.fits(1));

    REQUIRE(allocator.calloc<int32>(1) != nullptr);
    REQUIRE(allocator.fits(sizeof(int32) * 7));
    REQUIRE(!allocator.fits(sizeof(int32) * 7 + 1));
}
