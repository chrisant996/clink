@echo off
if x%1x == x?x goto :usage
if x%1x == x/?x goto :usage
if x%1x == x-?x goto :usage
if x%1x == x/hx goto :usage
if x%1x == x-hx goto :usage
if x%1x == x/helpx goto :usage
if x%1x == x--helpx goto :usage
if x%1x == xhelpx goto :usage

if x%1x == x/dbgx call devenv /debugexe %~dp0.build\vs2019\bin\debug\clink_test_x64.exe
if not x%1x == x/dbgx %~dp0.build\vs2019\bin\debug\clink_test_x64.exe
goto :eof

:usage
echo Usage:  test [/? /dbg]
echo.
echo   Run clink_test_x64.exe.
echo.
echo.  /?        Show usage info.
echo   /dbg      Run test under the debugger.
goto :eof
