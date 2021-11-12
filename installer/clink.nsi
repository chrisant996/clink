;
; Copyright (c) 2014 Martin Ridgers
; Portions Copyright (c) 2021 Christopher Antos
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

!include "winmessages.nsh"
!include "Sections.nsh"

;-------------------------------------------------------------------------------
Name                    "clink v${CLINK_VERSION}"
InstallDir              "$PROGRAMFILES\clink"
OutFile                 "${CLINK_BUILD}_setup.exe"
AllowSkipFiles          off
SetCompressor           /SOLID lzma
LicenseBkColor          /windows
LicenseData             ${CLINK_SOURCE}\installer\license.rtf
LicenseForceSelection   off
RequestExecutionLevel   admin
XPStyle                 on

;-------------------------------------------------------------------------------
Page license
Page directory
Page components
Page instfiles

UninstPage uninstConfirm
UninstPage components
UninstPage instfiles

Var installRoot
Var uninstallerExe

;-------------------------------------------------------------------------------
Function cleanLegacyInstall
    IfFileExists $INSTDIR\..\clink_uninstall.exe +3 0
        DetailPrint "Install does not trample an existing one."
        Return

    ; Start menu items and uninstall registry entry.
    ;
    StrCpy $0 "Software\Microsoft\Windows\CurrentVersion\Uninstall" 
    Delete $SMPROGRAMS\clink\*
    RMDir $SMPROGRAMS\clink
    DeleteRegKey HKLM $0"\Product"

    ; Install dir
    ;
    Delete /REBOOTOK $INSTDIR\..\clink*

    ; Migrate state to the new location.
    ;
    IfFileExists $APPDATA\clink 0 +2
        Rename $APPDATA\clink $LOCALAPPDATA\clink
FunctionEnd

;-------------------------------------------------------------------------------
Function cleanPreviousInstalls
    StrCpy $0 "Software\Microsoft\Windows\CurrentVersion\Uninstall" 
    StrCpy $1 0
    EnumUninstallKeysLoop:
        EnumRegKey $2 HKLM $0 $1
        StrCmp $2 "" EnumUninstallKeysEnd

        ; Skip installs of ourself over an existing installation.
        ;
        StrCmp $2 "clink_${CLINK_VERSION}" EndIfClinkUninstallEntry 0
            ; Check for uninstaller entries that start "clink_"
            ;
            StrCpy $3 $2 6
            StrCmp $3 "clink_" 0 EndIfClinkUninstallEntry
                ReadRegStr $4 HKLM "$0\$2" "UninstallString"
                ExecWait '"$4" /S'
                DeleteRegKey HKLM "$0\$2"
        EndIfClinkUninstallEntry:

        IntOp $1 $1 + 1
        Goto EnumUninstallKeysLoop
    EnumUninstallKeysEnd:
FunctionEnd

;-------------------------------------------------------------------------------
Section "-" section_versioned_subdir_hidden
    ; Hidden placeholder so that app_files_id can see whether to use a
    ; versioned subdir.
SectionEnd

