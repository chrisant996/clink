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
set __MSBUILD=

set __DEFAULTCONFIG=%__CONFIG%
set __DEFAULTPLATFORM=%__PLATFORM%

rem -- Try to find MSBuild.

for /f %%a in ('where msbuild.exe 2^>nul') do (
	set __MSBUILD="%%a"
	goto :gotmsbuild
)

set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
if exist %__MSBUILD% goto gotmsbuild
set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Enterprise\MSBuild\15.0\Bin\MSBuild.exe"
if exist %__MSBUILD% goto gotmsbuild
set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\MSBuild\15.0\Bin\MSBuild.exe"
if exist %__MSBUILD% goto gotmsbuild
set __MSBUILD="%ProgramFiles(x86)%\MSBuild\Current\Bin\MSBuild.exe"
if exist %__MSBUILD% goto gotmsbuild
set __MSBUILD="%ProgramFiles(x86)%\MSBuild\15.0\Bin\MSBuild.exe"
if exist %__MSBUILD% goto gotmsbuild
set __MSBUILD="%ProgramFiles(x86)%\MSBuild\12.0\Bin\MSBuild.exe"
if exist %__MSBUILD% goto gotmsbuild
set __MSBUILD=

:gotmsbuild

rem -- Try to find solution file.

if exist *.sln (
	for %%a in (*.sln) do (
		set __SLN="%%a"
		goto :gotsln
	)
) else if exist .build\vs2019\*.sln (
	for %%a in (.build\vs2019\*.sln) do (
		set __SLN="%%a"
		set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
		goto :gotsln
	)
) else if exist .build\vs2017\*.sln (
	for %%a in (.build\vs2017\*.sln) do (
		set __SLN="%%a"
		set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Enterprise\MSBuild\15.0\Bin\MSBuild.exe"
		goto :gotsln
	)
)

:gotsln
goto :doarg

:findsln
if exist %__SLN% (
	set __SLN="%__SLN%"
	goto :nextarg
) else if exist %__SLN%.sln (
	set __SLN="%__SLN%.sln"
	goto :nextarg
) else if exist .build\vs2019\%__SLN% (
	set __SLN=".build\vs2019\%__SLN%"
	set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
	goto :nextarg
) else if exist .build\vs2019\%__SLN%.sln (
	set __SLN=".build\vs2019\%__SLN%.sln"
	set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
	goto :nextarg
) else if exist .build\vs2017\%__SLN% (
	set __SLN=".build\vs2017\%__SLN%"
	set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\15.0\Bin\MSBuild.exe"
	goto :nextarg
) else if exist .build\vs2017\%__SLN%.sln (
	set __SLN=".build\vs2017\%__SLN%.sln"
	set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\15.0\Bin\MSBuild.exe"
	goto :nextarg
)
echo Unable to find solution '%__SLN%'.
exit /b 1

rem -- Get next arg.

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
if "%1" == "/shp" ( set __CONFIG=%__release_config%&goto :nextarg )
if "%1" == "/ship" ( set __CONFIG=%__release_config%&goto :nextarg )
if "%1" == "/single" ( set __MULTICPU=&goto :nextarg )
if "%1" == "--single" ( set __MULTICPU=&goto :nextarg )
if "%1" == "/multi" ( set __MULTICPU=-m:6&goto :nextarg )
if "%1" == "--multi" ( set __MULTICPU=-m:6&goto :nextarg )
if "%1" == "/t" ( set __TARGETS=%__TARGETS% -t:%2&shift&goto :nextarg )
if "%1" == "-t" ( set __TARGETS=%__TARGETS% -t:%2&shift&goto :nextarg )
if "%1" == "/target" ( set __TARGETS=%__TARGETS% -t:%2&shift&goto :nextarg )
if "%1" == "--target" ( set __TARGETS=%__TARGETS% -t:%2&shift&goto :nextarg )
if "%1" == "/sln" ( set __SLN=%2&shift&goto :findsln )
if "%1" == "--sln" ( set __SLN=%2&shift&goto :findsln )
:notminus

set __ARGS=
:appendarg
if not "%1" == "" set __ARGS=%__ARGS% %1&shift&goto appendarg

rem -- Try to run MSBuild with the solution file.

if x%__SLN% == x echo error: Unable to find .sln file.&goto :eof
if x%__MSBUILD% == x echo error: Unable to find MSBuild.&goto :eof

set __SLNNAME=%__SLN%
call :setslnname %__SLN%

rem echo.
%EC% %BOLD%%NEG% Building %__SLNNAME% %__PLATFORM% %__CONFIG% %POS%%NORM%
%__MSBUILD% -nologo -v:minimal %__MULTICPU% -p:Configuration=%__CONFIG% -p:Platform=%__PLATFORM% %__TARGETS% %__ARGS% %__SLN%
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

%EC% %BOLD%Usage:%NORM%  BLD [/dbg /rel /fin /x86 /x64 /multi /single] [msbuild_options]
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
echo.  /??           Show MSBuild help.
echo.
echo Aliases:
echo   /x86          Or /win32.
echo   /x64          Or /amd64.
echo   /dbg          Or /debug.
echo   /rel          Or /release or /shp or /ship.
echo   /fin          Or /final.
echo.
if x%__SLN% == x echo warning: Unable to find .sln file.
if not x%__SLN% == x echo Using SLN file:  %__SLN%
if not exist %__MSBUILD% echo warning: Unable to find MSBuild.
if exist %__MSBUILD% echo Using MSBuild:   %__MSBUILD%
goto :eof

:msbuildusage
%__MSBUILD% -help
goto :eof

:setslnname
if not "%~n1" == "" set __SLNNAME=%~n1
goto :eof

