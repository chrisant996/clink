// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    const char* prefix = (argc > 1) ? argv[1] : "";
    return (clatch::run(prefix) != true);
}
