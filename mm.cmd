@echo off
setlocal

set EC=echo
set LINE=(0
set ASC=(B
set BOLD=[1m
set UNDER=[4m
set NEG=[7m
set POS=[27m
set CYAN=[36m
set NORM=%ASC%[0m

set __debug_config=debug
set __release_config=release
set __final_config=final
set __x86_platform=_x86
set __x64_platform=_x64

set __MULTICPU=-j6
set __CONFIG=%__debug_config%
set __PLATFORM=%__x64_platform%
set __MAKENAME=mingw32-make
set __MAKE=
set __DIR=

set __DEFAULTCONFIG=%__CONFIG%
set __DEFAULTPLATFORM=%__PLATFORM%

rem -- Try to find build dir.
if exist .build\gmake2 (
	set __DIR=.build\gmake2
) else if exist .build\gmake (
	set __DIR=.build\gmake
) else (
	echo Unable to find build dir ^(try 'premake5 gmake' or 'premake5 gmake2'^).
	goto :eof
)

rem -- Try to find make program.
call :findmake %__MAKENAME%.exe

goto :doarg

rem -- Get next arg.

:nextarg
shift

:doarg
if "%1" == "?" goto :usage
if "%1" == "??" goto :makeusage
if "%1" == "/?" goto :usage
if "%1" == "/??" goto :makeusage
if "%1" == "-?" goto :usage
if "%1" == "-??" goto :makeusage
if "%1" == "/h" goto :usage
if "%1" == "-h" goto :usage
if "%1" == "/help" goto :usage
if "%1" == "-help" goto :usage
if "%1" == "--help" goto :usage
if "%1" == "/x86" ( set __PLATFORM=%__x86_platform%&goto :nextarg )
if "%1" == "/win32" ( set __PLATFORM=%__x86_platform%&goto :nextarg )
if "%1" == "/x64" ( set __PLATFORM=%__x64_platform%&goto :nextarg )
if "%1" == "/amd64" ( set __PLATFORM=%__x64_platform%&goto :nextarg )
if "%1" == "/dbg" ( set __CONFIG=%__debug_config%&goto :nextarg )
if "%1" == "/debug" ( set __CONFIG=%__debug_config%&goto :nextarg )
if "%1" == "/fin" ( set __CONFIG=%__final_config%&goto :nextarg )
if "%1" == "/final" ( set __CONFIG=%__final_config%&goto :nextarg )
if "%1" == "/rel" ( set __CONFIG=%__release_config%&goto :nextarg )
if "%1" == "/release" ( set __CONFIG=%__release_config%&goto :nextarg )
if "%1" == "/shp" ( set __CONFIG=%__release_config%&goto :nextarg )
if "%1" == "/ship" ( set __CONFIG=%__release_config%&goto :nextarg )
if "%1" == "/single" ( set __MULTICPU=&goto :nextarg )
if "%1" == "--single" ( set __MULTICPU=&goto :nextarg )
if "%1" == "/multi" ( set __MULTICPU=-j6&goto :nextarg )
if "%1" == "--multi" ( set __MULTICPU=-j6&goto :nextarg )
:notminus

set __ARGS=
:appendarg
if not "%1" == "" set __ARGS=%__ARGS% %1&shift&goto appendarg

if x%__MAKE% == x echo error: Unable to find %__MAKENAME%.&goto :eof

setlocal
cd %__DIR%

echo.
%EC% %BOLD%%NEG% Building %__CONFIG%%__PLATFORM% %POS%%NORM%
%__MAKE% %__MULTICPU% config=%__CONFIG%%__PLATFORM% %__ARGS%
if errorlevel 1 goto :eof

goto :eof

rem -- Print usage info.

:usage

set __ISX86DEFAULT=
set __ISX64DEFAULT=
set __ISDEBUGDEFAULT=
set __ISRELEASEDEFAULT=
set __ISFINALDEFAULT=
set __DEFAULTMARKER= %CYAN%(default)%NORM%
if %__DEFAULTPLATFORM% == %__x86_platform% set __ISX86DEFAULT=%__DEFAULTMARKER%
if %__DEFAULTPLATFORM% == %__x64_platform% set __ISX64DEFAULT=%__DEFAULTMARKER%
if %__DEFAULTCONFIG% == %__debug_config% set __ISDEBUGDEFAULT=%__DEFAULTMARKER%
if %__DEFAULTCONFIG% == %__release_config% set __ISRELEASEDEFAULT=%__DEFAULTMARKER%
if %__DEFAULTCONFIG% == %__final_config% set __ISFINALDEFAULT=%__DEFAULTMARKER%

%EC% %BOLD%Usage:%NORM%  MM [/dbg /rel /fin /x86 /x64 /multi /single] [make_options]
echo.
echo Builds %__DEFAULTCONFIG% %__DEFAULTPLATFORM% by default.
echo.
echo   /x86          Builds x86%__ISX86DEFAULT%.
echo   /x64          Builds x64%__ISX64DEFAULT%.
echo   /dbg          Builds DEBUG%__ISDEBUGDEFAULT%.
echo   /rel          Builds RELEASE%__ISRELEASEDEFAULT%.
echo   /fin          Builds FINAL%__ISFINALDEFAULT%.
echo.
echo   /multi        Multi processor build %CYAN%(default)%NORM%.
echo   /single       Single processor build.
echo.
echo   /t target     Build target.
echo.
echo.  /??           Show %__MAKENAME% help.
echo.
echo Aliases:
echo   /x86          Or /win32.
echo   /x64          Or /amd64.
echo   /dbg          Or /debug.
echo   /rel          Or /release or /shp or /ship.
echo   /fin          Or /final.
echo.
if x%__DIR% == x echo warning: Unable to find build dir (try 'premake5 gmake').
if not x%__DIR% == x echo Using build dir:     %__DIR%
if not exist %__MAKE% echo warning: Unable to find %__MAKENAME%.
if exist %__MAKE% echo Using %__MAKENAME%:  %__MAKE%

goto :eof

:makeusage
%__MAKE% --help
goto :eof

:findmake
set __MAKE="%~$PATH:1"
goto :eof

