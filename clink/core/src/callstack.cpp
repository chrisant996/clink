// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

// WARNING:  Cannot rely on global scope constructors running before these
// functions are used.  The ensure() function and its associated data are
// designed to accommodate that.

#include "pch.h"

#ifdef DEBUG

#include "callstack.h"
#include "debugheap.h"
#include "assert_improved.h"

#include <stdio.h>
#include <stdlib.h>
#include <DbgHelp.h>
#include <assert.h>

static CRITICAL_SECTION s_cs;
static HANDLE s_process;

using func_RtlCaptureStackBackTrace_t = USHORT (WINAPI *)(ULONG FramesToSkip, ULONG FramesToCapture, PVOID *BackTrace, PULONG BackTraceHash);
using func_SymInitialize_t = BOOL (WINAPI *)(HANDLE hProcess, LPSTR UserSearchPath, BOOL fInvadeProcess);
using func_SymSetOptions_t = BOOL (WINAPI *)(DWORD SymOptions);
using func_SymLoadModule_t = BOOL (WINAPI *)(HANDLE hProcess, HANDLE hFile, PSTR ImageName, PSTR ModuleName, DWORD_PTR BaseOfDll, DWORD SizeOfDll);
using func_SymGetModuleInfo_t = BOOL (WINAPI *)(HANDLE hProcess, DWORD_PTR dwAddr, PIMAGEHLP_MODULE ModuleInfo);
using func_SymGetSymFromAddr_t = BOOL (WINAPI *)(HANDLE hProcess, DWORD_PTR dwAddr, PDWORD_PTR pdwDisplacement, PIMAGEHLP_SYMBOL Symbol);
using func_SymUnDName_t = BOOL (WINAPI *)(PIMAGEHLP_SYMBOL sym, LPSTR UnDecName, DWORD UnDecNameLength);

static union {
    FARPROC proc[7];
    struct {
        func_RtlCaptureStackBackTrace_t RtlCaptureStackBackTrace;
        func_SymInitialize_t SymInitialize;
        func_SymSetOptions_t SymSetOptions;
        func_SymLoadModule_t SymLoadModule;
        func_SymGetModuleInfo_t SymGetModuleInfo;
        func_SymGetSymFromAddr_t SymGetSymFromAddr;
        func_SymUnDName_t SymUnDName;
    };
} s_functions;

static void load_proc_address(FARPROC& proc, HINSTANCE hinst, const char* name, bool& failed)
{
    proc = hinst ? GetProcAddress(hinst, name) : nullptr;
    failed |= !proc;
}

static void init_dbghelp()
{
    char sympath[MAX_PATH * 4];
    char env[MAX_PATH];

    // Module path.
    GetModuleFileName(nullptr, sympath, _countof(sympath));
    sympath[_countof(sympath) - 1] = '\0';

    // String filename.
    char* last_sep = nullptr;
    for (char* walk = sympath; *walk; ++walk)
        if ('\\' == *walk || '/' == *walk)
            last_sep = walk;
    if (last_sep)
        *last_sep = '\0';

    // Append environment variable _NT_SYMBOL_PATH.
    if (GetEnvironmentVariableA("_NT_SYMBOL_PATH", env, _countof(env)))
    {
        strcat_s(sympath, _countof(sympath), ";");
        strcat_s(sympath, _countof(sympath), env);
    }

    // Append environment variable _NT_ALTERNATE_SYMBOL_PATH.
    if (GetEnvironmentVariableA("_NT_ALTERNATE_SYMBOL_PATH", env, _countof(env)))
    {
        strcat_s(sympath, _countof(sympath), ";");
        strcat_s(sympath, _countof(sympath), env);
    }

    // Append environment variable SYSTEMROOT.
    if (GetEnvironmentVariableA("SYSTEMROOT", env, _countof(env)))
    {
        strcat_s(sympath, _countof(sympath), ";");
        strcat_s(sympath, _countof(sympath), env);
        // And SYSTEMROOT\System32.
        strcat_s(sympath, _countof(sympath), ";");
        strcat_s(sympath, _countof(sympath), env);
        strcat_s(sympath, _countof(sympath), "\\System32");
    }

    // Initialize DBGHELP DLL.
    s_functions.SymInitialize(s_process, sympath, false);

    const DWORD options = SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_LOAD_ANYTHING|SYMOPT_IGNORE_CVREC;
    s_functions.SymSetOptions(options);
}

