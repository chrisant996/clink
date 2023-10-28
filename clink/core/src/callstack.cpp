// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

// WARNING:  Cannot rely on global scope constructors running before these
// functions are used.  The ensure() function and its associated data are
// designed to accommodate that.

#include "pch.h"

#if defined(DEBUG) && defined(_MSC_VER)

#include "callstack.h"
#include "debugheap.h"
#include "assert_improved.h"

#include <stdio.h>
#include <stdlib.h>
#include <DbgHelp.h>

//#define DBGHELP_DEBUG_OUTPUT

using func_SymInitialize_t = BOOL (WINAPI *)(HANDLE hProcess, LPSTR UserSearchPath, BOOL fInvadeProcess);
using func_SymSetOptions_t = BOOL (WINAPI *)(DWORD SymOptions);
using func_SymRegisterCallback_t = BOOL (WINAPI *)(HANDLE hProcess, PSYMBOL_REGISTERED_CALLBACK CallbackFunction, ULONG_PTR UserContext);
using func_SymLoadModule_t = BOOL (WINAPI *)(HANDLE hProcess, HANDLE hFile, PSTR ImageName, PSTR ModuleName, DWORD_PTR BaseOfDll, DWORD SizeOfDll);
using func_SymGetModuleInfo_t = BOOL (WINAPI *)(HANDLE hProcess, DWORD_PTR dwAddr, PIMAGEHLP_MODULE ModuleInfo);
using func_SymGetSymFromAddr_t = BOOL (WINAPI *)(HANDLE hProcess, DWORD_PTR dwAddr, PDWORD_PTR pdwDisplacement, PIMAGEHLP_SYMBOL Symbol);
using func_SymUnDName_t = BOOL (WINAPI *)(PIMAGEHLP_SYMBOL sym, LPSTR UnDecName, DWORD UnDecNameLength);
using func_StackWalk_t = BOOL (WINAPI *)(DWORD MachineType, HANDLE hProcess, HANDLE hThread,
                                         LPSTACKFRAME StackFrame, PVOID ContextRecord,
                                         PREAD_PROCESS_MEMORY_ROUTINE ReadMemoryRoutine,
                                         PFUNCTION_TABLE_ACCESS_ROUTINE FunctionTableAccessRoutine,
                                         PGET_MODULE_BASE_ROUTINE GetModuleBaseRoutine,
                                         PTRANSLATE_ADDRESS_ROUTINE TranslateAddress);
using func_SymFunctionTableAccess_t = PVOID (WINAPI *)(HANDLE hProcess, DWORD_PTR AddrBase);
using func_RtlCaptureStackBackTrace_t = WORD (WINAPI *)(DWORD FramesToSkip, DWORD FramesToCapture, PVOID* BackTrace, PDWORD BackTraceHash);

union function_table
{
    FARPROC proc[10];
    struct {
        func_SymInitialize_t SymInitialize;
        func_SymSetOptions_t SymSetOptions;
        func_SymRegisterCallback_t SymRegisterCallback;
        func_SymLoadModule_t SymLoadModule;
        func_SymGetModuleInfo_t SymGetModuleInfo;
        func_SymGetSymFromAddr_t SymGetSymFromAddr;
        func_SymUnDName_t SymUnDName;
        func_StackWalk_t StackWalk;
        func_SymFunctionTableAccess_t SymFunctionTableAccess;
        func_RtlCaptureStackBackTrace_t RtlCaptureStackBackTrace;
    };
};

struct symbol_info
{
    char        module[MAX_MODULE_LEN];
    char        symbol[MAX_SYMBOL_LEN];
    DWORD_PTR   offset;
};

class dbghelp
{
    friend class function_access;

public:
    dbghelp();
    ~dbghelp();

    function_access lock(bool lock=true);

private:
    static const function_table* ensure();
    static void init(function_table& ft);
    void lock_internal();
    void unlock_internal();

    const bool m_ensured = false;
#ifdef DEBUG
    int m_locked = 0;
#endif

