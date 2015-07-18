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
#include "shared/util.h"

//------------------------------------------------------------------------------
typedef int (dispatch_func_t)(const char*, int);

const char* g_clink_args = NULL;
int         g_all_users  = 0;



}

//------------------------------------------------------------------------------
static HKEY open_software_key(int all_users, const char* key, int wow64, int writable)
{
    LONG ok;
    DWORD flags;
    HKEY result;
    char buffer[1024];

    buffer[0] = '\0';
    str_cat(buffer, "Software\\", sizeof_array(buffer));
    if (wow64)
    {
        str_cat(buffer, "Wow6432Node\\", sizeof(buffer));
    }
    str_cat(buffer, key, sizeof_array(buffer));

    flags = KEY_READ|(writable ? KEY_WRITE : 0);
    flags |= KEY_WOW64_64KEY;

    ok = RegCreateKeyEx(all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
        buffer, 0, NULL, REG_OPTION_NON_VOLATILE, flags, NULL, &result, NULL);

    return (ok == 0) ? result : NULL;
}

//------------------------------------------------------------------------------
static void close_key(HKEY key)
{
    RegCloseKey(key);
}

//------------------------------------------------------------------------------
static int set_value(HKEY key, const char* name, const char* str)
{
    LONG ok;
    ok = RegSetValueEx(key, name, 0, REG_SZ, str, (DWORD)strlen(str) + 1);
    return (ok == ERROR_SUCCESS);
}

//------------------------------------------------------------------------------
static int get_value(HKEY key, const char* name, char** buffer)
{
    int i;
    DWORD req_size;

    *buffer = NULL;
    i = RegQueryValueEx(key, name, NULL, NULL, *buffer, &req_size);
    if (i != ERROR_SUCCESS && i != ERROR_MORE_DATA)
    {
        return 0;
    }

    *buffer = malloc(req_size);
    RegQueryValueEx(key, name, NULL, NULL, *buffer, &req_size);

    return req_size;
}

//------------------------------------------------------------------------------
static int delete_value(HKEY key, const char* name)
{
    LONG ok;
    ok = RegDeleteValue(key, name);
    return (ok == ERROR_SUCCESS);
}



//------------------------------------------------------------------------------
HKEY open_cmd_proc_key(int all_users, int wow64, int writable)
{
    return open_software_key(all_users, "Microsoft\\Command Processor", wow64, writable);
}

//------------------------------------------------------------------------------
static int check_registry_access()
{
    HKEY key;

    key = open_cmd_proc_key(g_all_users, 0, 1);
    if (key == NULL)
        return 0;

    close_key(key);

    key = open_cmd_proc_key(g_all_users, 1, 1);
    if (key == NULL)
        return 0;

    close_key(key);
    return 1;
}

//------------------------------------------------------------------------------
static int find_clink_entry(const char* value, int* left, int* right)
{
    int quoted;
    int i;
    const char* tag;
    const char* c;

    const char* needles[] = {
        "clink inject",
        "clink\" inject",
        "clink.exe inject",
        "clink.exe\" inject",
        "clink.bat inject",
        "clink.bat\" inject",
    };

    // Find roughly where clink's entry is.
    for (i = 0; i < sizeof_array(needles); ++i)
    {
        tag = strstr(value, needles[i]);
        if (tag != NULL)
        {
            break;
        }
    }

    if (tag == NULL)
    {
        return 0;
    }

    // Find right most extents of clink's entry.
    c = strchr(tag, '&');
    if (c != NULL)
    {
        *right = (int)(c - value);
    }
    else
    {
        *right = (int)strlen(value);
    }

    // Is clink's path quoted?
    c = strchr(tag, ' ');
    quoted = (c != 0) && (c[-1] == '\"');
    
    // And find the left most extent. First search for opening quote if need
    // be, then search for command separator.
    i = quoted ? '\"' : '&';
    c = tag;
    while (c > value)
    {
        if (c[0] == i)
        {
            if (!quoted)
            {
                c -= (c[-1] == i);  // & or &&?
                break;
            }

            quoted = 0;
            i = '&';
        }
            
        --c;
    }
    *left = (int)(c - value);

    return 1;
}

