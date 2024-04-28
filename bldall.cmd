@echo off
setlocal

set EC=echo
set CP=cp.exe /uy
set XCOPY=xcopy.exe /cdfiky
set LINE=(0
set ASC=(B
set BOLD=[1m
set UNDER=[4m
set NEG=[7m%ASC%
set POS=[27m%LINE%
set CYAN=[36m
set NORM=%ASC%[0m

set __debug_config=Debug
set __release_config=Release
set __final_config=Final
set __x86_platform=Win32
set __x64_platform=x64

set __X86=
set __AMD64=
set __DEBUG=
set __RELEASE=
set __FINAL=
set __MULTICPU=-m:4
set __STOPONERROR=
set __INSTALL=
set __INSTALLDIR=c:\wbin\clink
set __SLN=
set __MSBUILD=

rem -- Try to find MSBuild.

for /f %%a in ('where msbuild.exe 2^>nul') do (
	set __MSBUILD="%%a"
	goto :gotmsbuild
)

set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
if exist %__MSBUILD% goto gotmsbuild
set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
if exist %__MSBUILD% goto gotmsbuild
set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Enterprise\MSBuild\15.0\Bin\MSBuild.exe"
if exist %__MSBUILD% goto gotmsbuild
set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\17.0\Bin\MSBuild.exe"
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
) else if exist .build\vs2022\*.sln (
	for %%a in (.build\vs2022\*.sln) do (
		set __SLN="%%a"
		set __MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
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

rem -- Get next arg.

:nextarg
shift

:doarg
if "%1" == "/x86" ( set __X86=1&goto :nextarg )
if "%1" == "/win32" ( set __X86=1&goto :nextarg )
if "%1" == "/x64" ( set __AMD64=1&goto :nextarg )
if "%1" == "/amd64" ( set __AMD64=1&goto :nextarg )
if "%1" == "/dbg" ( set __DEBUG=1&goto :nextarg )
if "%1" == "/debug" ( set __DEBUG=1&goto :nextarg )
if "%1" == "/rel" ( set __RELEASE=1&goto :nextarg )
if "%1" == "/release" ( set __RELEASE=1&goto :nextarg )
if "%1" == "/shp" ( set __RELEASE=1&goto :nextarg )
if "%1" == "/ship" ( set __RELEASE=1&goto :nextarg )
if "%1" == "/fin" ( set __FINAL=1&goto :nextarg )
if "%1" == "/final" ( set __FINAL=1&goto :nextarg )
rem if "%1" == "-q" ( set __MULTICPU=-m:1&set __STOPONERROR=yes&goto :nextarg )
if "%1" == "install" ( set __INSTALL=final&set __FINAL=1&goto :nextarg )
if "%1" == "install:debug" ( set __INSTALL=debug&set __DEBUG=1&goto :nextarg )
if "%1" == "install:release" ( set __INSTALL=release&set __RELEASE=1&goto :nextarg )
if "%1" == "install:final" ( set __INSTALL=final&set __FINAL=1&goto :nextarg )

if not "%1" == "" (
	%EC% %BOLD%Usage:%NORM%  BLDALL [/x86 /amd64 /dbg /shp] [install]
	echo.
	echo   By default, builds DEBUG for both x86 and x64.
	echo.
	echo Options:
	echo   /x86             Build x86.
	echo   /x64             Build x64.
	echo   /dbg             Build DEBUG.
	echo   /rel             Build RELEASE.
	echo   /fin             Build FINAL.
rem	echo   -q               Quit immediately on any error.
	echo   install          Copy FINAL build results to the install directory.
	echo   install:debug    Copy DEBUG build results to the install directory.
	echo   install:release  Copy RELEASE build results to the install directory.
	echo   install:final    Copy FINAL build results to the install directory.
	echo.
	echo Aliases:
	echo   /x86             Or /win32.
	echo   /x64             Or /amd64.
	echo   /dbg             Or /debug.
	echo   /rel             Or /release or /shp or /ship.
	echo   /fin             Or /final.
	echo.
	if x%__SLN% == x echo warning: Unable to find VS2022, VS2019, or VS2017 clink.sln file.
	if not x%__SLN% == x echo Using SLN file:  %__SLN%
	if not exist %__MSBUILD% echo warning: Unable to find VS2022, VS2019, or VS2017 Enterprise installation.
	if exist %__MSBUILD% echo Using MSBuild:   %__MSBUILD%
	goto :eof
)

if "%__X86%" == "" ( if "%__AMD64%" == "" (set __X86=1&set __AMD64=1) )
if "%__DEBUG%" == "" ( if "%__RELEASE%" == "" ( if "%__FINAL%" == "" (set __DEBUG=1) ) )

if x%__SLN% == x echo error: Unable to find .sln file.&goto :eof
if not exist %__MSBUILD% echo error: Unable to find MSBuild.&goto :eof

set __SLNNAME=%__SLN%
call :setslnname %__SLN%

if not "%__X86%" == "" ( if not "%__DEBUG%" == "" (
	echo.
	%EC% %BOLD%%NEG% Building %__SLNNAME% x86 DEBUG %POS%%NORM%
	%__MSBUILD% -nologo -v:minimal %__MULTICPU% -p:Configuration=Debug;Platform=Win32 %__SLN%
	if "%__AMD64%" == "" (
		if errorlevel 1 goto :eof
	)
) )

if not "%__AMD64%" == "" ( if not "%__DEBUG%" == "" (
	echo.
	%EC% %BOLD%%NEG% Building %__SLNNAME% amd64 DEBUG %POS%%NORM%
	%__MSBUILD% -nologo -v:minimal %__MULTICPU% -p:Configuration=Debug;Platform=x64 %__SLN%
	if errorlevel 1 goto :eof
) )

if not "%__X86%" == "" ( if not "%__RELEASE%" == "" (
	echo.
	%EC% %BOLD%%NEG% Building %__SLNNAME% x86 RELEASE %POS%%NORM%
	%__MSBUILD% -nologo -v:minimal %__MULTICPU% -p:Configuration=Release;Platform=Win32 %__SLN%
	if "%__AMD64%" == "" (
		if errorlevel 1 goto :eof
	)
) )

if not "%__AMD64%" == "" ( if not "%__RELEASE%" == "" (
	echo.
	%EC% %BOLD%%NEG% Building %__SLNNAME% amd64 RELEASE %POS%%NORM%
	%__MSBUILD% -nologo -v:minimal %__MULTICPU% -p:Configuration=Release;Platform=x64 %__SLN%
	if errorlevel 1 goto :eof
) )

if not "%__X86%" == "" ( if not "%__FINAL%" == "" (
	echo.
	%EC% %BOLD%%NEG% Building %__SLNNAME% x86 FINAL %POS%%NORM%
	%__MSBUILD% -nologo -v:minimal %__MULTICPU% -p:Configuration=Final;Platform=Win32 %__SLN%
	if "%__AMD64%" == "" (
		if errorlevel 1 goto :eof
	)
) )

if not "%__AMD64%" == "" ( if not "%__FINAL%" == "" (
	echo.
	%EC% %BOLD%%NEG% Building %__SLNNAME% amd64 FINAL %POS%%NORM%
	%__MSBUILD% -nologo -v:minimal %__MULTICPU% -p:Configuration=Final;Platform=x64 %__SLN%
	if errorlevel 1 goto :eof
) )

set __INSTALLTYPE=
if "%__INSTALL%" == "debug" ( if not "%__DEBUG%" == "" ( if not "%__X86%" == "" ( if not "%__AMD64%" == "" (
	set __INSTALLTYPE=DEBUG
) ) ) )
if "%__INSTALL%" == "release" ( if not "%__RELEASE%" == "" ( if not "%__X86%" == "" ( if not "%__AMD64%" == "" (
	set __INSTALLTYPE=RELEASE
) ) ) )
if "%__INSTALL%" == "final" ( if not "%__FINAL%" == "" ( if not "%__X86%" == "" ( if not "%__AMD64%" == "" (
	set __INSTALLTYPE=FINAL
) ) ) )

if not "%__INSTALLTYPE%" == "" (
	echo.
	%EC% %BOLD%%NEG% INSTALLING %__INSTALLTYPE% OUTPUTS %POS%%NORM%
	if "%USERNAME%" == "%chrisant%" (
		%CP% .build\vs2022\bin\%__INSTALLTYPE%\clink.bat;clink.lua;clink_x*.exe;clink_x*.pdb;clink_dll_x*.dll;clink_dll_x*.pdb "%__INSTALLDIR%"
		%CP% .build\docs\clink.html "%__INSTALLDIR%"
	) else (
		%XCOPY% .build\vs2022\bin\%__INSTALLTYPE%\clink.bat "%__INSTALLDIR%"
		%XCOPY% .build\vs2022\bin\%__INSTALLTYPE%\clink.lua "%__INSTALLDIR%"
		%XCOPY% .build\vs2022\bin\%__INSTALLTYPE%\clink_x*.exe "%__INSTALLDIR%"
		%XCOPY% .build\vs2022\bin\%__INSTALLTYPE%\clink_x*.pdb "%__INSTALLDIR%"
		%XCOPY% .build\vs2022\bin\%__INSTALLTYPE%\clink_dll_x*.dll "%__INSTALLDIR%"
		%XCOPY% .build\vs2022\bin\%__INSTALLTYPE%\clink_dll_x*.pdb "%__INSTALLDIR%"
		%XCOPY% .build\docs\clink.html "%__INSTALLDIR%"
	)
)

goto :eof

:setslnname
if not "%~n1" == "" set __SLNNAME=%~n1
goto :eof