    static CRITICAL_SECTION s_cs;
    static HANDLE s_process;
};

class function_access
{
public:
    function_access(dbghelp* dh=nullptr, const function_table* funcs=nullptr, bool lock=false);
    ~function_access();
    operator bool() const { return m_dh && m_funcs; }
    const function_table& call() const { return *m_funcs; }
    HANDLE get_process() const;
    void unlock();
    void relock();
private:
    dbghelp* const m_dh = nullptr;
    const function_table* const m_funcs = nullptr;
    bool m_locked = false;
};

CRITICAL_SECTION dbghelp::s_cs;
HANDLE dbghelp::s_process;

static void __dbghelp_assert(const char* file, uint32 line, const char* message)
{
    wchar_t wmessage[1024];
    wchar_t wfile[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, message, -1, wmessage, _countof(wmessage));
    MultiByteToWideChar(CP_ACP, 0, file, -1, wfile, _countof(wfile));

    wchar_t wbuffer[4096];

    swprintf_s(wbuffer, L"ASSERT:  %ls @li%u:  %ls\r\n", wfile, line, wmessage);
    OutputDebugStringW(wbuffer);

    swprintf_s(wbuffer, L"%ls\r\n\r\n\r\nFile: %ls\r\nLine: %u", wmessage, wfile, line);
    switch (MessageBoxW(nullptr, wbuffer, L"ASSERT", MB_ICONEXCLAMATION|MB_ABORTRETRYIGNORE|MB_DEFBUTTON3))
    {
    case IDABORT:   TerminateProcess(GetCurrentProcess(), -1); break;
    case IDRETRY:   DebugBreak(); break;
    }
}

#define dbghelp_assert(expr) do { if (!(expr)) __dbghelp_assert(__FILE__, __LINE__, #expr); } while (false)
#undef assert
#undef assertimplies

function_access::function_access(dbghelp* dh, const function_table* funcs, bool lock)
: m_dh(dh)
, m_funcs(funcs)
{
    if (m_dh && m_funcs && lock)
    {
        m_dh->lock_internal();
        m_locked = true;
    }
}

function_access::~function_access()
{
    if (m_locked)
        m_dh->unlock_internal();
}

HANDLE function_access::get_process() const
{
    dbghelp_assert(m_dh);
    return m_dh ? m_dh->s_process : 0;
}

void function_access::unlock()
{
    dbghelp_assert(m_locked);
    dbghelp_assert(m_dh && m_funcs);
    if (m_locked)
        m_dh->unlock_internal();
}

void function_access::relock()
{
    dbghelp_assert(!m_locked);
    dbghelp_assert(m_dh && m_funcs);
    if (m_dh && m_funcs)
        m_dh->lock_internal();
}

static void load_proc_address(FARPROC& proc, HINSTANCE hinst, const char* name, bool& failed)
{
    proc = hinst ? GetProcAddress(hinst, name) : nullptr;
    failed |= !proc;
}

#ifdef DBGHELP_DEBUG_OUTPUT
static BOOL WINAPI registered_callback(HANDLE process, DWORD action_code, ULONG_PTR callback_data, ULONG_PTR user_context)
{
    switch (action_code)
    {
    case CBA_DEBUG_INFO:
        OutputDebugString((const char*)callback_data);
        return true;
    default:
        return false;
    }
}
#endif

static bool get_module_filename(HANDLE process, void* address, char* buffer, DWORD max)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (address && VirtualQueryEx(process, address, &mbi, sizeof(mbi)))
    {
        if (mbi.Type & MEM_IMAGE)
            address = mbi.AllocationBase;
    }

    buffer[0] = '\0';
    GetModuleFileName(HINSTANCE(address), buffer, max);
    buffer[max - 1] = '\0';

    char* last_sep = nullptr;
    for (char* walk = buffer; *walk; ++walk)
        if ('\\' == *walk || '/' == *walk)
            last_sep = walk;
    if (last_sep)
        *last_sep = '\0';

    return *buffer;
}