//------------------------------------------------------------------------------
static const char* get_cmd_start(const char* cmd)
{
    while (isspace(*cmd) || *cmd == '&')
    {
        ++cmd;
    }

    return cmd;
}

//------------------------------------------------------------------------------
static int uninstall_autorun(const char* clink_path, int wow64)
{
    HKEY cmd_proc_key;
    char* key_value;
    int ret;
    int left, right;

    cmd_proc_key = open_cmd_proc_key(g_all_users, wow64, 1);
    if (cmd_proc_key == NULL)
    {
        printf("ERROR: Failed to open registry key (%d)\n", GetLastError());
        return 0;
    }

    key_value = NULL;
    get_value(cmd_proc_key, "AutoRun", &key_value);

    ret = 1;
    if (key_value && find_clink_entry(key_value, &left, &right))
    {
        const char* read;
        char* write;
        int i, n;

        // Copy the key value into itself, skipping clink's entry.
        read = write = key_value;
        for (i = 0, n = (int)strlen(key_value); i <= n; ++i)
        {
            if (i < left || i >= right)
            {
                *write++ = *read;
            }

            ++read;
        }

        read = get_cmd_start(key_value);
        if (*read == '\0')
        {
            // Empty key. We might as well delete it.
            if (!delete_value(cmd_proc_key, "AutoRun"))
            {
                ret = 0;
            }
        }
        else if (!set_value(cmd_proc_key, "AutoRun", read))
        {
            ret = 0;
        }
    }

    // Tidy up.
    close_key(cmd_proc_key);
    free(key_value);
    return ret;
}

//------------------------------------------------------------------------------
static int install_autorun(const char* clink_path, int wow64)
{
    HKEY cmd_proc_key;
    const char* value;
    char* new_value;
    char* key_value;
    int i;

    // Remove any previous autorun entries so we never have more than one. We
    // could just check for an exisiting entry, but by always uninstalling and
    // reinstalling, we ensure clink's last in the chain, which allows it to
    // play nicely with other projects that hook cmd.exe and install autoruns.
    uninstall_autorun(clink_path, wow64);

    cmd_proc_key = open_cmd_proc_key(g_all_users, wow64, 1);
    if (cmd_proc_key == NULL)
    {
        printf("ERROR: Failed to open registry key (%d)\n", GetLastError());
        return 0;
    }

    key_value = NULL;
    get_value(cmd_proc_key, "AutoRun", &key_value);

    i = key_value ? (int)strlen(key_value) : 0;
    i += 2048;
    new_value = malloc(i);

    // Build the new autorun entry by appending clink's entry to the current one.
    new_value[0] = '\0';
    if (key_value != NULL && *key_value != '\0')
    {
        str_cat(new_value, key_value, i);
        str_cat(new_value, "&&", i);
    }
    str_cat(new_value, "\"", i);
    str_cat(new_value, clink_path, i);
    str_cat(new_value, "\\clink\" inject", i);

    if (g_clink_args != NULL)
    {
        str_cat(new_value, " ", i);
        str_cat(new_value, g_clink_args, i);
    }

    // Set it
    value = get_cmd_start(new_value);
    i = 1;
    if (!set_value(cmd_proc_key, "AutoRun", value))
    {
        i = 0;
    }

    // Tidy up.
    close_key(cmd_proc_key);
    free(new_value);
    free(key_value);
    return i;
}

//------------------------------------------------------------------------------
static int show_autorun(const char* clink_path, int wow64)
{
    HKEY cmd_proc_key;
    char* key_value;

    cmd_proc_key = open_cmd_proc_key(g_all_users, wow64, 0);
    if (cmd_proc_key == NULL)
    {
        return 0;
    }

    key_value = NULL;
    get_value(cmd_proc_key, "AutoRun", &key_value);

    printf("%6s : %s\n",
        wow64 ? "wow64" : "native",
        key_value ? key_value : "<unset>"
    );

    close_key(cmd_proc_key);
    free(key_value);
    return 0;
}

