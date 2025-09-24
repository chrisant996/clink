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

!include "MUI2.nsh"
!include "winmessages.nsh"
!include "Sections.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"

;-------------------------------------------------------------------------------
Unicode                 true
Name                    "clink v${CLINK_VERSION}"
; InstallDir and InstallDirRegKey are omitted so that /D= usage can be detected.
; The .onInit function handles providing a default value when /D= is omitted,
; reading the InstallDir regkey when appropriate.  The "-" section handles
; writing the InstallDir regkey.
;InstallDir              "$PROGRAMFILES\clink"
;InstallDirRegKey        HKLM "Software\Clink" "InstallDir"
OutFile                 "${CLINK_BUILD}_setup.exe"
AllowSkipFiles          off
SetCompressor           /SOLID lzma
LicenseBkColor          /windows
LicenseForceSelection   off
RequestExecutionLevel   admin
XPStyle                 on

;-------------------------------------------------------------------------------
!insertmacro GetParameters
!insertmacro GetOptions

;-------------------------------------------------------------------------------
!define MUI_COMPONENTSPAGE_SMALLDESC

!define MUI_UI_COMPONENTSPAGE_SMALLDESC "${CLINK_SOURCE}\installer\modern_mediumdesc.exe"

!insertmacro MUI_PAGE_LICENSE "${CLINK_SOURCE}\installer\license.rtf"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_COMPONENTS
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

;-------------------------------------------------------------------------------
Var uninstallerExe

;-------------------------------------------------------------------------------
Function cleanLegacyInstall
    ; Start menu items and uninstall registry entry.
    ;
    StrCpy $0 "Software\Microsoft\Windows\CurrentVersion\Uninstall"
    Delete $SMPROGRAMS\clink\*
    RMDir $SMPROGRAMS\clink
    DeleteRegKey HKLM $0"\Product"

    ; Install dir
    ;
    Delete /REBOOTOK $INSTDIR

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
        StrCmp $2 "clink_chrisant996" EndIfClinkUninstallEntry 0
            ; Check for uninstaller entries that start "clink_"
            ;
            StrCpy $3 $2 6
            StrCmp $3 "clink_" 0 EndIfClinkUninstallEntry
                ReadRegStr $4 HKLM "$0\$2" "UninstallString"
                ${GetParent} $4 $5
                ExecWait '"$4" /S _?=$5'
                Delete $4
                DeleteRegKey HKLM "$0\$2"
        EndIfClinkUninstallEntry:

        IntOp $1 $1 + 1
        Goto EnumUninstallKeysLoop
    EnumUninstallKeysEnd:
FunctionEnd

;-------------------------------------------------------------------------------
Function cleanPreviousUninstallers
    Delete /REBOOTOK $INSTDIR\clink_uninstall*.exe
FunctionEnd

