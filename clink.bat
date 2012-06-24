::
:: Copyright (c) 2012 Martin Ridgers
::
::
:: Permission is hereby granted, free of charge, to any person obtaining a copy
:: of this software and associated documentation files (the "Software"), to deal
:: in the Software without restriction, including without limitation the rights
:: to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
:: copies of the Software, and to permit persons to whom the Software is
:: furnished to do so, subject to the following conditions:
::
:: The above copyright notice and this permission notice shall be included in
:: all copies or substantial portions of the Software.
::
:: THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
:: IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
:: FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
:: AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
:: LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
:: OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
:: SOFTWARE.
::

@echo off
setlocal

:: If the .bat is run without any arguments, then start a cmd.exe instance.
::
if "%1"=="" (
    start "" cmd.exe /k "%~s0 inject && title clink"
    goto :eof
)

:: Injects clink into the parent cmd.exe process
::
if /i "%1"=="inject" (
    pushd %~dps0
    if "%PROCESSOR_ARCHITECTURE%"=="x86" (
        %~n0_x86.exe
    ) else (
        %~n0_x64.exe
    )
    popd
    goto :eof
)

:: Add registry keys to inject clink everytime cmd.exe is started.
::
if /i "%1"=="install" (
    :: attempt to break-out to native OS version of cmd.exe if need be
    if not "%PROCESSOR_ARCHITEW6432%"=="" (
        %windir%\sysnative\cmd.exe /c "%~s0 install"
    ) else (
        call :install "HKLM\Software\Microsoft\Command Processor"
        if not "%PROCESSOR_ARCHITECTURE%"=="x86" (
            call :install "HKLM\Software\Wow6432Node\Microsoft\Command Processor"
        )
    )
    goto :eof
)

:: Delete the registry keys added by 'install'
::
if /i "%1"=="uninstall" (
    if not "%PROCESSOR_ARCHITEW6432%"=="" (
        %windir%\sysnative\cmd.exe /c "%~s0 uninstall"
    ) else (
        call :uninstall "HKLM\Software\Microsoft\Command Processor"
        if not "%PROCESSOR_ARCHITECTURE%"=="x86" (
            call :uninstall "HKLM\Software\Wow6432Node\Microsoft\Command Processor"
        )
    )
    goto :eof
)

goto :eof
::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

:: installs clink's autorun
::
:install
    :: Check for an existing install
    call :get_reg_key %1 AutoRunPreClinkInstall
    if "%found%"=="1" (
        goto :eof
    )

    set autorun=%~s0 inject
    call :get_reg_key %1 AutoRun
    if not "%ret%"=="" (
        set autorun=%~s0 inject ^&^& %ret:&=^&%
    )
    call :set_reg_key %1 AutoRunPreClinkInstall "%ret%"
    call :set_reg_key %1 AutoRun "%autorun%"
exit /b 0

:: uninstalls clink's autorun.
::
:uninstall
    call :get_reg_key %1 AutoRunPreClinkInstall
    if "%ret%"=="" (
        call :del_reg_key %1 AutoRun
    ) else (
        call :set_reg_key %1 AutoRun "%ret%"
    )
    call :del_reg_key %1 AutoRunPreClinkInstall
exit /b 0

:: returns a registry key in the 'ret' environment variable, setting found to
:: 1 if a key was... found.
::
:get_reg_key
    set ret=""
    set found=0
    reg query %1 /v %2 1>nul 2>nul
    if %errorlevel%==0 (
        for /f "tokens=2* delims= " %%d in ('reg query %1 /v %2') do (
            set found=1
            set ret="%%e"
        )
    )

    set ret=%ret:&=^&%
    set ret=%ret:~1,-1%
exit /b 0

:: Sets the registry key (REG_SZ type only!)
::
:set_reg_key
    reg add %1 /v %2 /t REG_SZ /d "%~3" /f
exit /b 0

:: Deletes a registry key
::
:del_reg_key
    reg delete %1 /v %2 /f
exit /b 0