//------------------------------------------------------------------------------
static int force_autorun(const char* value, int wow64)
{
    HKEY cmd_proc_key;
    int i;

    cmd_proc_key = open_cmd_proc_key(g_all_users, wow64, 1);
    if (cmd_proc_key == NULL)
    {
        printf("ERROR: Failed to open registry key (%d)\n", GetLastError());
        return 0;
    }

    i = set_value(cmd_proc_key, "AutoRun", value);

    close_key(cmd_proc_key);
    return 0;
}

//------------------------------------------------------------------------------
static int dispatch(dispatch_func_t* function, const char* clink_path)
{
    int ok;
    int i;
    int is_x64_os;
    SYSTEM_INFO system_info;

    GetNativeSystemInfo(&system_info);
    i = system_info.wProcessorArchitecture;
    is_x64_os = (i == PROCESSOR_ARCHITECTURE_AMD64);

    ok = 1;
    for (i = 0; i <= is_x64_os; ++i)
    {
        ok &= function(clink_path, i);
    }

    return ok;
}

//------------------------------------------------------------------------------
int autorun(int argc, char** argv)
{
    char* clink_path;
    int i;
    int ret;
    dispatch_func_t* function;
    const char* path_arg;

    struct option options[] = {
        { "install",    no_argument,        NULL, 'i' },
        { "uninstall",  no_argument,        NULL, 'u' },
        { "show",       no_argument,        NULL, 's' },
        { "value",      required_argument,  NULL, 'v' },
        { "help",       no_argument,        NULL, 'h' },
        { "allusers",   no_argument,        NULL, 'a' },
        { NULL, 0, NULL, 0 }
    };

    const char* help[] = {
        "-i, --install",        "Installs an autorun entry for cmd.exe.",
        "-u, --uninstall",      "Uninstalls an autorun entry.",
        "-s, --show",           "Displays the current autorun settings.",
        "-v, --value <string>", "Sets the autorun to <string>.",
        "-a, --allusers",       "Install autorun for all users (requires admin rights).",
        "-h, --help",           "Shows this help text.",
        "-- <args>",            "Pass <args> that follow '--' on to Clink."
    };

    extern const char* g_clink_header;

    // Get path where clink is installed (assumed to be where this executable is)
    clink_path = malloc(strlen(_pgmptr));
    clink_path[0] = '\0';
    str_cat(clink_path, _pgmptr, (int)(strrchr(_pgmptr, '\\') - _pgmptr + 1));

    function = NULL;
    path_arg = clink_path;

    while ((i = getopt_long(argc, argv, "v:siuha", options, NULL)) != -1)
    {
        switch (i)
        {
        case 'i':
            function = install_autorun;
            break;

        case 'u':
            function = uninstall_autorun;
            break;

        case 's':
            function = show_autorun;
            break;

        case 'v':
            function = force_autorun;
            path_arg = optarg;
            break;

        case 'a':
            g_all_users = 1;
            break;

        case '?':
            ret = -1;
            goto end;

        case 'h':
            puts(g_clink_header);
            puts_help(help, sizeof_array(help));
            ret = -1;
            goto end;
        }
    }

    // Collect arguments to pass on to Clink.
    if (optind < argc && strcmp(argv[optind - 1], "--") == 0)
    {
        const char* cmd_line;

        cmd_line = GetCommandLine();
        cmd_line = strstr(cmd_line, " -- ");
        if (cmd_line != NULL)
        {
            g_clink_args = cmd_line + 4;
        }
    }

    // Do the magic.
    if (function != NULL)
    {
        if (!check_registry_access())
        {
            puts("You must have administator rights to access cmd.exe's autorun");
            ret = -1;
            goto end;
        }

        ret = dispatch(function, path_arg);
    }
    else
    {
        puts(g_clink_header);
        puts_help(help, sizeof_array(help));
        puts("Write access to cmd.exe's AutoRun registry entry can require\n"
            "administrator privileges.");
        ret = -1;
    }

end:
    free(clink_path);
    return ret;
}

// vim: expandtab