;-------------------------------------------------------------------------------
Section "!Application files" app_files_id
    SectionIn RO
    SetShellVarContext all
    
    ; Clean up version >= 0.2
    ;
    Call cleanPreviousInstalls

    ; Install to a versioned folder to reduce interference between versions.
    ;
    StrCpy $1 ${CLINK_VERSION}
    SectionGetFlags ${section_versioned_subdir_hidden} $0
    IntOp $0 $0 & ${SF_SELECTED}
    StrCmp $0 0 0 LUseVersionedSubDir
        StrCpy $1 bin
    LUseVersionedSubDir:
    StrCpy $installRoot $INSTDIR
    StrCpy $INSTDIR $INSTDIR\$1

    ; Installs the main files.
    ;
    CreateDirectory $INSTDIR
    SetOutPath $INSTDIR
    File ${CLINK_BUILD}\clink_dll_x*.dll
    File ${CLINK_BUILD}\CHANGES
    File ${CLINK_BUILD}\LICENSE
    File ${CLINK_BUILD}\clink_x*.exe
    File ${CLINK_BUILD}\clink.bat
    File ${CLINK_BUILD}\clink.html

    ; Create an uninstaller.
    ;
    StrCpy $uninstallerExe "clink_uninstall_${CLINK_VERSION}.exe"
    WriteUninstaller "$INSTDIR\$uninstallerExe"

    ; Add to "add/remove programs" or "programs and features"
    ;
    StrCpy $0 "Software\Microsoft\Windows\CurrentVersion\Uninstall\clink_${CLINK_VERSION}"
    WriteRegStr HKLM $0 "DisplayName"       "Clink v${CLINK_VERSION}"
    WriteRegStr HKLM $0 "UninstallString"   "$INSTDIR\$uninstallerExe"
    WriteRegStr HKLM $0 "Publisher"         "Christopher Antos"
    WriteRegStr HKLM $0 "DisplayIcon"       "$SYSDIR\cmd.exe,0"
    WriteRegStr HKLM $0 "URLInfoAbout"      "http://chrisant996.github.io/clink"
    WriteRegStr HKLM $0 "HelpLink"          "http://chrisant996.github.io/clink"
    WriteRegStr HKLM $0 "InstallLocation"   "$INSTDIR"
    WriteRegStr HKLM $0 "DisplayVersion"    "${CLINK_VERSION}"

    SectionGetSize ${app_files_id} $1
    WriteRegDWORD HKLM $0 "EstimatedSize"   $1

    ; Clean up legacy installs.
    ;
    Call cleanLegacyInstall

    CreateDirectory $LOCALAPPDATA\clink
SectionEnd

;-------------------------------------------------------------------------------
Section "Add shortcuts to Start menu" section_add_shortcuts
    SetShellVarContext all

    ; Create start menu folder.
    ;
    StrCpy $0 "$SMPROGRAMS\clink\${CLINK_VERSION}"
    CreateDirectory $0

    ; Add shortcuts to the program and documentation.
    ;
    CreateShortcut "$0\Clink v${CLINK_VERSION}.lnk" "$INSTDIR\clink.bat" 'startmenu --profile ~\clink' "$SYSDIR\cmd.exe" 0 SW_SHOWMINIMIZED
    CreateShortcut "$0\Clink v${CLINK_VERSION} Documentation.lnk" "$INSTDIR\clink.html"

    ; Add a shortcut to the uninstaller.
    ;
    CreateShortcut "$0\Uninstall Clink v${CLINK_VERSION}.lnk" "$INSTDIR\$uninstallerExe"
SectionEnd

;-------------------------------------------------------------------------------
Section "Autorun when cmd.exe starts" section_autorun
    SetShellVarContext all

    StrCpy $0 "~\clink"
    ExecShellWait "open" "$INSTDIR\clink_x86.exe" 'autorun --allusers uninstall' SW_HIDE
    ExecShellWait "open" "$INSTDIR\clink_x86.exe" 'autorun install -- --profile "$0"' SW_HIDE
SectionEnd

;-------------------------------------------------------------------------------
Section "Set %CLINK_DIR% to install location" section_clink_dir
    SetShellVarContext all

    StrCpy $0 "System\CurrentControlSet\Control\Session Manager\Environment"
    WriteRegExpandStr HKLM $0 "CLINK_DIR" $INSTDIR

    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=1000
SectionEnd

;-------------------------------------------------------------------------------
Section "Use versioned install directory" section_versioned_subdir_visible
    ; This is visible to the user.  .onSelChange applies the selection state
    ; from section_versioned_subdir_visible to section_versioned_subdir_hidden.
SectionEnd

