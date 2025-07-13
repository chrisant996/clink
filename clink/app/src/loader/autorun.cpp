// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/usage.h"

#include <core/base.h>
#include <core/str.h>

#include <getopt.h>

//------------------------------------------------------------------------------
typedef bool    (dispatch_func_t)(const char*, int32);
str<>           g_clink_args;
int32           g_all_users  = 0;
static bool     s_was_installed = false;



//------------------------------------------------------------------------------
static HKEY open_software_key(int32 all_users, const char* _key, int32 wow64, int32 writable)
{
    wstr<> key;
    key << L"Software\\";
    if (wow64)
        key << L"Wow6432Node\\";
    to_utf16(key, _key);

    DWORD flags;
    flags = KEY_READ|(writable ? KEY_WRITE : 0);
    flags |= KEY_WOW64_64KEY;

    HKEY result;
    BOOL ok = RegCreateKeyExW(all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
        key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, flags, nullptr,
        &result, nullptr);

    return (ok == 0) ? result : nullptr;
}

//------------------------------------------------------------------------------
static void close_key(HKEY key)
{
    RegCloseKey(key);
}

//------------------------------------------------------------------------------
static bool set_value(HKEY key, const char* _name, const char* _value)
{
    wstr<> name(_name);
    wstr<> value(_value);

    LONG ok;
    ok = RegSetValueExW(key, name.c_str(), 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)(value.length() + 1) * sizeof(*value.c_str()));
    return (ok == ERROR_SUCCESS);
}

//------------------------------------------------------------------------------
static int32 get_value(HKEY key, const char* _name, char** _buffer)
{
    *_buffer = nullptr;

    wstr<> name(_name);

    DWORD type = 0;
    DWORD req_size = 0;
    int32 i = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &req_size);
    if (i != ERROR_SUCCESS && i != ERROR_MORE_DATA)
        return 0;
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return 0;

    WCHAR* buffer = static_cast<WCHAR*>(malloc(req_size + sizeof(*buffer)));
    if (!buffer)
        return 0;

    ZeroMemory(buffer, req_size + sizeof(*buffer));
    RegQueryValueExW(key, name.c_str(), nullptr, nullptr, (BYTE*)buffer, &req_size);
    assert(wcslen(buffer) <= req_size / sizeof(*buffer));

    int32 len = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    if (len)
    {
        char* out = static_cast<char*>(malloc(len));
        len = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, out, len, nullptr, nullptr);
        if (len)
        {
            // len is the buffer size, so find the string length.
            len = int32(strlen(out));
            assert(len);
            *_buffer = out;
            out = nullptr;
        }
        free(out);
    }

    free(buffer);

    return len;
}

//------------------------------------------------------------------------------
static bool delete_value(HKEY key, const char* _name)
{
    wstr<> name(_name);

    LONG ok;
    ok = RegDeleteValueW(key, name.c_str());
    return (ok == ERROR_SUCCESS);
}



//------------------------------------------------------------------------------
static HKEY open_cmd_proc_key(int32 all_users, int32 wow64, int32 writable)
{
    return open_software_key(all_users, "Microsoft\\Command Processor", wow64, writable);
}

//------------------------------------------------------------------------------
static bool check_registry_access()
{
    HKEY key;

    key = open_cmd_proc_key(g_all_users, 0, 1);
    if (key == nullptr)
        return false;

    close_key(key);

    key = open_cmd_proc_key(g_all_users, 1, 1);
    if (key == nullptr)
        return false;

    close_key(key);
    return true;
}

