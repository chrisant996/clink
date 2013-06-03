/* Copyright (c) 2012 Martin Ridgers
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "getopt.h"
#include "shared/util.h"

//------------------------------------------------------------------------------
void                prepare_env_for_inputrc();
lua_State*          initialise_lua();
int                 call_readline_w(const wchar_t*, wchar_t*, unsigned);
char**              match_display_filter(char**, int);
extern void         (*g_alt_fwrite_hook)(wchar_t*);

static const char*  g_getc_automatic    = NULL;
static char*        g_caught_matches    = NULL;

//------------------------------------------------------------------------------
int getwch_automatic(int* alt)
{
    if (g_getc_automatic != NULL)
    {
        char c = *g_getc_automatic;
        ++g_getc_automatic;

        if (*g_getc_automatic == '\0')
        {
            g_getc_automatic = "\t\t\n";
        }

        return c;
    }

    return '\n';
}

//------------------------------------------------------------------------------
static void match_catch(char** matches, int match_count, int longest)
{
    int i;
    char* write;

    free(g_caught_matches);
    g_caught_matches = malloc((longest * 2) * match_count);

    matches = match_display_filter(matches, match_count);

    write = g_caught_matches;
    for (i = 1; i <= match_count; ++i)
    {
        strcpy(write, matches[i]);
        write += strlen(write) + 1;

        free(matches[i]);
    }

    free(matches);
    *write = '\0';
}

//------------------------------------------------------------------------------
static void stdout_catch(wchar_t* buffer)
{
}

//------------------------------------------------------------------------------
static int call_readline_lua(lua_State* lua)
{
    wchar_t output[1024];
    char utf8[sizeof_array(output)];
    char* read;
    int i;

    // Check we've got at least one string argument.
    if (lua_gettop(lua) == 0 || !lua_isstring(lua, 1))
    {
        return 0;
    }

    g_getc_automatic = lua_tostring(lua, 1);

    // Call Readline.
    g_alt_fwrite_hook = stdout_catch;
    rl_completion_display_matches_hook = match_catch;
    call_readline_w(L"", output, sizeof_array(output));

    // Get output.
    WideCharToMultiByte(CP_UTF8, 0, output, -1, utf8, sizeof(utf8), NULL, NULL);
    lua_pushstring(lua, utf8);

    // Collect matches "displayed" by readline.
    lua_createtable(lua, 0, 0);

    i = 1;
    read = g_caught_matches;
    while (read && *read != '\0')
    {
        lua_pushstring(lua, read);
        lua_rawseti(lua, -2, i);

        ++i;
        read += strlen(read) + 1;
    }

    free(g_caught_matches);
    g_caught_matches = NULL;

    return 2;
}

//------------------------------------------------------------------------------
int get_cwd(lua_State* lua)
{
    char buffer[MAX_PATH];

    GetCurrentDirectory(sizeof_array(buffer), buffer);
    lua_pushstring(lua, buffer);

    return 1;
}

//------------------------------------------------------------------------------
int ch_dir(lua_State* lua)
{
    const char* path;
    BOOL ok;

    if (lua_gettop(lua) == 0 || !lua_isstring(lua, 1))
    {
        return 0;
    }

    path = lua_tostring(lua, 1);

    ok = SetCurrentDirectory(path);
    lua_pushboolean(lua, ok != FALSE);

    return 1;
}

//------------------------------------------------------------------------------
int mk_dir(lua_State* lua)
{
    int ok;
    const char* path;

    if (lua_gettop(lua) == 0 || !lua_isstring(lua, 1))
    {
        return 0;
    }

    path = lua_tostring(lua, 1);

    ok = SHCreateDirectoryEx(NULL, path, NULL);
    lua_pushboolean(lua, ok == ERROR_SUCCESS);

    return 1;
}

//------------------------------------------------------------------------------
int rm_dir(lua_State* lua)
{
    int ok;
    char path[MAX_PATH];
    SHFILEOPSTRUCT op = {
        NULL, FO_DELETE, NULL, NULL,
        FOF_SILENT|FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_NOCONFIRMMKDIR,
        FALSE, NULL, ""
    };

    if (lua_gettop(lua) == 0 || !lua_isstring(lua, 1))
    {
        return 0;
    }

    str_cpy(path, lua_tostring(lua, 1), sizeof_array(path) - 1);
    path[strlen(path) + 1] = '\0';

    op.pFrom = path; 
    ok = SHFileOperation(&op);
    lua_pushboolean(lua, !ok);

    return 1;
}

//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    int test_ok;
    char buffer[512];
    const char* specific_test;
    const char* scripts_path;
    lua_State* lua;
    int no_colour;
    int verbose;
    int arg;

    struct option options[] = {
        { "scripts",    required_argument,  NULL, 's' },
        { "verbose",    no_argument,        NULL, 'v' },
        { "nocolour",   no_argument,        NULL, 'n' },
        { "test",       required_argument,  NULL, 't' },
        { NULL,         0,                  NULL, 0 }
    };

    // Parse arguments
    specific_test = "";
    no_colour = 0;
    verbose = 0;
    scripts_path = NULL;
    if (argc > 1)
    {
        while ((arg = getopt_long(argc, argv, "+svnt", options, NULL)) != -1)
        {
            switch (arg)
            {
                case 's':
                    scripts_path = optarg;
                    break;

                case 'v':
                    verbose = 1;
                    break;

                case 'n':
                    no_colour = 1;
                    break;

                case 't':
                    specific_test = optarg;
                    break;

                default:
                    return 0;
            }
        }
    }

    // Without arguments show help.
    if (scripts_path == NULL || argc <= 1)
    {
        puts("Usage: --scripts=<scripts_path>");
        return 0;
    }

    if (SetCurrentDirectory(scripts_path) == FALSE)
    {
        puts("Invalid scripts path");
        return 0;
    }

    prepare_env_for_inputrc();

    // Load root test.lua script into Clink's Lua state.
    lua = initialise_lua();
    {
        struct luaL_Reg native_methods[] = {
            { "call_readline", call_readline_lua },
            { "get_cwd", get_cwd },
            { "ch_dir", ch_dir },
            { "mk_dir", mk_dir },
            { "rm_dir", rm_dir },
            { NULL, NULL }
        };

        lua_pushglobaltable(lua); 
        luaL_setfuncs(lua, native_methods, 0);
    }

    lua_pushinteger(lua, verbose);
    lua_setglobal(lua, "verbose");

    lua_pushinteger(lua, no_colour);
    lua_setglobal(lua, "no_colour");

    lua_pushstring(lua, specific_test);
    lua_setglobal(lua, "specific_test");

    if (luaL_dofile(lua, "test.lua"))
    {
        puts(lua_tostring(lua, -1));
        return 0;
    }

    // Run the test, collecting the results.
    lua_getglobal(lua, "clink");
    lua_pushliteral(lua, "test");
    lua_rawget(lua, -2);

    lua_pushliteral(lua, "run");
    lua_rawget(lua, -2);

    if (lua_pcall(lua, 0, 1, 0) != 0)
    {
        puts(lua_tostring(lua, -1));
        lua_pop(lua, 2);
        return 0;
    }

    test_ok = lua_toboolean(lua, -1);
    lua_pop(lua, 1);

    lua_pop(lua, 2);

    return test_ok;
}

// vim: expandtab
