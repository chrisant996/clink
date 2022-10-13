:: Copyright (c) 2012 Martin Ridgers
:: License: http://opensource.org/licenses/MIT

@echo off
set clink_profile_arg=
set clink_quiet_arg=

set clink_autorun=0
set clink_parameters="%*"
set clink_parameters_nospace="%clink_parameters: =_%"
if not %clink_parameters_nospace:--autorun=%==x%clink_parameters_nospace% set clink_autorun=1

:: quick exit if CLINK_NOAUTORUN is defined
if defined CLINK_NOAUTORUN (
    if %clink_autorun% EQU 1 (
        echo Clink autorun is disabled by CLINK_NOAUTORUN ^(clink.bat^)
        goto :end
    )
)

:: Mimic cmd.exe's behaviour when starting from the start menu.
if /i "%1"=="startmenu" (
    cd /d "%userprofile%"
    shift /1
)

:: Check for the --profile option.
if /i "%1"=="--profile" (
    set clink_profile_arg=--profile "%~2"
    shift /1
    shift /1
)

:: Check for the --quiet option.
if /i "%1"=="--quiet" (
    set clink_quiet_arg= --quiet
    shift /1
)

:: If the .bat is run without any arguments, then start a cmd.exe instance.
if "%1"=="" (
    call :launch
    goto :end
)

:: Pass through to appropriate loader.
if /i "%processor_architecture%"=="x86" (
        "%~dp0\clink_x86.exe" %*
) else if /i "%processor_architecture%"=="amd64" (
    if defined processor_architew6432 (
        "%~dp0\clink_x86.exe" %*
    ) else (
        "%~dp0\clink_x64.exe" %*
    )
)

:end
set clink_profile_arg=
set clink_quiet_arg=
set clink_parameters=
set clink_autorun=0
goto :eof

::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
:launch
setlocal
set WT_PROFILE_ID=
set WT_SESSION=
start "Clink" cmd.exe /s /k ""%~dpnx0" inject %clink_profile_arg%%clink_quiet_arg%"
endlocal
exit /b 0
