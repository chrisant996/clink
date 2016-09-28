// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>
#include <core/str.h>

#include <getopt.h>

//------------------------------------------------------------------------------
typedef int     (dispatch_func_t)(const char*, int);
str<>           g_clink_args;
int             g_all_users  = 0;
void            puts_help(const char**, int);



//------------------------------------------------------------------------------
static HKEY open_software_key(int all_users, const char* key, int wow64, int writable)
{
    str<512> buffer;
    buffer << "Software\\";
    if (wow64)
        buffer << "Wow6432Node\\";
    buffer << key;

    DWORD flags;
    flags = KEY_READ|(writable ? KEY_WRITE : 0);
    flags |= KEY_WOW64_64KEY;

    HKEY result;
    BOOL ok = RegCreateKeyEx(all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
        buffer.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, flags, nullptr,
        &result, nullptr);

    return (ok == 0) ? result : nullptr;
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
    ok = RegSetValueEx(key, name, 0, REG_SZ, (const BYTE*)str, (DWORD)strlen(str) + 1);
    return (ok == ERROR_SUCCESS);
}

//------------------------------------------------------------------------------
static int get_value(HKEY key, const char* name, char** buffer)
{
    int i;
    DWORD req_size;

    *buffer = nullptr;
    i = RegQueryValueEx(key, name, nullptr, nullptr, (BYTE*)*buffer, &req_size);
    if (i != ERROR_SUCCESS && i != ERROR_MORE_DATA)
        return 0;

    *buffer = (char*)malloc(req_size);
    RegQueryValueEx(key, name, nullptr, nullptr, (BYTE*)*buffer, &req_size);

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
static HKEY open_cmd_proc_key(int all_users, int wow64, int writable)
{
    return open_software_key(all_users, "Microsoft\\Command Processor", wow64, writable);
}

//------------------------------------------------------------------------------
static int check_registry_access()
{
    HKEY key;

    key = open_cmd_proc_key(g_all_users, 0, 1);
    if (key == nullptr)
        return 0;

    close_key(key);

    key = open_cmd_proc_key(g_all_users, 1, 1);
    if (key == nullptr)
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
        "clink_x64.exe inject",
        "clink_x86.exe inject",
        "clink_x64.exe\" inject",
        "clink_x86.exe\" inject",
        "clink.bat inject",
        "clink.bat\" inject",
    };

    // Find roughly where clink's entry is.
    for (i = 0; i < sizeof_array(needles); ++i)
    {
        tag = strstr(value, needles[i]);
        if (tag != nullptr)
        {
            break;
        }
    }

    if (tag == nullptr)
    {
        return 0;
    }

    // Find right most extents of clink's entry.
    c = strchr(tag, '&');
    if (c != nullptr)
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
    if (cmd_proc_key == nullptr)
    {
        printf("ERROR: Failed to open registry key (%d)\n", GetLastError());
        return 0;
    }

    key_value = nullptr;
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
    char* key_value;
    int i;

    // Remove any previous autorun entries so we never have more than one. We
    // could just check for an exisiting entry, but by always uninstalling and
    // reinstalling, we ensure clink's last in the chain, which allows it to
    // play nicely with other projects that hook cmd.exe and install autoruns.
    uninstall_autorun(clink_path, wow64);

    cmd_proc_key = open_cmd_proc_key(g_all_users, wow64, 1);
    if (cmd_proc_key == nullptr)
    {
        printf("ERROR: Failed to open registry key (%d)\n", GetLastError());
        return 0;
    }

    key_value = nullptr;
    get_value(cmd_proc_key, "AutoRun", &key_value);

    i = key_value ? (int)strlen(key_value) : 0;
    i += 2048;
    str_base new_value((char*)malloc(i), i);
    new_value.clear();

    // Build the new autorun entry by appending clink's entry to the current one.
    if (key_value != nullptr && *key_value != '\0')
        new_value << key_value << "&";
    new_value << "\"" << clink_path << "\\clink.bat\" inject --autorun";

    if (!g_clink_args.empty())
        new_value << " " << g_clink_args;

    // Set it
    value = get_cmd_start(new_value.c_str());
    i = 1;
    if (!set_value(cmd_proc_key, "AutoRun", value))
        i = 0;

    // Tidy up.
    close_key(cmd_proc_key);
    free(new_value.data());
    free(key_value);
    return i;
}

//------------------------------------------------------------------------------
static int show_autorun()
{
    int all_users;

    puts("Current AutoRun values");

    for (all_users = 0; all_users < 2; ++all_users)
    {
        int wow64;

        printf("\n  %s:\n", all_users ? "All users" : "Current user");

        for (wow64 = 0; wow64 < 2; ++wow64)
        {
            HKEY cmd_proc_key;
            char* key_value;

            cmd_proc_key = open_cmd_proc_key(all_users, wow64, 0);
            if (cmd_proc_key == nullptr)
            {
                printf("ERROR: Failed to open registry key (%d)\n", GetLastError());
                return 0;
            }

            key_value = nullptr;
            get_value(cmd_proc_key, "AutoRun", &key_value);

            printf("\n    %6s : %s",
                    wow64     ? " wow64"  : "native",
                    key_value ? key_value : "<unset>"
                  );

            close_key(cmd_proc_key);
            free(key_value);
        }

        puts("");
    }

    puts("");
    return 1;
}

