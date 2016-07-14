// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
struct inject_args
{
    // Must be kept simple as it's blitted
    // from one process to another.

    char    profile_path[512];
    bool    quiet;
    bool    no_log;
};