;-------------------------------------------------------------------------------
Section "!Application files" app_files_id
    SectionIn RO
    SetShellVarContext all

    ; Clean up version >= 0.2
    ;
    Call cleanPreviousInstalls

    ; Installs the main files.
    ;
    CreateDirectory $INSTDIR
    SetOutPath $INSTDIR
    File ${CLINK_BUILD}\clink_dll_x*.dll
    File ${CLINK_BUILD}\clink.ico
    File ${CLINK_BUILD}\CHANGES
    File ${CLINK_BUILD}\LICENSE
    File ${CLINK_BUILD}\clink_x*.exe
    File ${CLINK_BUILD}\clink.bat
    File ${CLINK_BUILD}\clink.html
    File ${CLINK_BUILD}\CaskaydiaCoveNerdFontMono-Regular.woff2
    ${If} ${IsNativeARM64}
        File ${CLINK_BUILD}\clink_dll_arm*.dll
        File ${CLINK_BUILD}\clink_arm*.exe
    ${EndIf}

    CreateDirectory $INSTDIR\themes
    SetOutPath $INSTDIR\themes
    File "${CLINK_BUILD}\themes\4-bit Enhanced Defaults.clinktheme"
    File "${CLINK_BUILD}\themes\Dracula.clinktheme"
    File "${CLINK_BUILD}\themes\Enhanced Defaults.clinktheme"
    File "${CLINK_BUILD}\themes\Plain.clinktheme"
    File "${CLINK_BUILD}\themes\Solarized Dark.clinktheme"
    File "${CLINK_BUILD}\themes\Solarized Light.clinktheme"
    File "${CLINK_BUILD}\themes\Tomorrow.clinktheme"
    File "${CLINK_BUILD}\themes\Tomorrow Night.clinktheme"
    File "${CLINK_BUILD}\themes\Tomorrow Night Blue.clinktheme"
    File "${CLINK_BUILD}\themes\Tomorrow Night Bright.clinktheme"
    File "${CLINK_BUILD}\themes\Tomorrow Night Eighties.clinktheme"
    File "${CLINK_BUILD}\themes\agnoster.clinkprompt"
    File "${CLINK_BUILD}\themes\Antares.clinkprompt"
    File "${CLINK_BUILD}\themes\bureau.clinkprompt"
    File "${CLINK_BUILD}\themes\darkblood.clinkprompt"
    File "${CLINK_BUILD}\themes\Headline.clinkprompt"
    File "${CLINK_BUILD}\themes\jonathan.clinkprompt"
    File "${CLINK_BUILD}\themes\oh-my-posh.clinkprompt"
    File "${CLINK_BUILD}\themes\pure.clinkprompt"
    File "${CLINK_BUILD}\themes\starship.clinkprompt"
    SetOutPath $INSTDIR

    ; Clean up previous uninstallers.
    ;
    Call cleanPreviousUninstallers

    ; Create an uninstaller.
    ;
    StrCpy $uninstallerExe "clink_uninstall_${CLINK_VERSION}.exe"
    WriteUninstaller "$INSTDIR\$uninstallerExe"

    ; Add to "add/remove programs" or "programs and features"
    ;
    StrCpy $0 "Software\Microsoft\Windows\CurrentVersion\Uninstall\clink_chrisant996"
    WriteRegStr HKLM $0 "DisplayName"       "Clink v${CLINK_TAGVERSION}"
    WriteRegStr HKLM $0 "UninstallString"   "$INSTDIR\$uninstallerExe"
    WriteRegStr HKLM $0 "Publisher"         "Christopher Antos"
    WriteRegStr HKLM $0 "DisplayIcon"       "$INSTDIR\clink.ico,0"
    WriteRegStr HKLM $0 "URLInfoAbout"      "http://chrisant996.github.io/clink"
    WriteRegStr HKLM $0 "HelpLink"          "http://chrisant996.github.io/clink"
    WriteRegStr HKLM $0 "InstallLocation"   "$INSTDIR"
    WriteRegStr HKLM $0 "DisplayVersion"    "${CLINK_TAGVERSION}"

    SectionGetSize ${app_files_id} $1
    WriteRegDWORD HKLM $0 "EstimatedSize"   $1

    ; Clean up legacy installs.
    ;
    Call cleanLegacyInstall

    CreateDirectory $LOCALAPPDATA\clink
SectionEnd

;-------------------------------------------------------------------------------
Section "Use enhanced default settings" section_enhance
    SetShellVarContext all

    File /oname=default_settings ${CLINK_BUILD}\_default_settings
    File /oname=default_inputrc ${CLINK_BUILD}\_default_inputrc
SectionEnd

;-------------------------------------------------------------------------------
Section "Add shortcuts to Start menu" section_add_shortcuts
    SetShellVarContext all

    ; Create start menu folder.
    ;
    StrCpy $0 "$SMPROGRAMS\Clink"
    CreateDirectory $0

    ; Add shortcut to the program.
    ;
    SetOutPath "%USERPROFILE%"
    CreateShortcut "$0\Clink.lnk" "$INSTDIR\clink.bat" '--profile ~\clink' "$INSTDIR\clink.ico" 0 SW_SHOWMINIMIZED
    SetOutPath $INSTDIR

    ; Add shortcut to the documentation.
    ;
    CreateShortcut "$0\Clink Documentation.lnk" "$INSTDIR\clink.html"

    ; Add a shortcut to the uninstaller.
    ;
    CreateShortcut "$0\Uninstall Clink.lnk" "$INSTDIR\$uninstallerExe"
SectionEnd

;-------------------------------------------------------------------------------
Section "Set %CLINK_DIR% to install location" section_clink_dir
    SetShellVarContext all

    StrCpy $0 "System\CurrentControlSet\Control\Session Manager\Environment"
    WriteRegExpandStr HKLM $0 "CLINK_DIR" $INSTDIR

    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=1000
SectionEnd

;-------------------------------------------------------------------------------
Section "Autorun when cmd.exe starts" section_autorun
    SetShellVarContext all

    StrCpy $0 "~\clink"
    ExecShellWait "open" "$INSTDIR\clink_x86.exe" 'autorun --allusers uninstall' SW_HIDE
    ExecShellWait "open" "$INSTDIR\clink_x86.exe" 'autorun install -- --profile "$0"' SW_HIDE
SectionEnd

