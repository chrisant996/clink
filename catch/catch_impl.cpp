// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

//------------------------------------------------------------------------------
int run_catch(int argc, char** argv)
{
    int result = Catch::Session().run(argc, argv);
    return result;
}