//------------------------------------------------------------------------------
static bool find_clink_entry(const char* value, int32* left, int32* right)
{
    int32 quoted = false;
    int32 i;
    const char* tag;
    const char* c;

    const char* needles[] = {
        "clink inject",
        "clink\" inject",
        "clink_x64.exe inject",
        "clink_x86.exe inject",
        "clink_arm64.exe inject",
        "clink_x64.exe\" inject",
        "clink_x86.exe\" inject",
        "clink_arm64.exe\" inject",
        "clink.bat inject",
        "clink.bat\" inject",
    };

    // Find roughly where clink's entry is.
    for (i = 0; i < sizeof_array(needles); ++i)
    {
        tag = strstr(value, needles[i]);
        if (tag != nullptr)
        {
            // Check if clink's path is quoted.  Checking the needle makes
            // sure not to get tricked by a false positive from something else
            // later on in the value.
            quoted = !!strchr(needles[i], ' ');
            break;
        }
    }

    if (tag == nullptr)
        return false;

    // Find right most extents of clink's entry.
    c = strchr(tag, '&');
    if (c != nullptr)
        *right = (int32)(c - value);
    else
        *right = (int32)strlen(value);

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
    *left = (int32)(c - value);

    return true;
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
static bool uninstall_autorun(const char* clink_path, int32 wow64)
{
    HKEY cmd_proc_key;
    char* key_value;
    int32 left, right;

    cmd_proc_key = open_cmd_proc_key(g_all_users, wow64, 1);
    if (cmd_proc_key == nullptr)
    {
        printf("ERROR: Failed to open registry key (%d)\n", GetLastError());
        return false;
    }

    key_value = nullptr;
    get_value(cmd_proc_key, "AutoRun", &key_value);

    bool ret = 0;
    if (key_value && find_clink_entry(key_value, &left, &right))
    {
        const char* read;
        char* write;
        int32 i, n;

        s_was_installed = true;

        // Copy the key value into itself, skipping clink's entry.
        read = write = key_value;
        for (i = 0, n = (int32)strlen(key_value); i <= n; ++i)
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
            ret = delete_value(cmd_proc_key, "AutoRun");
        }
        else
        {
            ret = set_value(cmd_proc_key, "AutoRun", read);
        }
    }
    else
    {
        s_was_installed = false;
        ret = true;
    }

    // Tidy up.
    close_key(cmd_proc_key);
    free(key_value);
    return ret;
}

//------------------------------------------------------------------------------
static bool install_autorun(const char* clink_path, int32 wow64)
{
    HKEY cmd_proc_key;
    const char* value;
    char* key_value;
    int32 i;

    // Remove any previous autorun entries so we never have more than one. We
    // could just check for an exisiting entry, but by always uninstalling and
    // reinstalling, we ensure clink's last in the chain, which allows it to
    // play nicely with other projects that hook cmd.exe and install autoruns.
    uninstall_autorun(clink_path, wow64);

    cmd_proc_key = open_cmd_proc_key(g_all_users, wow64, 1);
    if (cmd_proc_key == nullptr)
    {
        printf("ERROR: Failed to open registry key (%d)\n", GetLastError());
        return false;
    }

    key_value = nullptr;
    get_value(cmd_proc_key, "AutoRun", &key_value);

    i = key_value ? (int32)strlen(key_value) : 0;
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
    bool ret = set_value(cmd_proc_key, "AutoRun", value);

    // Tidy up.
    close_key(cmd_proc_key);
    free(new_value.data());
    free(key_value);
    return ret;
}