;-------------------------------------------------------------------------------
Section "Install shortcut icons" section_icons
    SetShellVarContext all

    File /oname=clink_red.ico ${CLINK_BUILD}\clink_red.ico
    File /oname=clink_orange.ico ${CLINK_BUILD}\clink_orange.ico
    File /oname=clink_gold.ico ${CLINK_BUILD}\clink_gold.ico
    File /oname=clink_green.ico ${CLINK_BUILD}\clink_green.ico
    File /oname=clink_cyan.ico ${CLINK_BUILD}\clink_cyan.ico
    File /oname=clink_blue.ico ${CLINK_BUILD}\clink_blue.ico
    File /oname=clink_purple.ico ${CLINK_BUILD}\clink_purple.ico
    File /oname=clink_gray.ico ${CLINK_BUILD}\clink_gray.ico
SectionEnd

;-------------------------------------------------------------------------------
Section "-"
    ; Remember the installation directory.
    WriteRegStr HKLM Software\Clink InstallDir $INSTDIR

    ; Remember the enhanced default settings choice.
    SectionGetFlags ${section_enhance} $0
    IntOp $0 $0 & ${SF_SELECTED}
    WriteRegDWORD HKLM Software\Clink EnhancedDefaultSettings $0

    ; Remember the shortcuts choice.
    SectionGetFlags ${section_add_shortcuts} $0
    IntOp $0 $0 & ${SF_SELECTED}
    WriteRegDWORD HKLM Software\Clink AddShortcuts $0

    ; Remember the autorun choice.
    SectionGetFlags ${section_autorun} $0
    IntOp $0 $0 & ${SF_SELECTED}
    WriteRegDWORD HKLM Software\Clink UseAutoRun $0

    ; Remember the icons choice.
    SectionGetFlags ${section_icons} $0
    IntOp $0 $0 & ${SF_SELECTED}
    WriteRegDWORD HKLM Software\Clink ShortcutIcons $0

    ; Remember the CLINK_DIR choice.
    SectionGetFlags ${section_clink_dir} $0
    IntOp $0 $0 & ${SF_SELECTED}
    WriteRegDWORD HKLM Software\Clink SetClinkDir $0
SectionEnd

;-------------------------------------------------------------------------------
Function .onInit
    ${GetParameters} $1

    ; Check command line options.
    ClearErrors
    ${GetOptions} $1 '/?' $0
    IfErrors 0 +4
    ClearErrors
    ${GetOptions} $1 '-?' $0
    IfErrors +3 0
        MessageBox MB_OK "/?$\t$\tShow this help.$\n/noEnhance$\tUncheck 'Use enhanced default settings'.$\n/S$\t$\tSilently install.$\n/D=dir$\t$\tInstallation directory.$\n$\nIf /D=dir is used, it must come last in the command line, it must not use quotes, and dir must be an absolute path."
        Abort

    ; Apply remembered installation directory, unless /D is used.
    StrCmp $INSTDIR "" 0 LInstallDir
        StrCpy $INSTDIR "$PROGRAMFILES\clink"
        ReadRegStr $0 HKLM Software\Clink InstallDir
        StrCmp $0 "" LInstallDir 0
            StrCpy $INSTDIR $0
    LInstallDir:

    ; Apply remembered selection state for enhanced default settings section.
    SectionSetFlags ${section_enhance} 0
    ClearErrors
    ${GetOptions} $1 '/noEnhance' $0
    IfErrors 0 LEnhancedDefaultSettings
        ReadRegDWORD $0 HKLM Software\Clink EnhancedDefaultSettings
        SectionSetFlags ${section_enhance} ${SF_SELECTED}
        StrCmp $0 "0" 0 LEnhancedDefaultSettings
            SectionSetFlags ${section_enhance} 0
    LEnhancedDefaultSettings:

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

    ; Apply remembered selection state for icons section.
    ReadRegDWORD $0 HKLM Software\Clink ShortcutIcons
    StrCmp $0 "0" 0 LShortcutIcons
        SectionSetFlags ${section_icons} 0
    LShortcutIcons:

    ; Apply remembered selection state for CLINK_DIR section.
    ReadRegDWORD $0 HKLM Software\Clink SetClinkDir
    StrCmp $0 "0" 0 LSetClinkDir
        SectionSetFlags ${section_clink_dir} 0
    LSetClinkDir:
FunctionEnd

;-------------------------------------------------------------------------------
Function .onInstSuccess
    SetErrorLevel 0
FunctionEnd