void dbghelp::init(function_table& ft)
{
    char sympath[MAX_PATH * 4];
    char env[MAX_PATH];

    sympath[0] = '\0';

    // This module's path.
    if (get_module_filename(s_process, init, env, _countof(env)))
    {
        if (sympath[0])
            dbgcchcat(sympath, _countof(sympath), ";");
        dbgcchcat(sympath, _countof(sympath), env);
    }

    // Process module's path.
    if (get_module_filename(s_process, nullptr, env, _countof(env)))
    {
        if (sympath[0])
            dbgcchcat(sympath, _countof(sympath), ";");
        dbgcchcat(sympath, _countof(sympath), env);
    }

    // Append environment variable _NT_SYMBOL_PATH.
    if (GetEnvironmentVariableA("_NT_SYMBOL_PATH", env, _countof(env)))
    {
        dbgcchcat(sympath, _countof(sympath), ";");
        dbgcchcat(sympath, _countof(sympath), env);
    }

    // Append environment variable _NT_ALTERNATE_SYMBOL_PATH.
    if (GetEnvironmentVariableA("_NT_ALTERNATE_SYMBOL_PATH", env, _countof(env)))
    {
        dbgcchcat(sympath, _countof(sympath), ";");
        dbgcchcat(sympath, _countof(sympath), env);
    }

    // Append environment variable SYSTEMROOT.
    if (GetEnvironmentVariableA("SYSTEMROOT", env, _countof(env)))
    {
        dbgcchcat(sympath, _countof(sympath), ";");
        dbgcchcat(sympath, _countof(sympath), env);
        // And SYSTEMROOT\System32.
        dbgcchcat(sympath, _countof(sympath), ";");
        dbgcchcat(sympath, _countof(sympath), env);
        dbgcchcat(sympath, _countof(sympath), "\\System32");
    }

    // Initialize DBGHELP DLL.
#ifdef DBGHELP_DEBUG_OUTPUT
    dbgtracef("DBGHELP: SYMPATH=%s", sympath);
#endif
    ft.SymInitialize(s_process, sympath, false);

    DWORD options = SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_LOAD_ANYTHING|SYMOPT_IGNORE_CVREC;
#ifdef DBGHELP_DEBUG_OUTPUT
    ft.SymRegisterCallback(s_process, registered_callback, 0);
    options |= SYMOPT_DEBUG;
#endif
    ft.SymSetOptions(options);
}

const function_table* dbghelp::ensure()
{
    static volatile LONG s_init = 0;
    static volatile LONG s_success = false;
    static function_table s_functions = { 0 };

    while (true)
    {
        switch (InterlockedCompareExchange(&s_init, 1, 0))
        {
        case 0:
            {
                bool failed = false;
                bool optional = false;
                InitializeCriticalSection(&s_cs);
                s_process = GetCurrentProcess();
                memset(&s_functions, 0, sizeof(s_functions));
                HINSTANCE const hinst_ntdll = LoadLibrary("ntdll.dll");
                HINSTANCE const hinst_dbghelp = LoadLibrary("dbghelp.dll");
                size_t i = 0;
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymInitialize", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymSetOptions", failed);
#ifdef _WIN64
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymRegisterCallback64", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymLoadModule64", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymGetModuleInfo64", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymGetSymFromAddr64", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymUnDName64", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "StackWalk64", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymFunctionTableAccess64", failed);
#else
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymRegisterCallback", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymLoadModule", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymGetModuleInfo", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymGetSymFromAddr", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymUnDName", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "StackWalk", failed);
                load_proc_address(s_functions.proc[i++], hinst_dbghelp, "SymFunctionTableAccess", failed);
#endif
                load_proc_address(s_functions.proc[i++], hinst_ntdll, "RtlCaptureStackBackTrace", optional);
                if (!failed)
                {
                    init(s_functions);
                    s_success = true;
                }
                InterlockedCompareExchange(&s_init, 2, 1);
                // Must go after updating s_init, or it can deadlock.
                dbghelp_assert(i == sizeof(s_functions) / sizeof(FARPROC));
            }
            return s_success ? &s_functions : nullptr;
        case 1:
            Sleep(0);
            break;
        case 2:
            return s_success ? &s_functions : nullptr;
        }
    }
}

