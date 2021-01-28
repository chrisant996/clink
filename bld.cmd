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

set __debug_config=Debug
set __release_config=Release
set __final_config=Final
set __x86_platform=Win32
set __x64_platform=x64

set __AMD64=
set __MULTICPU=-m:4
set __STOPONERROR=
set __INSTALL=
set __CONFIG=%__debug_config%
set __PLATFORM=%__x64_platform%
set __TARGETS=
set __SLN=
set __MSBUILD=MSBuild.exe
if exist .build\vs2019\clink.sln (
	set __SLN=".build\vs2019\clink.sln"
	set __MSBUILD="c:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
) else if exist .build\vs2017\clink.sln (
	set __SLN=".build\vs2017\clink.sln"
	set __MSBUILD="c:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\MSBuild\15.0\Bin\MSBuild.exe"
)

goto :doarg

:nextarg
shift

:doarg
if "%1" == "?" goto :usage
if "%1" == "??" goto :msbuildusage
if "%1" == "/?" goto :usage
if "%1" == "/??" goto :msbuildusage
if "%1" == "-?" goto :usage
if "%1" == "-??" goto :msbuildusage
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
if "%1" == "/single" ( set __MULTICPU=&goto :nextarg )
if "%1" == "--single" ( set __MULTICPU=&goto :nextarg )
if "%1" == "/multi" ( set __MULTICPU=-m:6&goto :nextarg )
if "%1" == "--multi" ( set __MULTICPU=-m:6&goto :nextarg )
if "%1" == "/t" ( set __TARGETS=%__TARGETS% -t:%2&shift&goto :nextarg )
if "%1" == "-t" ( set __TARGETS=%__TARGETS% -t:%2&shift&goto :nextarg )
if "%1" == "/target" ( set __TARGETS=%__TARGETS% -t:%2&shift&goto :nextarg )
if "%1" == "--target" ( set __TARGETS=%__TARGETS% -t:%2&shift&goto :nextarg )
:notminus

set __ARGS=
:appendarg
if not "%1" == "" set __ARGS=%__ARGS% %1&shift&goto appendarg

if x%__SLN% == x echo error: Unable to find VS2019 (or VS2017) clink.sln file.&goto :eof
if not exist %__MSBUILD% echo error: Unable to find VS2019 (or VS2017) Enterprise installation.&goto :eof

echo.
%EC% %BOLD%%NEG% BUILDING %__PLATFORM% %__CONFIG% %POS%%NORM%
%__MSBUILD% -nologo -v:minimal %__MULTICPU% -p:Configuration=%__CONFIG% -p:Platform=%__PLATFORM% %__TARGETS% %__ARGS% %__SLN%
if errorlevel 1 goto :eof

goto :eof

:usage
%EC% %BOLD%Usage:%NORM%  BLD [/dbg /rel /fin /x86 /x64 /multi /single] [msbuild_options]
echo.
echo Builds DEBUG x64 by default.
echo.
echo   /x86          Builds x86.
echo   /x64          Builds x64 %CYAN%(default)%NORM%.
echo   /dbg          Builds DEBUG %CYAN%(default)%NORM%.
echo   /rel          Builds RELEASE.
echo   /fin          Builds FINAL.
echo.
echo   /multi        Multi processor build %CYAN%(default)%NORM%.
echo   /single       Single processor build.
echo.
echo   /t target     Build target.
echo.
echo.  /??           Show MSBuild help.
echo.
echo Aliases:
echo   /x86          Or /win32.
echo   /x64          Or /amd64.
echo   /dbg          Or /debug.
echo   /rel          Or /release.
echo   /fin          Or /final.
echo.
if x%__SLN% == x echo warning: Unable to find VS2019 (or VS2017) clink.sln file.
if not x%__SLN% == x echo Using SLN file:  %__SLN%
if not exist %__MSBUILD% echo warning: Unable to find VS2019 (or VS2017) Enterprise installation.
if exist %__MSBUILD% echo Using MSBuild:   %__MSBUILD%
goto :eof

:msbuildusage
%__MSBUILD% -help
goto :eof

