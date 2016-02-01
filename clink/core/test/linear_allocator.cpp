// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/linear_allocator.h>

TEST_CASE("linear_allocator: basic")
{
    linear_allocator allocator(8);
    REQUIRE(allocator.alloc(1) != nullptr);
    REQUIRE(allocator.alloc(7) != nullptr);
    REQUIRE(allocator.alloc(1) == nullptr);
}

TEST_CASE("linear_allocator: invalid")
{
    linear_allocator allocator(8);
    REQUIRE(allocator.alloc(0) == nullptr);
    REQUIRE(allocator.alloc(9) == nullptr);
}

TEST_CASE("linear_allocator: calloc")
{
    linear_allocator allocator(sizeof(int) * 8);
    REQUIRE(allocator.calloc<int>(0) == nullptr);
    REQUIRE(allocator.calloc<int>() != nullptr);
    REQUIRE(allocator.calloc<int>(7) != nullptr);
    REQUIRE(allocator.calloc<int>(1) == nullptr);
}