dbghelp::dbghelp()
{
}

dbghelp::~dbghelp()
{
    dbghelp_assert(!m_locked);
}

function_access dbghelp::lock(bool lock)
{
    const function_table* const ft = ensure();
    if (ft)
        return function_access(this, ft, lock);
    else
        return function_access();
}

void dbghelp::lock_internal()
{
    EnterCriticalSection(&s_cs);
    ++m_locked;
    dbghelp_assert(m_locked == 1); // Performance alert; lock scopes will be broader than intended.
}

void dbghelp::unlock_internal()
{
    dbghelp_assert(m_locked);
    --m_locked;
    LeaveCriticalSection(&s_cs);
}

static DWORD_PTR load_module_symbols(function_access& fa, HANDLE process, void* frame)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQueryEx(process, frame, &mbi, sizeof(mbi)))
    {
        if (mbi.Type & MEM_IMAGE)
        {
            char filename[MAX_PATH] = {};
            const DWORD len = GetModuleFileName((HINSTANCE)mbi.AllocationBase, filename, _countof(filename));
            filename[_countof(filename) - 1] = '\0';

#ifdef DBGHELP_DEBUG_OUTPUT
            fa.unlock();
            dbgtracef("DBGHELP: load symbols for 0x%p: file %s (base 0x%p)", frame, filename, mbi.AllocationBase);
            fa.relock();
#endif
            // Load twice because sometimes the first symbol from a module fails
            // to be retrieved.
            fa.call().SymLoadModule(process, nullptr, (len ? filename : nullptr), nullptr, (DWORD_PTR)mbi.AllocationBase, 0);
            fa.call().SymLoadModule(process, nullptr, (len ? filename : nullptr), nullptr, (DWORD_PTR)mbi.AllocationBase, 0);
            return (DWORD_PTR)mbi.AllocationBase;
        }
    }
    return 0;
}

static void get_symbol_info(void* frame, symbol_info& info)
{
    memset(&info, 0, sizeof(info));

    dbghelp dh;
    if (function_access fa = dh.lock())
    {
        IMAGEHLP_MODULE mi;
        mi.SizeOfStruct = sizeof(mi);

        HANDLE process = fa.get_process();

        if (fa.call().RtlCaptureStackBackTrace)
        {
            // RtlCaptureStackBackTrace requires the caller to load symbols.
            load_module_symbols(fa, process, frame);
        }

        if (fa.call().SymGetModuleInfo(process, DWORD_PTR(frame), &mi))
        {
            dbgcchcopy(info.module, _countof(info.module), mi.ModuleName);
            for (char *upper = info.module; *upper; ++upper)
                *upper = (char)toupper(uint8(*upper));
        }
        else
        {
#ifdef DBGHELP_DEBUG_OUTPUT
            fa.unlock();
            dbgtracef("DBGHELP: no module for 0x%p", DWORD_PTR(frame));
            fa.relock();
#endif
        }

        char undecorated[MAX_SYMBOL_LEN];
        char* name = nullptr;

        // Reserve space in the Name field.
        union
        {
            IMAGEHLP_SYMBOL symbol;
            char buffer[sizeof(symbol) + 1024];
        };

        __try
        {
            memset(&symbol, 0, sizeof(symbol));
            symbol.SizeOfStruct = sizeof(symbol);
            symbol.Address = DWORD_PTR(frame);
            symbol.MaxNameLength = sizeof(buffer) - (sizeof(symbol));
            if (fa.call().SymGetSymFromAddr(process, DWORD_PTR(frame), &info.offset, &symbol))
            {
                name = symbol.Name;
                if (fa.call().SymUnDName(&symbol, undecorated, _countof(undecorated) - 1))
                    name = undecorated;
                dbgcchcopy(info.symbol, _countof(info.symbol), name);
                if (strlen(info.symbol) == _countof(info.symbol) - 1)
                    memset(info.symbol + _countof(info.symbol) - 4, '.', 3);
            }
        }
        __except( EXCEPTION_EXECUTE_HANDLER )
        {
            info.offset = reinterpret_cast<size_t>(frame);
        }
    }
}

