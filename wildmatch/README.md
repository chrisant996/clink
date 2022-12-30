wildmatch
=========
wildmatch is a BSD-licensed C/C++ library for git/rsync-style pattern matching

[![travis build status](https://api.travis-ci.org/davvid/wildmatch.svg?branch=master)](https://travis-ci.org/davvid/wildmatch)
[![coverity scan results](https://scan.coverity.com/projects/8965/badge.svg)](https://scan.coverity.com/projects/davvid-wildmatch)

SYNOPSIS
--------
    /* C API */
    #include <wildmatch/wildmatch.h>

    if (wildmatch("/a/**/z", "/a/b/c/d/z", WM_WILDSTAR) == WM_MATCH) {
        /* matched */
    }

    /* C++ API */
    #include <wildmatch/wildmatch.hpp>

    if (wild::match("/a/**/z", "/a/b/c/d/z")) {
        /* matched */
    }

DESCRIPTION
-----------
wildmatch is an extension of function fnmatch(3) as specified in
POSIX 1003.2-1992, section B.6.

The wildmatch extension allows `**` to match `/` when `WM_PATHNAME` is
present. This gives the practical benefit of being able to match all
subdirectories of a path by using `**` and reserves the use of the
single-asterisk `*` character for matching within path components.

The C API is fnmatch-compatible by default.
The wildmatch extension is enabled by specifying `WM_WILDSTAR` in `flags`.
Specifying `WM_WILDSTAR` implies `WM_PATHNAME`.

The flags argument defaults to `wild::WILDSTAR` in the C++ API.
Calling `wild::match(...)` with only two arguments will use the extended
syntax by default.

The `WM_` flags are named the same as their `FNM_` fnmatch counterparts
and are compatible in behavior to fnmatch(3) in the absence of `WM_WILDSTAR`.

RETURN VALUE
------------
The C API returns `WM_MATCH` when string matches the pattern, and `WM_NOMATCH`
when the pattern does not match.  These values are #defines for 0 and 1,
respectively.

The C++ API returns a boolean `true` (match) or `false` (no match).

HISTORY
-------
Wildmatch's extended syntax was developed by targetting the
[wildmatch test cases](https://github.com/git/git/blob/master/t/t3070-wildmatch.sh)
from [Git](https://git-scm.com).

The "wildmatch" name is from an internal library in
[rsync](https://rsync.samba.org/) that is used for fnmatch-style matching.
Git's wildmatch implementation originally came from rsync.
The original wildmatch code was added to rsync by Wayne Davison in 2003.

The test suite for wildmatch came from Git but the library itself does not
share any lineage with either Git or rsync's wildmatch implementation.

Wildmatch was originally based on the fnmatch implementation from OpenBSD.
The OpenBSD fnmatch implementation was extended in a backwards-compatible
fashion by introducing a new `WM_WILDSTAR` flag for the purpose of enabling
the extended syntax.

BUILD
-----
    make

TEST
----
    make test

The test suite is borrowed from the Git project and can be found in
`tests/t3070-wildmatch.sh`.  Run the test script directly for debugging.
[sharness](https://github.com/mlafeldt/sharness) is used to run the tests.

Passing the `-v` flag to the test script increases its verbosity, and passing
the `--immediate` flag tells the test suite to stop on the first failure.

INSTALL
-------
    make prefix=/usr/local install

The install tree looks like the following:

    <prefix>
    ├── include
    │   └── wildmatch
    │       ├── wildmatch.h
    │       └── wildmatch.hpp
    ├── lib
    │   ├── libwildmatch-cxx.so
    │   └── libwildmatch.so
    └── share
        └── doc
            └── wildmatch
                └── README.md

The C++ API is provided by wildmatch.hpp and libwildmatch-cxx.so.
The C API is provided by wildmatch.h and libwildmatch.so.

The C++ API library is a superset of the C API.  If you link against the C++
library then you can also use the C API functions if desired; there is no need
to link against both libraries.

REQUIREMENTS
------------
* A C99-compatible compiler is needed for the C API.
* A C++11-compatible compiler is needed to build and use the C++ API.
* [cmake](https://cmake.org/) >= 2.8.12 is used to build the project.

SEE ALSO
--------
fnmatch(3)

[rsync](https://rsync.samba.org)

[Git](https://github.com/git/git)

LICENSE
-------
[BSD](LICENSE)