static bool ensure()
{
    static volatile LONG s_init = 0;
    static volatile LONG s_success = false;

    while (true)
    {
        switch (InterlockedCompareExchange(&s_init, 1, 0))
        {
        case 0:
            {
                bool failed = false;
                InitializeCriticalSection(&s_cs);
                s_process = GetCurrentProcess();
                memset(&s_functions, 0, sizeof(s_functions));
                HINSTANCE const hinst_ntdll = LoadLibrary("ntdll.dll");
                HINSTANCE const hinst_dbghelp = LoadLibrary("dbghelp.dll");
                size_t i = 0;
                load_proc_address(s_functions.proc[i++], hinst_ntdll, "RtlCaptureStackBackTrace", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymInitialize", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymSetOptions", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymLoadModule", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymGetModuleInfo", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymGetSymFromAddr", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymUnDName", failed);
                if (!failed)
                {
                    init_dbghelp();
                    s_success = true;
                }
            }
            InterlockedCompareExchange(&s_init, 2, 1);
            return s_success;
        case 1:
            Sleep( 0 );
            break;
        case 2:
            return s_success;
        }
    }
}

static void lock_dbghelp()
{
    EnterCriticalSection(&s_cs);
}

static void unlock_dbghelp()
{
    LeaveCriticalSection(&s_cs);
}

struct symbol_info
{
    char    module[MAX_MODULE_LEN];
    char    symbol[MAX_SYMBOL_LEN];
    size_t  offset;
};

static DWORD_PTR load_module_symbols(const void* frame)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQueryEx(s_process, const_cast<void*>(frame), &mbi, sizeof(mbi)))
    {
        if (mbi.Type & MEM_IMAGE)
        {
            char filename[MAX_PATH] = {};
            const DWORD len = GetModuleFileName((HINSTANCE)mbi.AllocationBase, filename, _countof(filename));
            filename[_countof(filename) - 1] = '\0';
            s_functions.SymLoadModule(s_process, nullptr, (len ? filename : nullptr), nullptr, (DWORD_PTR)mbi.AllocationBase, 0);
            return (DWORD_PTR)mbi.AllocationBase;
        }
    }
    return 0;
}

static void get_symbol_info(const void* frame, symbol_info& info)
{
    lock_dbghelp();

    memset(&info, 0, sizeof(info));

    IMAGEHLP_MODULE mi;
    mi.SizeOfStruct = sizeof(mi);

    // RtlCaptureStackBackTrace does not load symbols, so load symbols on
    // demand.  Twice...because it works around a problem where the first symbol
    // from a module can fail to be retrieved.
    load_module_symbols(frame);
    load_module_symbols(frame);

    if (s_functions.SymGetModuleInfo(s_process, DWORD_PTR(frame), &mi))
    {
        strcpy_s(info.module, _countof(info.module), mi.ModuleName);
        for (char *upper = info.module; *upper; ++upper)
            *upper = (char)toupper(static_cast<unsigned char>(*upper));
    }

    char undecorated[256];
    char* name = nullptr;

    // Reserve space for 256 characters in the Name field.
    union
    {
        IMAGEHLP_SYMBOL symbol;
        char buffer[sizeof(symbol) - 1 + 256];
    };

    __try
    {
        memset(&symbol, 0, sizeof(symbol));
        symbol.SizeOfStruct = sizeof(symbol);
        symbol.Address = DWORD_PTR(frame);
        symbol.MaxNameLength = sizeof(buffer) - sizeof(symbol);
        if (s_functions.SymGetSymFromAddr(s_process, DWORD_PTR(frame), &info.offset, &symbol))
        {
            name = symbol.Name;
            if (s_functions.SymUnDName(&symbol, undecorated, _countof(undecorated) - 1))
                name = undecorated;
            strcpy_s(info.symbol, _countof(info.symbol), name);
        }
    }
    __except( EXCEPTION_EXECUTE_HANDLER )
    {
        info.offset = reinterpret_cast<size_t>(frame);
    }

    unlock_dbghelp();
}

static size_t format_frame(const void* frame, char* buffer, size_t max)
{
    symbol_info info;
    get_symbol_info(frame, info);

    int out = 0;
    if (frame && info.module[0] || info.symbol[0])
    {
        static const char c_fmt_both[] = "%s! %s + 0x%zX";
#ifdef _WIN64
        static const char c_fmt_mod[] = "%s! 0x%p";
        static const char c_fmt_sym[] = "0x%p! %s + 0x%zX";
#else
        static const char c_fmt_mod[] = "%s! 0x%08X";
        static const char c_fmt_sym[] = "0x%08X! %s + 0x%X";
#endif
        if (info.module[0] && info.symbol[0])
            out = sprintf_s(buffer, max, c_fmt_both, info.module, info.symbol, info.offset);
        else if (info.module[0])
            out = sprintf_s(buffer, max, c_fmt_mod, info.module, frame);
        else
            out = sprintf_s(buffer, max, c_fmt_sym, frame, info.symbol, info.offset);
    }
    else
    {
#ifdef _WIN64
        static const char c_fmt[] = "<unknown> (0x%p)";
#else
        static const char c_fmt[] = "<unknown> (0x%08X)";
#endif
        out = sprintf_s(buffer, max, c_fmt, frame);
    }

    if (out < 0)
        return 0;
    return out;
}