;-------------------------------------------------------------------------------
Section "!un.Application files" section_un_app_files
    SectionIn RO
    SetShellVarContext all

    ExecShellWait "open" "$INSTDIR\clink_x86.exe" "autorun --allusers uninstall" SW_HIDE
    ExecShellWait "open" "$INSTDIR\clink_x86.exe" "autorun --enumusers uninstall" SW_HIDE

    ; Delete the installation directory and root directory if it's empty.
    ;
    Delete /REBOOTOK $INSTDIR\clink*
    Delete $INSTDIR\CHANGES
    Delete $INSTDIR\LICENSE
    Delete $INSTDIR\default_settings
    Delete $INSTDIR\default_inputrc
    Delete "$INSTDIR\themes\4-bit Enhanced Defaults.clinktheme"
    Delete "$INSTDIR\themes\Dracula.clinktheme"
    Delete "$INSTDIR\themes\Enhanced Defaults.clinktheme"
    Delete "$INSTDIR\themes\Plain.clinktheme"
    Delete "$INSTDIR\themes\Solarized Dark.clinktheme"
    Delete "$INSTDIR\themes\Solarized Light.clinktheme"
    Delete "$INSTDIR\themes\Tomorrow.clinktheme"
    Delete "$INSTDIR\themes\Tomorrow Night.clinktheme"
    Delete "$INSTDIR\themes\Tomorrow Night Blue.clinktheme"
    Delete "$INSTDIR\themes\Tomorrow Night Bright.clinktheme"
    Delete "$INSTDIR\themes\Tomorrow Night Eighties.clinktheme"
    Delete "$INSTDIR\themes\agnoster.clinkprompt"
    Delete "$INSTDIR\themes\Antares.clinkprompt"
    Delete "$INSTDIR\themes\bureau.clinkprompt"
    Delete "$INSTDIR\themes\darkblood.clinkprompt"
    Delete "$INSTDIR\themes\Headline.clinkprompt"
    Delete "$INSTDIR\themes\jonathan.clinkprompt"
    Delete "$INSTDIR\themes\oh-my-posh.clinkprompt"
    Delete "$INSTDIR\themes\pure.clinkprompt"
    Delete "$INSTDIR\themes\starship.clinkprompt"
    RMDir /REBOOTOK $INSTDIR\themes
    RMDir /REBOOTOK $INSTDIR

    ; Remove start menu items and uninstall registry entries.
    RMDir /r $SMPROGRAMS\Clink
    DeleteRegKey HKLM Software\Clink
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\clink_chrisant996"
    DeleteRegValue HKLM "System\CurrentControlSet\Control\Session Manager\Environment" "CLINK_DIR"

    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=1000
SectionEnd

;-------------------------------------------------------------------------------
Section /o "un.User scripts and history" section_un_scripts
    SetShellVarContext all

    RMDIR /r $APPDATA\clink         ; ...legacy path.
    RMDIR /r $LOCALAPPDATA\clink
SectionEnd

;-------------------------------------------------------------------------------
LangString desc_app_files           ${LANG_ENGLISH} "Installs the Clink application files."
LangString desc_enhanced_defaults   ${LANG_ENGLISH} "Pre-configures Clink with several popular enhancements enabled, including colors and some familiar CMD key bindings.  Any of them can be changed after installation.  Refer to the Getting Started docs for more info."
LangString desc_add_shortcuts       ${LANG_ENGLISH} "Adds Start Menu shortcuts for Clink and its documentation."
LangString desc_clink_dir           ${LANG_ENGLISH} "Sets %CLINK_DIR% to the Clink install location."
LangString desc_autorun             ${LANG_ENGLISH} "Configures the CMD.EXE AutoRun regkey to inject Clink when CMD.EXE starts.  This can be convenient, but also makes starting CMD.EXE always a little slower."
LangString desc_icons               ${LANG_ENGLISH} "Installs Clink icon files for use in shortcuts, terminal tabs, etc."

LangString undesc_app_files         ${LANG_ENGLISH} "Removes the Clink application files."
LangString undesc_scripts           ${LANG_ENGLISH} "Removes the default Clink profile directory for the current user."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${app_files_id} $(desc_app_files)
    !insertmacro MUI_DESCRIPTION_TEXT ${section_enhance} $(desc_enhanced_defaults)
    !insertmacro MUI_DESCRIPTION_TEXT ${section_add_shortcuts} $(desc_add_shortcuts)
    !insertmacro MUI_DESCRIPTION_TEXT ${section_clink_dir} $(desc_clink_dir)
    !insertmacro MUI_DESCRIPTION_TEXT ${section_autorun} $(desc_autorun)
    !insertmacro MUI_DESCRIPTION_TEXT ${section_icons} $(desc_icons)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

!insertmacro MUI_UNFUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${section_un_app_files} $(undesc_app_files)
    !insertmacro MUI_DESCRIPTION_TEXT ${section_un_scripts} $(undesc_scripts)
!insertmacro MUI_UNFUNCTION_DESCRIPTION_END