static size_t format_frame(void* frame, char* buffer, size_t max, bool condense)
{
    symbol_info info;
    get_symbol_info(frame, info);

    int32 out = 0;
    if (frame && condense && info.symbol[0])
    {
        static const char c_fmt[] = "%s + 0x%zX";
        out = _snprintf_s(buffer, max, _TRUNCATE, c_fmt, info.symbol, info.offset);
    }
    else if (frame && info.module[0] || info.symbol[0])
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
            out = _snprintf_s(buffer, max, _TRUNCATE, c_fmt_both, info.module, info.symbol, info.offset);
        else if (info.module[0])
            out = _snprintf_s(buffer, max, _TRUNCATE, c_fmt_mod, info.module, frame);
        else
            out = _snprintf_s(buffer, max, _TRUNCATE, c_fmt_sym, frame, info.symbol, info.offset);
    }
    else
    {
#ifdef _WIN64
        static const char c_fmt[] = "<unknown> (0x%p)";
#else
        static const char c_fmt[] = "<unknown> (0x%08X)";
#endif
        out = _snprintf_s(buffer, max, _TRUNCATE, c_fmt, frame);
    }

    if (out < 0)
        return 0;
    return out;
}

static DWORD stackframe_from_context(CONTEXT* context, STACKFRAME* stackframe)
{
    memset(stackframe, 0, sizeof(*stackframe));
#if defined(_M_IX86)
    stackframe->AddrPC.Offset       = context->Eip;
    stackframe->AddrPC.Mode         = AddrModeFlat;
    stackframe->AddrStack.Offset    = context->Esp;
    stackframe->AddrStack.Mode      = AddrModeFlat;
    stackframe->AddrFrame.Offset    = context->Ebp;
    stackframe->AddrFrame.Mode      = AddrModeFlat;
    return IMAGE_FILE_MACHINE_I386;
#elif defined(_M_AMD64)
    stackframe->AddrPC.Offset       = context->Rip;
    stackframe->AddrPC.Mode         = AddrModeFlat;
    stackframe->AddrStack.Offset    = context->Rsp;
    stackframe->AddrStack.Mode      = AddrModeFlat;
    stackframe->AddrFrame.Offset    = context->Rbp;
    stackframe->AddrFrame.Mode      = AddrModeFlat;
    return IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
    stackframe->AddrPC.Offset       = context->Pc;
    stackframe->AddrPC.Mode         = AddrModeFlat;
    stackframe->AddrStack.Offset    = context->Sp;
    stackframe->AddrStack.Mode      = AddrModeFlat;
    stackframe->AddrFrame.Offset    = context->Fp;
    stackframe->AddrFrame.Mode      = AddrModeFlat;
    return IMAGE_FILE_MACHINE_ARM64;
#else
#error Unknown Target Machine
#endif
}

static function_access* s_fa = nullptr;

static void* WINAPI cb_function_table_access(HANDLE process, DWORD_PTR addr)
{
    void* pv = s_fa->call().SymFunctionTableAccess(process, addr);

    if (pv)
        return pv;

    // Hash the address into a bit table to avoid repeated failures to load
    // symbols for a given module.  This assumes modules are not unloaded, and
    // other modules loaded into the same region.  It's not really true, but it
    // ends up working well enough for my purposes.
#ifdef _WIN64
    constexpr size_t c_range_kb = 8589934592;   // 8192 GB
#else
    constexpr size_t c_range_kb = 3145728;      // 3 GB
#endif
    constexpr size_t c_granularity_kb = 64;
    constexpr size_t c_dword_bits = sizeof(DWORD) * 8;
    constexpr size_t c_bitmap_size = c_range_kb / c_granularity_kb / c_dword_bits;

    static DWORD* s_bitmap = static_cast<DWORD*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, c_bitmap_size * sizeof(*s_bitmap)));

    const size_t slot = addr / (c_granularity_kb * 1024);

    if (s_bitmap[(slot / c_dword_bits) % c_bitmap_size] & (1u << (slot % c_dword_bits)))
        return nullptr;

    if (!load_module_symbols(*s_fa, process, reinterpret_cast<void*>(addr)))
        return nullptr;

    pv = s_fa->call().SymFunctionTableAccess(process, addr);

    if (!pv)
        s_bitmap[(slot / c_dword_bits) % c_bitmap_size] |= 1u << (slot % c_dword_bits);

    return pv;
}