;-------------------------------------------------------------------------------
Section "-"
    ; Remember the installation directory.
    WriteRegStr HKLM Software\Clink InstallDir $installRoot

    ; Remember the shortcuts choice.
    SectionGetFlags ${section_add_shortcuts} $0
    IntOp $0 $0 & ${SF_SELECTED}
    WriteRegDWORD HKLM Software\Clink AddShortcuts $0

    ; Remember the autorun choice.
    SectionGetFlags ${section_autorun} $0
    IntOp $0 $0 & ${SF_SELECTED}
    WriteRegDWORD HKLM Software\Clink UseAutoRun $0

    ; Remember the CLINK_DIR choice.
    SectionGetFlags ${section_clink_dir} $0
    IntOp $0 $0 & ${SF_SELECTED}
    WriteRegDWORD HKLM Software\Clink SetClinkDir $0

    ; Remember the versioned install directory choice.
    SectionGetFlags ${section_versioned_subdir_visible} $0
    IntOp $0 $0 & ${SF_SELECTED}
    WriteRegDWORD HKLM Software\Clink VersionedSubDir $0
SectionEnd

;-------------------------------------------------------------------------------
Function .onInit
    ; Apply remembered installation directory.
    ReadRegStr $0 HKLM Software\Clink InstallDir
    StrCmp $0 "" LEmptyInstallDir 0
        StrCpy $INSTDIR $0
    LEmptyInstallDir:

    ; Apply remembered selection state for shortcuts section.
    ReadRegDWORD $0 HKLM Software\Clink AddShortcuts
    StrCmp $0 "0" 0 LAddShortcuts
        SectionSetFlags ${section_add_shortcuts} 0
    LAddShortcuts:

    ; Apply remembered selection state for autorun section.
    ReadRegDWORD $0 HKLM Software\Clink UseAutoRun
    StrCmp $0 "0" 0 LUseAutoRun
        SectionSetFlags ${section_autorun} 0
    LUseAutoRun:

    ; Apply remembered selection state for CLINK_DIR section.
    ReadRegDWORD $0 HKLM Software\Clink SetClinkDir
    StrCmp $0 "0" 0 LSetClinkDir
        SectionSetFlags ${section_clink_dir} 0
    LSetClinkDir:

    ; Apply remembered selection state for versioned install directory section.
    ReadRegDWORD $0 HKLM Software\Clink VersionedSubDir
    StrCmp $0 "0" 0 LNotVersioned
        SectionSetFlags ${section_versioned_subdir_hidden} 0
        SectionSetFlags ${section_versioned_subdir_visible} 0
    LNotVersioned:
FunctionEnd

;-------------------------------------------------------------------------------
Function .onSelChange
    SectionGetFlags ${section_versioned_subdir_visible} $0
    SectionSetFlags ${section_versioned_subdir_hidden} $0
FunctionEnd

;-------------------------------------------------------------------------------
Section "!un.Application files"
    SectionIn RO
    SetShellVarContext all

    ExecShellWait "open" "$INSTDIR\clink_x86.exe" "autorun --allusers uninstall" SW_HIDE
    ExecShellWait "open" "$INSTDIR\clink_x86.exe" "autorun uninstall" SW_HIDE

    ; Delete the installation directory and root directory if it's empty.
    ;
    Delete /REBOOTOK $INSTDIR\clink*
    Delete $INSTDIR\CHANGES
    Delete $INSTDIR\LICENSE
    RMDir /REBOOTOK $INSTDIR
    RMDir /REBOOTOK $INSTDIR\..

    ; Remove start menu items and uninstall registry entries.
    RMDir /r $SMPROGRAMS\clink\${CLINK_VERSION}
    RMDir $SMPROGRAMS\clink
    DeleteRegKey HKLM Software\Clink
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\clink_${CLINK_VERSION}"
    DeleteRegValue HKLM "System\CurrentControlSet\Control\Session Manager\Environment" "CLINK_DIR"

    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=1000
SectionEnd

;-------------------------------------------------------------------------------
Section /o "un.User scripts and history"
    SetShellVarContext all

    RMDIR /r $APPDATA\clink         ; ...legacy path.
    RMDIR /r $LOCALAPPDATA\clink
SectionEnd