CALLSTACK_EXTERN_C size_t format_callstack(int skip_frames, int total_frames, char* buffer, size_t capacity)
{
    const void* frames[40];
    if (total_frames > _countof(frames))
        total_frames = _countof(frames);

    const int captured = get_callstack_frames(skip_frames, total_frames, frames);
    return format_frames(captured, frames, buffer, capacity, true);
}

CALLSTACK_EXTERN_C int get_callstack_frames(int skip_frames, int total_frames, const void** frames)
{
    memset(frames, 0, total_frames * sizeof(*frames));

    if (!ensure())
        return 0;

    // WARNING: A Windows update in Sep 2020 broke RtlCaptureStackBackTrace such
    // that it sometimes returns 0.  So retry once and only complain if it fails
    // both times.
    static volatile LONG s_total_attempts = 0;
    static volatile LONG s_failed_attempts = 0;
    USHORT captured = 0;
    for (USHORT attempts = 2; !captured && attempts--;)
    {
        InterlockedIncrement(&s_total_attempts);
        captured = s_functions.RtlCaptureStackBackTrace(skip_frames + 1, total_frames, const_cast<void**>(frames), nullptr);
        if (!captured)
        {
            InterlockedIncrement(&s_failed_attempts);
            if (attempts && s_total_attempts >= 1000)
            {
                // I want to know if it fails more than once in every thousand times.
                assert(float(s_failed_attempts) / float(s_total_attempts) < 0.001);
            }
        }
    }

    // I want to know if it fails all the retries in a row.
    assert(captured);
    return captured;
}

CALLSTACK_EXTERN_C size_t format_frames(int total_frames, const void* const* frames, char* buffer, size_t max, int newlines)
{
    if (!max)
        return 0;

    if (!ensure())
    {
        *buffer = '\0';
        return 0;
    }

    char* const orig_buffer = buffer;
    max--;                              // Reserve space for null terminator.

    char tmp[MAX_FRAME_LEN];
    for (int i = 0; i < total_frames && frames[i]; i++)
    {
        char* s = tmp;
        size_t used = 0;

        if (newlines)
        {
            //s[used++] = '\t';
        }
        else
        {
            s[used++] = ' ';
            if (i)
            {
                s[used++] = '/';
                s[used++] = ' ';
            }
        }

        used += format_frame(frames[i], s, _countof(tmp) - used);

        if (newlines)
        {
            if (used + 1 < _countof(tmp)) tmp[used++] = '\r';
            if (used + 1 < _countof(tmp)) tmp[used++] = '\n';
            tmp[used++] = '\0';
        }

        const size_t copied = dbgcchcopy(tmp, buffer, max);
        buffer += copied;
        max -= copied;

        if (max <= 1)
            break;
    }

    *buffer = '\0';                     // Space was reserved at top.
    return buffer - orig_buffer;
}

void __cdecl _wassert(wchar_t const* message, wchar_t const* file, unsigned line)
{
    char stack[4096];
    wchar_t wstack[4096];
    format_callstack(1, 20, stack, _countof(stack));
    MultiByteToWideChar(CP_ACP, 0, stack, -1, wstack, _countof(wstack));

    wchar_t tmp[32];
    _itow_s(line, tmp, 10);

    wchar_t wbuffer[4096];
    wcscpy_s(wbuffer, _countof(wbuffer), message);
    wcscat_s(wbuffer, L"\r\n\r\n\r\nFile: ");
    wcscat_s(wbuffer, file);
    wcscat_s(wbuffer, L"\r\nLine: ");
    wcscat_s(wbuffer, tmp);
    wcscat_s(wbuffer, L"\r\n\r\n");
    wcscat_s(wbuffer, _countof(wbuffer), wstack);

    switch (MessageBoxW(nullptr, wbuffer, L"ASSERT", MB_ICONEXCLAMATION|MB_ABORTRETRYIGNORE|MB_DEFBUTTON3))
    {
    case IDABORT:   TerminateProcess(GetCurrentProcess(), -1); break;
    case IDRETRY:   DebugBreak(); break;
    }
}

CALLSTACK_EXTERN_C void dbgassertf(const char* file, unsigned line, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsprintf_s(buffer, _countof(buffer) - 4, fmt, args);
    buffer[_countof(buffer) - 5] = '\0';

    wchar_t wmessage[1024];
    wchar_t wfile[MAX_PATH];

    MultiByteToWideChar(CP_ACP, 0, buffer, -1, wmessage, _countof(wmessage));
    MultiByteToWideChar(CP_ACP, 0, file, -1, wfile, _countof(wfile));

    _wassert(wmessage, wfile, line);

    va_end(args);
}

CALLSTACK_EXTERN_C void dbgtracef(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsprintf_s(buffer, _countof(buffer) - 4, fmt, args);
    buffer[_countof(buffer) - 5] = '\0';
    strcat_s(buffer, _countof(buffer), "\r\n");

    OutputDebugStringA(buffer);

    va_end(args);
}

#endif
