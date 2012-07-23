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
Name clink
InstallDir "$PROGRAMFILES\clink"
OutFile "${CLINK_SOURCE}_setup.exe"
SetCompressor lzma
LicenseData clink_license.txt
LicenseForceSelection off
RequestExecutionLevel admin
XPStyle on

;-------------------------------------------------------------------------------
!if 0
    !include "MUI2.nsh"

    !define MUI_ABORTWARNING

    ;!insertmacro MUI_PAGE_LICENSE "${NSISDIR}\Docs\Modern UI\License.txt"
    !insertmacro MUI_PAGE_COMPONENTS
    !insertmacro MUI_PAGE_DIRECTORY
    !insertmacro MUI_PAGE_INSTFILES
      
    !insertmacro MUI_UNPAGE_CONFIRM
    !insertmacro MUI_UNPAGE_INSTFILES
      
    !insertmacro MUI_LANGUAGE "English"
!else
    Page license
    Page directory
    Page components
    Page instfiles

    UninstPage uninstConfirm
    UninstPage components
    UninstPage instfiles
!endif

;-------------------------------------------------------------------------------
Section "!Application files"
    SectionIn RO
    SetShellVarContext all

    ; Installs the main files.
    CreateDirectory $INSTDIR
    SetOutPath $INSTDIR
    File ${CLINK_SOURCE}\*.dll
    File ${CLINK_SOURCE}\*.lua
    File ${CLINK_SOURCE}\*.txt
    File ${CLINK_SOURCE}\clink_x*.exe
    File ${CLINK_SOURCE}\*.bat
    File ${CLINK_SOURCE}\*_inputrc

    ; Create a start-menu shortcut
    CreateDirectory "$SMPROGRAMS\clink"
    CreateShortcut "$SMPROGRAMS\clink\clink.lnk" "$INSTDIR\clink.bat" "" "$SYSDIR\cmd.exe" 0 SW_SHOWMINIMIZED 

    ; Create an uninstaller and a shortcut to it.
    WriteUninstaller "$INSTDIR\clink_uninstall.exe"
    CreateShortcut "$SMPROGRAMS\clink\Uninstall clink.lnk" "$INSTDIR\clink_uninstall.exe"

    ; Add to "add/remove programs"/"programs and features"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Product" "DisplayName" "clink"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Product" "UninstallString" "$INSTDIR\clink_uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Product" "Publisher" "Martin Ridgers"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Product" "DisplayIcon" "$SYSDIR\cmd.exe,0"
SectionEnd

;-------------------------------------------------------------------------------
Section "Autorun when cmd.exe starts"
    SetShellVarContext all
    ExecWait '"$INSTDIR\clink.bat" install'
SectionEnd



;-------------------------------------------------------------------------------
Section "!un.Application files"
    SetShellVarContext all

    ExecWait '"$INSTDIR\clink.bat" uninstall'

    RMDir /r $INSTDIR
    RMDir /r $SMPROGRAMS\clink

    DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Product" "DisplayName"
    DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Product" "UninstallString"
    DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Product" "Publisher"
    DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Product" "DisplayIcon"
SectionEnd

;-------------------------------------------------------------------------------
Section "un.User scripts and history"
    SetShellVarContext current

    RMDIR /r $APPDATA\clink
SectionEnd
