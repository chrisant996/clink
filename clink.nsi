;
; Copyright (c) 2012 Martin Ridgers
;
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
; SOFTWARE.
;

;-------------------------------------------------------------------------------
Name					"clink v${CLINK_VERSION}"
InstallDir				"$PROGRAMFILES\clink"
OutFile					"${CLINK_SOURCE}_setup.exe"
AllowSkipFiles			off
SetCompressor			/SOLID lzma
LicenseBkColor			/windows
LicenseData				LICENSE
LicenseForceSelection	off
RequestExecutionLevel	admin
XPStyle					on

;-------------------------------------------------------------------------------
Page license
Page directory
Page components
;Page custom "checkForLockedFiles"
Page instfiles

UninstPage uninstConfirm
UninstPage components
;UninstPage custom "un.checkForLockedFiles"
UninstPage instfiles

;-------------------------------------------------------------------------------
Function "checkForLockedFiles"
FunctionEnd

;-------------------------------------------------------------------------------
Function "un.checkForLockedFiles"
FunctionEnd

;-------------------------------------------------------------------------------
Section "!Application files"
    SectionIn RO
    SetShellVarContext all

    ; Installs the main files.
    CreateDirectory $INSTDIR
    SetOutPath $INSTDIR
    File ${CLINK_SOURCE}\clink_dll_x*.dll
    File ${CLINK_SOURCE}\clink.lua
    File ${CLINK_SOURCE}\CHANGES
    File ${CLINK_SOURCE}\LICENSE
    File ${CLINK_SOURCE}\clink_x*.exe
    File ${CLINK_SOURCE}\clink.bat
    File ${CLINK_SOURCE}\clink_inputrc
    WriteRegStr HKLM "Software\clink" "Version" "${CLINK_VERSION}"

    ; Create a start-menu shortcut
    CreateDirectory "$SMPROGRAMS\clink"
    CreateShortcut "$SMPROGRAMS\clink\clink.lnk" "$INSTDIR\clink.bat" "" "$SYSDIR\cmd.exe" 0 SW_SHOWMINIMIZED 

    ; Create an uninstaller and a shortcut to it.
    WriteUninstaller "$INSTDIR\clink_uninstall.exe"
    CreateShortcut "$SMPROGRAMS\clink\Uninstall clink.lnk" "$INSTDIR\clink_uninstall.exe"

    ; Add to "add/remove programs"/"programs and features"
    StrCpy $0 "Software\Microsoft\Windows\CurrentVersion\Uninstall"
    WriteRegStr HKLM "$0\clink" "DisplayName" "clink"
    WriteRegStr HKLM "$0\clink" "UninstallString" "$INSTDIR\clink_uninstall.exe"
    WriteRegStr HKLM "$0\clink" "Publisher" "Martin Ridgers"
    WriteRegStr HKLM "$0\clink" "DisplayIcon" "$SYSDIR\cmd.exe,0"
    WriteRegStr HKLM "$0\clink" "URLInfoAbout" "http://code.google.com/p/clink"
    WriteRegStr HKLM "$0\clink" "HelpLink" "http://code.google.com/p/clink"
    WriteRegStr HKLM "$0\clink" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "$0\clink" "DisplayVersion" "${CLINK_VERSION}"

    ; Migrate state from previous versions.
    IfFileExists $APPDATA\clink 0 CreateClinkProfileDir
        Rename $APPDATA\clink $LOCALAPPDATA\clink
        Goto DoneClinkProfileDir
    CreateClinkProfileDir:
        CreateDirectory $LOCALAPPDATA\clink
    DoneClinkProfileDir:

    DeleteRegKey HKLM "$0\Product"
SectionEnd

;-------------------------------------------------------------------------------
Section "Autorun when cmd.exe starts"
    SetShellVarContext all
    ExecWait 'cmd.exe /c "$INSTDIR\clink" autorun --install'
SectionEnd

;-------------------------------------------------------------------------------
Section "!un.Application files"
    SectionIn RO
    SetShellVarContext all

    ExecWait 'cmd.exe /c "$INSTDIR\clink" autorun --uninstall'

    RMDir /r $INSTDIR
    RMDir /r $SMPROGRAMS\clink

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\clink"
    DeleteRegKey HKLM "Software\clink"
SectionEnd

;-------------------------------------------------------------------------------
Section "un.User scripts and history"
    SetShellVarContext all

    RMDIR /r $APPDATA\clink
    RMDIR /r $LOCALAPPDATA\clink
SectionEnd
