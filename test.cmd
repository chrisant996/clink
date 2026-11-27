@echo off
setlocal
set __DBG=
set __ME=%~dp0
set __CPU=x64
set __FLAVOR=debug
set __FLAGS=
set __TEST=

:arg
if "%~1" == "?" goto :usage
if "%~1" == "/?" goto :usage
if "%~1" == "-?" goto :usage
if "%~1" == "/h" goto :usage
if "%~1" == "-h" goto :usage
if "%~1" == "/help" goto :usage
if "%~1" == "--help" goto :usage
if "%~1" == "help" goto :usage
if "%~1" == "/x64" set __CPU=x64& goto :nextarg
if "%~1" == "--x64" set __CPU=x64& goto :nextarg
if "%~1" == "/x86" set __CPU=x86& goto :nextarg
if "%~1" == "--x86" set __CPU=x86& goto :nextarg
if "%~1" == "/dbg" set __DBG=call devenv /debugexe& goto:nextarg
if "%~1" == "--dbg" set __DBG=call devenv /debugexe& goto:nextarg
if "%~1" == "/rel" set __FLAVOR=release& goto:nextarg
if "%~1" == "--rel" set __FLAVOR=release& goto:nextarg
if "%~1" == "/release" set __FLAVOR=release& goto:nextarg
if "%~1" == "--release" set __FLAVOR=release& goto:nextarg
if "%~1" == "/ship" set __FLAVOR=release& goto:nextarg
if "%~1" == "--ship" set __FLAVOR=release& goto:nextarg
if "%~1" == "--list" set __FLAGS= --list& goto:nextarg

if "%~2" == "/rel" goto:oopsflag
if "%~2" == "-rel" goto:oopsflag
if "%~2" == "/release" goto:oopsflag
if "%~2" == "--release" goto:oopsflag
if "%~2" == "/ship" goto:oopsflag
if "%~2" == "--ship" goto:oopsflag

if exist "%__ME%.build\vs2019\bin\%__FLAVOR%\clink_test_%__CPU%.exe" set __TEST=%__ME%.build\vs2019\bin\%__FLAVOR%\clink_test_%__CPU%.exe
if exist "%__ME%.build\vs2022\bin\%__FLAVOR%\clink_test_%__CPU%.exe" set __TEST=%__ME%.build\vs2022\bin\%__FLAVOR%\clink_test_%__CPU%.exe
if exist "%__ME%.build\vs2026\bin\%__FLAVOR%\clink_test_%__CPU%.exe" set __TEST=%__ME%.build\vs2026\bin\%__FLAVOR%\clink_test_%__CPU%.exe
if "%__TEST%" == "" goto no_exe

if "%__FLAGS%" == "" echo %__DBG% %__TEST%%__FLAGS% %1 %2 %3
%__DBG% %__TEST%%__FLAGS% %1 %2 %3
goto :eof

:nextarg
shift
goto :arg

:no_exe
echo Unable to find clink_test_%__CPU%.exe.
goto :eof

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
echo   --list    List tests.
echo.
echo Script options must precede test options.
echo.
echo If [test name prefix] is included, then it only runs tests whose name begins
echo with the specified prefix.
goto :eof
