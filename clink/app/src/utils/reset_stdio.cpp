// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "reset_stdio.h"

#include <core/log.h>

#include <assert.h>

/*
    Notes for future debugging if/when the C runtime changes such that this
    technique no longer compiles or works:

    1.  Make some .lua script that uses `print("hello")` or some other message.
    2.  `start "repro" cmd.exe /k {...path_to...}\Cmder\vendor\init.bat`
    3.  Observe whether the "hello" message shows up.
        - If yes, then the issue didn't repro.  Try again.
        - If no, then the issue repro'd.

    The root cause is redirection in CMD.EXE changing the STD handles.  So
    another way to encounter the problems is by async prompt filtering.
*/

#if !defined(__MINGW32__) && !defined(__MINGW64__)

//------------------------------------------------------------------------------
static bool s_can_reset = true;

//------------------------------------------------------------------------------
// Use static initializers to get the Std handles at the same time the C runtime
// initializes itself and runs __acrt_initialize_lowio() to get the handles.
static intptr_t s_hStdin = reinterpret_cast<intptr_t>(GetStdHandle(STD_INPUT_HANDLE));
static intptr_t s_hStdout = reinterpret_cast<intptr_t>(GetStdHandle(STD_OUTPUT_HANDLE));
static intptr_t s_hStderr = reinterpret_cast<intptr_t>(GetStdHandle(STD_ERROR_HANDLE));

//------------------------------------------------------------------------------
#if defined( __USE_INTERNAL_DEFS_FRAGILE )
// WARNING!  This is lifted from the UCRT sources, and is very sensitive to
// changes in the UCRT data structure layout for __crt_lowio_handle_data.
struct __crt_lowio_handle_data
{
    CRITICAL_SECTION           lock;
    intptr_t                   osfhnd;          // underlying OS file HANDLE
    __int64                    startpos;        // File position that matches buffer start
    unsigned char              osfile;          // Attributes of file (e.g., open in text mode?)
    char                       textmode;
    char                       _pipe_lookahead[3];

    uint8_t unicode          : 1; // Was the file opened as unicode?
    uint8_t utf8translations : 1; // Buffer contains translations other than CRLF
    uint8_t dbcsBufferUsed   : 1; // Is the dbcsBuffer in use?
    char    mbBuffer[MB_LEN_MAX]; // Buffer for the lead byte of DBCS when converting from DBCS to Unicode
                                  // Or for the first up to 3 bytes of a UTF-8 character
};
extern "C" __crt_lowio_handle_data* __pioinfo[1];
#endif

//------------------------------------------------------------------------------
extern "C" int32 __cdecl __acrt_lowio_set_os_handle(int32 const fh, intptr_t const value);
extern "C" int32 __cdecl _free_osfhnd(int32 const fh);

//------------------------------------------------------------------------------
// Reset one lowio handle.  If the expected state is not found, then it halts
// any further attempts to reset handles.
static void reset_handle(intptr_t& h, int32 fh)
{
    static const char* const c_handle_name[] = { "stdin", "stdout", "stderr" };

    assert(fh >= 0);
    assert(fh < 3);

#if defined( __USE_INTERNAL_DEFS_FRAGILE )

    if (!__pioinfo || !__pioinfo[0])
    {
        LOG("ERROR: missing lowio handle array!");
        s_can_reset = false;
        return;
    }

    __crt_lowio_handle_data* pioinfo = __pioinfo[0] + fh;

    if (!pioinfo)
    {
        LOG("ERROR: missing %s handle!", c_handle_name[fh]);
        return;
    }

    // Our cached handle had better match the C runtime's cached handle.  If it
    // doesn't then probably the C runtime data structure layout has changed.
    assert(h == pioinfo->osfhnd);
    if (h != pioinfo->osfhnd)
    {
        s_can_reset = false;
        LOG("ERROR: unexpected lowio handle state for %s!  (expected 0x%p, actual 0x%p)",
            c_handle_name[fh], (void*)h, (void*)pioinfo->osfhnd);
        return;
    }

    // Don't bail out until after testing for state mismatch, so that all three
    // stdio handles get tested and logged.
    if (!s_can_reset)
        return;

    // Update the C runtime's cached handle if the corresponding actual handle
    // is different.
    intptr_t curr = reinterpret_cast<intptr_t>(GetStdHandle(STD_INPUT_HANDLE - fh));
    //printf("lowio %s handle is 0x%p; should be 0x%p\n", c_handle_name[fh], (void*)pioinfo->osfhnd, (void*)curr);
    if (h != curr)
    {
        LOG("resetting mismatched %s handle", c_handle_name[fh]);
        pioinfo->osfhnd = curr;
        h = curr;
    }

#else

    const intptr_t osfhnd = _get_osfhandle(fh);

    // Our cached handle had better match the C runtime's cached handle.  If it
    // doesn't then something has gotten out of sync; better not to attempt any
    // further repairs.
    assert(h == osfhnd);
    if (h != osfhnd)
    {
        s_can_reset = false;
        LOG("ERROR: unexpected lowio handle state for %s!  (expected 0x%p, actual 0x%p)",
            c_handle_name[fh], (void*)h, (void*)osfhnd);
        return;
    }

    // Don't bail out until after testing for state mismatch, so that all three
    // stdio handles get tested and logged.
    if (!s_can_reset)
        return;

    // Update the C runtime's cached handle if the corresponding actual handle
    // is different.
    intptr_t curr = reinterpret_cast<intptr_t>(GetStdHandle(STD_INPUT_HANDLE - fh));
    //printf("lowio %s handle is 0x%p; should be 0x%p\n", c_handle_name[fh], (void*)osfhnd, (void*)curr);
    if (h != curr)
    {
        LOG("resetting mismatched %s handle", c_handle_name[fh]);
        _free_osfhnd(fh);
        __acrt_lowio_set_os_handle(fh, curr);
        h = curr;
    }

#endif
}

//------------------------------------------------------------------------------
void reset_stdio_handles()
{
    // ISSUE 93:
    //
    // Cmder's init.bat script uses a bunch of redirection before launching
    // Clink.  That leaves CMD's current STD handles in an unpredictable state
    // at the moment Clink's dllmain_dispatch is called with DLL_PROCESS_ATTACH
    // and initializes the C runtime.  That causes Lua and Readline output to
    // potentially be written to a stale/bogus file handle (which could even
    // become valid again and used for some other purpose -- thus potentially
    // leading to data corruption and even file corruption).
    //
    // To compensate, Clink forcibly resets the C runtime's std file handles.
    // This should be safe because they only use temporary buffering and are
    // always immediately flushed.

    // ISSUE 134:
    //
    // When `cmd.get_errorlevel` is true, there's a lot of redirection
    // happening.  Apparently the STD handles problem can recur, so the reset
    // code needs to run repeatedly.

    reset_handle(s_hStdin, 0);
    reset_handle(s_hStdout, 1);
    reset_handle(s_hStderr, 2);
}

#else // __MINGW32__ || __MINGW64__

//------------------------------------------------------------------------------
void reset_stdio_handles()
{
    // I don't know whether MinGW may have a similar problem (I would assume
    // so), but the preceding fix is specific to MSVC.
}

#endif // __MINGW32__ || __MINGW64__
