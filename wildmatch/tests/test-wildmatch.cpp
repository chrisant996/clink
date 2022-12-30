// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>
#include <core/str.h>

#include "wildmatch.h"

static void xxx_hack(int caseNum, const char* line, const char*& str)
{
    REQUIRE(str[0] != '/', [&] () {
        printf("Slash prefix in case #%d; use 'XXX/' instead of '/':\n%s\n", caseNum, line);
    });

    if (!strncmp(str, "XXX/", 4) || !strncmp(str, "XXX\\", 4))
        str += 3;
}

static int test_line(int caseNum, const char* line, const char* op, const char* string, const char* pattern)
{
    xxx_hack(caseNum, line, op);
    xxx_hack(caseNum, line, string);
    xxx_hack(caseNum, line, pattern);

    //printf("op=\"%s\", str=\"%s\", pat=\"%s\"\n", op, string, pattern);

    const int slashFlag = (op[0] == '\\' ) ? WM_SLASHFOLD : 0;
    if (slashFlag)
        op++;

    int exitCode;
    if (!strcmp(op, "wildmatch"))
        exitCode = !!wildmatch(pattern, string, WM_WILDSTAR | slashFlag);
    else if (!strcmp(op, "iwildmatch"))
        exitCode = !!wildmatch(pattern, string, WM_WILDSTAR | WM_CASEFOLD | slashFlag);
    else if (!strcmp(op, "pathmatch"))
        exitCode = !!wildmatch(pattern, string, slashFlag);
    else if (!strcmp(op, "fnmatch"))
        //exitCode = !!fnmatch(pattern, string, FNM_PATHNAME | slashFlag);
        exitCode = !!wildmatch(pattern, string, WM_PATHNAME | slashFlag);
    else
    {
        exitCode = -1;
        REQUIRE(false, [&] () {
            printf("Unrecognized operation '%s' in case #%d:\n%s\n", op, caseNum, line);
        });
    }

    return exitCode;
}

static int get_arg(char* s, const char** args)
{
    const char* arg = *args;

    if (!*arg)
        return 0;

    const char quote = (arg[0] == '\'' || arg[0] == '\"') ? arg[0] : '\0';
    const char stop = quote ? quote : ' ';

    if (*arg && *arg == quote)
        *arg++; // Drop the quote.

    while (*arg && *arg != stop)
        *s++ = *arg++;

    if (*arg && *arg == quote)
        *arg++; // Drop the quote.

    *s = '\0';

    while (*arg && *arg == ' ')
        arg++;

    *args = arg;
    return 1;
}

static void for_each_line(int caseNum, const char* line, const char* op, const char* mode, const char* args)
{
    char desc[1024];
    char pattern[1024];
    char string[1024];

    REQUIRE(get_arg(string, &args), [&] () {
        printf("Missing string argument in case #%d.\n%s\n", caseNum, line);
    });

    if (*op == '\\')
    {
        for (char *p = string; *p; p++)
            if (*p == '/')
                *p = '\\';
    }

    REQUIRE(get_arg(pattern, &args), [&] () {
        printf("Missing pattern argument in case #%d:\n%s\n", caseNum, line);
    });

    sprintf(desc, "%-11s %s match %s %s", op, (*mode == '1') ? "  " : "no", string, pattern);

    int pass;
    if (*mode == '1')
        pass = (test_line(caseNum, line, op, string, pattern) == 0);
    else if (*mode == '0')
        pass = (test_line(caseNum, line, op, string, pattern) != 0);
    else
        return;

    REQUIRE(pass, [&] () {
        printf("FAILED case #%d:  %s\n%s\n", caseNum, desc, line);
    });
}

//------------------------------------------------------------------------------
#include "t3070-wildmatch.i"
TEST_CASE("wildmatch")
{
    int caseNum = 0;
    for (const char* line : c_cases)
    {
        caseNum++;

        if (line[0] == '#')
            continue;

        //printf("case #%d:  %s\n", caseNum, line);

        // Parse the line.

        str<> tmp(line);
        char* p = tmp.data();

        const char slashMode = (*p == '/' || *p == '\\') ? *p : '\0';
        if (slashMode)
            p++;

        char* op = p;
        p = strchr(op, ' ');
        if (!p)
        {
LNeedFields:
            REQUIRE(false, [&] () {
                printf("Not enough fields in case #%d:\n%s\n", caseNum, line);
            });
        }
        *p++ = '\0';
        while (*p == ' ')
            p++;

        char* wmode = p;
        p = strchr(wmode, ' ');
        if (!p)
            goto LNeedFields;
        *p++ = '\0';
        while (*p == ' ')
            p++;

        char* fnmode = 0;
        if (strcmp(op, "match") == 0)
        {
            fnmode = p;
            p = strchr(fnmode, ' ');
            if (!p)
                goto LNeedFields;
            *p++ = '\0';
            while (*p == ' ')
                p++;
        }

        REQUIRE(*p, [&] () {
            printf("Missing path arguments in case #%d:\n%s\n", caseNum, line);
        });

        // Take action for the line.

        if (strcmp(op, "match") == 0)
        {
            if (slashMode != '\\')
            {
                for_each_line(caseNum, line, "wildmatch", wmode, p );
                for_each_line(caseNum, line, "fnmatch", fnmode, p );
            }
            if (slashMode != '/')
            {
                for_each_line(caseNum, line, "\\wildmatch", wmode, p );
                for_each_line(caseNum, line, "\\fnmatch", fnmode, p );
            }
        }
        else if (strcmp(op, "imatch") == 0)
        {
            if (slashMode != '\\')
                for_each_line(caseNum, line, "iwildmatch", wmode, p );
            if (slashMode != '/')
                for_each_line(caseNum, line, "\\iwildmatch", wmode, p );
        }
        else if (strcmp(op, "pathmatch") == 0)
        {
            if (slashMode != '\\')
                for_each_line(caseNum, line, "pathmatch", wmode, p );
            if (slashMode != '/')
                for_each_line(caseNum, line, "\\pathmatch", wmode, p );
        }
        else
        {
            REQUIRE(false, [&] () {
                printf("Unrecognized operation '%s' in case #%d:\n%s\n", op, caseNum, line);
            });
        }
    }

    REQUIRE(c_expected_count == caseNum, [&] () {
        printf("Expected %d test cases; ran %d.\n", c_expected_count, caseNum);
    });
}
