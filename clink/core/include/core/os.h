// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <stdio.h>
#include <sys/stat.h>

class str_base;

#define _S_IFLNK        (0x0800)
#define S_IFLNK         _S_IFLNK

#define S_ISLNK(m)      (((m)&S_IFLNK) == S_IFLNK)

//------------------------------------------------------------------------------
namespace os
{

enum {
    path_type_invalid,
    path_type_file,
    path_type_dir,
};

enum {
    drive_type_unknown,
    drive_type_invalid,
    drive_type_remote,      // Remote (network) drive.
    drive_type_removable,   // Floppy, thumb drive, flash card reader, CD-ROM, etc.
    drive_type_fixed,       // Hard drive, flash drive, etc.
    drive_type_ramdisk,     // RAM disk.
};

enum temp_file_mode {
    normal              = 0x0000,   // text mode (translate line endings)
    binary              = 0x0001,   // binary mode (no translation)
    delete_on_close     = 0x0002,   // delete on close (requires FILE_SHARE_DELETE)
};

enum argv_quote_mode {
    for_createprocess   = 0x0000,   // quote for CreateProcess, if arg requires quoting
    for_cmd             = 0x0001,   // quote for cmd.exe, if arg requires quoting
    force               = 0x0002,   // force arg to be quoted
};

DEFINE_ENUM_FLAG_OPERATORS(temp_file_mode);

class high_resolution_clock
{
public:
                high_resolution_clock();
    double      elapsed() const;
private:
    double      m_freq;
    long long   m_start;
};

struct cwd_restorer
{
    cwd_restorer();
    ~cwd_restorer();
    wchar_t m_path[288];
};

DWORD   get_file_attributes(const wchar_t* path, bool* symlink=nullptr);
DWORD   get_file_attributes(const char* path, bool* symlink=nullptr);
int     get_path_type(const char* path);
int     get_drive_type(const char* path, unsigned int len=-1);
int     get_file_size(const char* path);
bool    is_hidden(const char* path);
void    get_current_dir(str_base& out);
bool    set_current_dir(const char* dir);
bool    make_dir(const char* dir);
bool    remove_dir(const char* dir);
bool    unlink(const char* path);
bool    move(const char* src_path, const char* dest_path);
bool    copy(const char* src_path, const char* dest_path);
bool    get_temp_dir(str_base& out);
FILE*   create_temp_file(str_base* out=nullptr, const char* prefix=nullptr, const char* ext=nullptr, temp_file_mode mode=normal, const char* path=nullptr);
bool    expand_env(const char* in, unsigned int in_len, str_base& out, int* point=nullptr);
bool    get_env(const char* name, str_base& out);
bool    set_env(const char* name, const char* value);
bool    get_alias(const char* name, str_base& out);
bool    get_short_path_name(const char* path, str_base& out);
bool    get_long_path_name(const char* path, str_base& out);
bool    get_full_path_name(const char* path, str_base& out, unsigned int len=-1);
bool    get_net_connection_name(const char* path, str_base& out);
double  clock();
time_t  filetime_to_time_t(const FILETIME& ft);
bool    get_clipboard_text(str_base& out);
bool    set_clipboard_text(const char* text, int length);
bool    disambiguate_abbreviated_path(const char*& in, str_base& out);

#if 0
void    append_argv(str_base& out, const char* arg, argv_quote_mode mode);
#endif

void    map_errno();
void    map_errno(unsigned long const oserrno);

void    set_errorlevel(int errorlevel);
int     get_errorlevel();

void    set_shellname(const wchar_t* shell_name);
const wchar_t* get_shellname();

int     system(const char* command, const char* cwd);
HANDLE  spawn_internal(const char* command, const char* cwd, HANDLE hin, HANDLE hout);

HANDLE  dup_handle(HANDLE process_handle, HANDLE h, bool inherit=false);

}; // namespace os