//------------------------------------------------------------------------------
static bool show_autorun()
{
    int32 all_users;

    puts("Current AutoRun values");

    for (all_users = 0; all_users < 2; ++all_users)
    {
        int32 wow64;

        printf("\n  %s:\n", all_users ? "All users" : "Current user");

        for (wow64 = 0; wow64 < 2; ++wow64)
        {
            HKEY cmd_proc_key;
            char* key_value;

            cmd_proc_key = open_cmd_proc_key(all_users, wow64, 0);
            if (cmd_proc_key == nullptr)
            {
                printf("ERROR: Failed to open registry key (%d)\n", GetLastError());
                return false;
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
    return true;
}

//------------------------------------------------------------------------------
static bool set_autorun_value(const char* value, int32 wow64)
{
    HKEY cmd_proc_key = open_cmd_proc_key(g_all_users, wow64, 1);
    if (cmd_proc_key == nullptr)
    {
        printf("ERROR: Failed to open registry key (%d)\n", GetLastError());
        return false;
    }

    bool ret;
    if (value == nullptr || *value == '\0')
        ret = delete_value(cmd_proc_key, "AutoRun");
    else
        ret = set_value(cmd_proc_key, "AutoRun", value);

    close_key(cmd_proc_key);
    return ret;
}

//------------------------------------------------------------------------------
static bool dispatch(dispatch_func_t* function, const char* clink_path)
{
    int32 wow64;
    int32 is_x64_os;
    SYSTEM_INFO system_info;

    GetNativeSystemInfo(&system_info);
    is_x64_os = system_info.wProcessorArchitecture;
    is_x64_os = (is_x64_os == PROCESSOR_ARCHITECTURE_AMD64);

    bool ok = true;
    for (wow64 = 0; wow64 <= is_x64_os; ++wow64)
    {
        ok &= function(clink_path, wow64);
    }

    return ok;
}

//------------------------------------------------------------------------------
static void print_help()
{
    static const char* const help_verbs[] = {
        "install [-- <args...>]", "Installs a command to cmd.exe's autorun to start Clink.",
        "uninstall",         "Does the opposite of 'install'.",
        "show",              "Displays the values of cmd.exe's autorun variables.",
        "set <string...>",   "Explicitly sets cmd.exe's autorun to <string>.",
        nullptr
    };

    static const char* const help_args[] = {
        "-a, --allusers",       "Modifies autorun for all users (requires admin rights).",
        "-h, --help",           "Shows this help text.",
        nullptr
    };

    puts_clink_header();

    puts("Usage: autorun [options] <verb> [<string>] [-- <clink_args>]\n");

    puts("Verbs:");
    puts_help(help_verbs, help_args);

    puts("Options:");
    puts_help(help_args, help_verbs);

    puts("Autorun simplifies making modifications to cmd.exe's autorun registry\n"
        "variables. The value of these variables are read and executed by cmd.exe when\n"
        "it starts. The 'install/uninstall' verbs add/remove the correct command to run\n"
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
static bool safe_append_quoted(str_base& s, const char* append)
{
    // https://devblogs.microsoft.com/oldnewthing/20100917-00/?p=12833
    // https://docs.microsoft.com/en-us/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way

    const char* quote = strchr(append, '\"');
    if (quote && quote[1])
        return false;

    unsigned backslashes = 0;
    const bool space = !!strchr(append, ' ');

    if (space)
    {
        for (const char* end = append + strlen(append); end-- > append && *end == '\\';)
            ++backslashes;
        s << "\"";
    }

    s << append;

    if (space)
    {
        if (quote)
            s.truncate(s.length() - 1);
        while (backslashes--)
            s << "\\";
        s << "\"";
    }

    return true;
}

//------------------------------------------------------------------------------
int32 autorun(int32 argc, char** argv)
{
    // Parse command line arguments.
    struct option options[] = {
        { "help",       no_argument,    nullptr, 'h' },
        { "allusers",   no_argument,    nullptr, 'a' },
        {}
    };

    str<MAX_PATH> clink_path;

    int32 i;
    int32 ret = 0;
    while ((i = getopt_long(argc, argv, "?ha", options, nullptr)) != -1)
    {
        switch (i)
        {
        case 'a':
            g_all_users = 1;
            break;

        case '?':
        case 'h':
            print_help();
            return 0;

        default:
            return 1;
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
            if (!safe_append_quoted(g_clink_args, argv[i]))
            {
                printf("ERROR: Invalid quoting for argument '%s'.\n", argv[i]);
                puts("Is a backslash missing?  On Windows, double backslashes before a quote.");
                puts("For example \"c:\\foo bar\\\\\" instead of \"c:\\foo bar\\\".");
                return 1;
            }
            if (i < argc - 1)
                g_clink_args << " ";
        }
    }

    // If we can't continue any further then warn the user.
    if (function == nullptr)
    {
        puts("ERROR: Invalid arguments. Run 'clink autorun --help' for info.");
        return 1;
    }

    // Do the magic.
    if (!check_registry_access())
    {
        puts("You must have administrator rights to access cmd.exe's autorun.");
        return 1;
    }

    const char* arg = clink_path.c_str();
    arg = *arg ? arg : g_clink_args.c_str();
    ret = !dispatch(function, arg);

    // Provide the user with some feedback.
    if (ret == 0)
    {
        const char* msg = nullptr;

        if (function == install_autorun)
            msg = "Clink successfully installed to run when cmd.exe starts";
        else if (function == uninstall_autorun)
            msg = (s_was_installed ?
                   "Clink's autorun entry has been removed" :
                   "Clink does not have an autorun entry");
        else if (function == set_autorun_value)
            msg = "Cmd.exe's AutoRun registry key set successfully";

        if (msg != nullptr)
            success_message(msg);
    }

    return ret;
}