//------------------------------------------------------------------------------
static int set_autorun_value(const char* value, int wow64)
{
    HKEY cmd_proc_key;
    int ret;

    cmd_proc_key = open_cmd_proc_key(g_all_users, wow64, 1);
    if (cmd_proc_key == nullptr)
    {
        printf("ERROR: Failed to open registry key (%d)\n", GetLastError());
        return 0;
    }

    if (value == nullptr || *value == '\0')
        ret = delete_value(cmd_proc_key, "AutoRun");
    else
        ret = set_value(cmd_proc_key, "AutoRun", value);

    close_key(cmd_proc_key);
    return ret;
}

//------------------------------------------------------------------------------
static int dispatch(dispatch_func_t* function, const char* clink_path)
{
    int ok;
    int wow64;
    int is_x64_os;
    SYSTEM_INFO system_info;

    GetNativeSystemInfo(&system_info);
    is_x64_os = system_info.wProcessorArchitecture;
    is_x64_os = (is_x64_os == PROCESSOR_ARCHITECTURE_AMD64);

    ok = 1;
    for (wow64 = 0; wow64 <= is_x64_os; ++wow64)
    {
        ok &= function(clink_path, wow64);
    }

    return ok;
}

//------------------------------------------------------------------------------
static void print_help()
{
    const char* help_verbs[] = {
        "install <args...>", "Installs a command to cmd.exe's autorun to start Clink",
        "uninstall",         "Does the opposite of 'install'",
        "show",              "Displays the values of cmd.exe's autorun variables",
        "set <string...>",   "Explicitly sets cmd.exe's autorun to <string>",
    };

    const char* help_args[] = {
        "-a, --allusers",       "Modifies autorun for all users (requires admin rights).",
        "-h, --help",           "Shows this help text.",
    };

    extern const char* g_clink_header;

    puts(g_clink_header);

    puts("Usage: autorun [options] <verb> [<string>] [-- <clink_args>]\n");

    puts("Verbs:");
    puts_help(help_verbs, sizeof_array(help_verbs));

    puts("Options:");
    puts_help(help_args, sizeof_array(help_args));

    puts("Autorun simplifies making modifications to cmd.exe's autorun registry\n"
        "variables. The value of these variables are read and executed by cmd.exe when\n"
        "it starts. The 'install/uninstall' verbs add/remove the corrent command to run\n"
        "Clink when cmd.exe starts. All '<args>' that follow 'install' are passed to\n"
        "Clink - see 'clink inject --help' for reference.\n");

    puts("To include quotes they must be escaped with a backslash;\n");
    puts("  clink autorun set \\\"foobar\\\"");

    puts("\nWrite access to cmd.exe's AutoRun registry entry will require\n"
        "administrator privileges when using the --allusers option.");
}

//------------------------------------------------------------------------------
static void success_message(const char* message)
{
    show_autorun();
    printf("%s (for %s).\n", message, g_all_users ? "all users" : "current user");
}


//------------------------------------------------------------------------------
int autorun(int argc, char** argv)
{
    // Parse command line arguments.
    struct option options[] = {
        { "help",       no_argument,    nullptr, 'h' },
        { "allusers",   no_argument,    nullptr, 'a' },
        {}
    };

    str<MAX_PATH> clink_path;

    int i;
    int ret = 0;
    while ((i = getopt_long(argc, argv, "ha", options, nullptr)) != -1)
    {
        switch (i)
        {
        case 'a':
            g_all_users = 1;
            break;

        case 'h':
            print_help();
            return 0;

        default:
            return 0;
        }
    }

    dispatch_func_t* function = nullptr;

    // Find out what to do by parsing the verb.
    if (optind < argc)
    {
        if (!strcmp(argv[optind], "install"))
            function = install_autorun;
        else if (!strcmp(argv[optind], "uninstall"))
            function = uninstall_autorun;
        else if (!strcmp(argv[optind], "set"))
            function = set_autorun_value;
        else if (!strcmp(argv[optind], "show"))
            return show_autorun();
    }

    // Get path where clink is installed (assumed to be where this executable is)
    if (function == install_autorun)
    {
        clink_path << _pgmptr;
        clink_path.truncate(clink_path.last_of('\\'));
    }

    // Collect the remainder of the command line.
    if (function == install_autorun || function == set_autorun_value)
    {
        for (i = optind + 1; i < argc; ++i)
        {
            g_clink_args << argv[i];
            if (i < argc - 1)
                g_clink_args << " ";
        }
    }

    // If we can't continue any further then warn the user.
    if (function == nullptr)
    {
        puts("ERROR: Invalid arguments. Run 'clink autorun --help' for info.");
        return 0;
    }

    // Do the magic.
    if (!check_registry_access())
    {
        puts("You must have administator rights to access cmd.exe's autorun");
        return 0;
    }

    const char* arg = clink_path.c_str();
    arg = *arg ? arg : g_clink_args.c_str();
    ret = dispatch(function, arg);

    // Provide the user with some feedback.
    if (ret == 1)
    {
        const char* msg = nullptr;

        if (function == install_autorun)
            msg = "Clink successfully installed to run when cmd.exe starts";
        else if (function == uninstall_autorun)
            msg = "Clink's autorun entry has been removed";
        else if (function == set_autorun_value)
            msg = "Cmd.exe's AutoRun registry key set successfully";

        if (msg != nullptr)
            success_message(msg);
    }

    return ret;
}