static DWORD_PTR WINAPI cb_get_module_base(HANDLE process, DWORD_PTR addr)
{
    IMAGEHLP_MODULE mi;
    mi.SizeOfStruct = sizeof(mi);
    if (s_fa->call().SymGetModuleInfo(process, addr, &mi))
        return mi.BaseOfImage;
    return load_module_symbols(*s_fa, process, reinterpret_cast<void*>(addr));
}

CALLSTACK_EXTERN_C size_t format_callstack(int32 skip_frames, int32 total_frames, char* buffer, size_t capacity, int32 newlines)
{
    void* frames[40];
    if (total_frames > _countof(frames))
        total_frames = _countof(frames);

    DWORD hash;
    const int32 captured = get_callstack_frames(skip_frames, total_frames, frames, &hash);
    return format_frames(captured, frames, hash, buffer, capacity, newlines);
}

CALLSTACK_EXTERN_C int32 get_callstack_frames(int32 skip_frames, int32 total_frames, void** frames, DWORD* hash)
{
    memset(frames, 0, total_frames * sizeof(*frames));
    if (hash)
        *hash = 0;

    dbghelp dh;
    function_access fa = dh.lock(false);
    if (!fa)
        return 0;

    if (fa.call().RtlCaptureStackBackTrace)
    {
        // WARNING: A Windows update in Sep 2020 broke RtlCaptureStackBackTrace
        // such that now it sometimes returns 0.  Retry once and only complain
        // if it fails both times.
        static volatile LONG s_total_attempts = 0;
        static volatile LONG s_failed_attempts = 0;
        uint32 captured = 0;
        for (uint32 attempts = 2; !captured && attempts--;)
        {
            InterlockedIncrement(&s_total_attempts);
            captured = RtlCaptureStackBackTrace(skip_frames + 1, total_frames, frames, hash);
            if (!captured)
            {
                InterlockedIncrement(&s_failed_attempts);
                if (attempts && s_total_attempts >= 1000)
                {
                    // I want to know if it fails more than very rarely.
                    if (s_total_attempts >= 1000)
                        dbghelp_assert(float(s_failed_attempts) / float(s_total_attempts) < 0.005);
                    else if (s_total_attempts >= 100)
                        dbghelp_assert(float(s_failed_attempts) / float(s_total_attempts) < 0.01);
                    else if (s_total_attempts >= 10)
                        dbghelp_assert(float(s_failed_attempts) / float(s_total_attempts) <= 0.2);
                }
            }
        }
        // I want to know if it fails all the retries in a row.
        dbghelp_assert(captured);
        return captured;
    }

    DWORD_PTR* pdw = reinterpret_cast<DWORD_PTR*>(frames);
    HANDLE const thread = GetCurrentThread();

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&context);

    STACKFRAME stackframe;
    const DWORD machine = stackframe_from_context(&context, &stackframe);

    skip_frames += 2; // Skip this function and RtlCaptureContext.

    if (hash)
        *hash = 0;

    dbghelp_assert(!s_fa);
    s_fa = &fa;

    int32 captured = 0;
    for (int32 i = 0; i < skip_frames + total_frames; ++i)
    {
        fa.relock();
        const bool walked = !!fa.call().StackWalk(
            machine, fa.get_process(), thread, &stackframe, &context, nullptr,
            cb_function_table_access, cb_get_module_base, nullptr);
        const DWORD err = walked ? NOERROR : GetLastError();
        fa.unlock();

        if (!walked)
        {
            //dbgtracef("!StackWalk, err = %d (0x%08x)", err, err);
            break;
        }

        if (i >= skip_frames)
        {
            if (hash)
            {
                if (!*hash)
                    *hash = 8191;
#ifdef _WIN64
                *hash *= 5;
                *hash += DWORD(stackframe.AddrPC.Offset >> 32);
#endif
                *hash *= 5;
                *hash += DWORD(stackframe.AddrPC.Offset);
            }

            *(pdw++) = stackframe.AddrPC.Offset;
            captured++;
        }
    }

    dbghelp_assert(&fa == s_fa);
    s_fa = nullptr;

    return captured;
}

