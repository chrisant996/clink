@echo off
setlocal
set __DBG=
set __ME=%~dp0
set __CPU=x64
set __FLAVOR=debug
set __FLAGS=

:arg
if x%1x == x?x goto :usage
if x%1x == x/?x goto :usage
if x%1x == x-?x goto :usage
if x%1x == x/hx goto :usage
if x%1x == x-hx goto :usage
if x%1x == x/helpx goto :usage
if x%1x == x--helpx goto :usage
if x%1x == xhelpx goto :usage
if x%1x == x/x64x set __CPU=x64& goto :nextarg
if x%1x == x--x64x set __CPU=x64& goto :nextarg
if x%1x == x/x86x set __CPU=x86& goto :nextarg
if x%1x == x--x86x set __CPU=x86& goto :nextarg
if x%1x == x/dbgx set __DBG=call devenv /debugexe& goto:nextarg
if x%1x == x--dbgx set __DBG=call devenv /debugexe& goto:nextarg
if x%1x == x/relx set __FLAVOR=release& goto:nextarg
if x%1x == x--relx set __FLAVOR=release& goto:nextarg
if x%1x == x/releasex set __FLAVOR=release& goto:nextarg
if x%1x == x--releasex set __FLAVOR=release& goto:nextarg
if x%1x == x/shipx set __FLAVOR=release& goto:nextarg
if x%1x == x--shipx set __FLAVOR=release& goto:nextarg
if x%1x == x--list-testsx set __FLAGS= --list-tests& goto:nextarg

if x%2x == x/relx goto:oopsflag
if x%2x == x-relx goto:oopsflag
if x%2x == x/releasex goto:oopsflag
if x%2x == x--releasex goto:oopsflag
if x%2x == x/shipx goto:oopsflag
if x%2x == x--shipx goto:oopsflag

if "%__FLAGS%" == "" echo %__DBG% %__ME%.build\vs2022\bin\%__FLAVOR%\clink_test_%__CPU%.exe%__FLAGS% %1 %2 %3
%__DBG% %__ME%.build\vs2022\bin\%__FLAVOR%\clink_test_%__CPU%.exe%__FLAGS% %1 %2 %3
goto :eof

:nextarg
shift
goto :arg

:oopsflag
echo Options in wrong order; %2 belongs before %1.
goto :eof

:usage
echo Usage:  test [options1] [options2] [test name prefix]
echo.
echo   Run clink_test_x64.exe.
echo.
echo Script options:
echo.  /?        Show usage info.
echo   /dbg      Run test under the debugger.
echo   /x64      Run clink_test_x64.exe (the default).
echo   /x86      Run clink_test_x86.exe.
echo   /rel      Run release version (runs debug version by default).
echo.
echo Test options:
echo   -d        Load Lua debugger.
echo   -t        Show execution time.
echo.
echo Script options must precede test options.
echo.
echo If [test name prefix] is included, then it only runs tests whose name begins
echo with the specified prefix.
goto :eof