CALLSTACK_EXTERN_C size_t format_frames(int32 total_frames, void* const* frames, DWORD hash, char* buffer, size_t max, int32 newlines)
{
    if (!max)
        return 0;

    {
        dbghelp dh;
        if (!dh.lock(false))
        {
            *buffer = '\0';
            return 0;
        }
    }

    char* const orig_buffer = buffer;
    max--;                              // Reserve space for null terminator.

    if (hash)
    {
        static const char c_fmt[] = "HASH 0x%08X%s";
        const size_t copied = _snprintf_s(buffer, max, _TRUNCATE, c_fmt, hash, newlines ? "\r\n" : " / ");
        buffer += copied;
        max -= copied;
    }

    char tmp[MAX_FRAME_LEN];
    for (int32 i = 0; i < total_frames && frames[i]; i++)
    {
        char* s = tmp;
        size_t used = 0;

        if (newlines)
        {
            //s[used++] = '\t';
        }
        else if (i > 0)
        {
            s[used++] = ' ';
            s[used++] = '/';
            s[used++] = ' ';
        }

        used += format_frame(frames[i], s + used, _countof(tmp) - used, !newlines);

        if (newlines)
        {
            if (used + 1 < _countof(tmp)) tmp[used++] = '\r';
            if (used + 1 < _countof(tmp)) tmp[used++] = '\n';
            tmp[used++] = '\0';
        }

        const size_t copied = dbgcchcopy(buffer, max, tmp);
        buffer += copied;
        max -= copied;

        if (max <= 1)
            break;
    }

    *buffer = '\0';                     // Space was reserved at top.
    return buffer - orig_buffer;
}

extern "C" void _wassert(wchar_t const* message, wchar_t const* file, unsigned line)
{
    char stack[4096];
    wchar_t wstack[4096];
    format_callstack(1, 20, stack, _countof(stack), true);
    MultiByteToWideChar(CP_ACP, 0, stack, -1, wstack, _countof(wstack));

    wchar_t tmp[32];
    _itow_s(line, tmp, 10);

    wchar_t wbuffer[4096];
    wcscpy_s(wbuffer, L"ASSERT:  ");
    wcscat_s(wbuffer, file);
    wcscat_s(wbuffer, L" @li");
    wcscat_s(wbuffer, tmp);
    wcscat_s(wbuffer, L":  ");
    wcscat_s(wbuffer, message);
    wcscat_s(wbuffer, L"\r\n");
#ifdef TRACE_ASSERT_STACK
    wcscat_s(wbuffer, _countof(wbuffer), wstack);
#endif

    OutputDebugStringW(wbuffer);

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
    vsnprintf_s(buffer, _countof(buffer) - 4, _TRUNCATE, fmt, args);
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

    char buffer[8192];
    size_t size = _countof(buffer) - 4;
    int32 used = _snprintf_s(buffer, size, _TRUNCATE, "%u\t", GetCurrentThreadId());
    used += vsnprintf_s(buffer + used, size - used, _TRUNCATE, fmt, args);
    buffer[_countof(buffer) - 5] = '\0';
    dbgcchcat(buffer + used, size - used, "\r\n");

    OutputDebugStringA(buffer);

    va_end(args);
}

#endif // DEBUG && _MSC_VER
