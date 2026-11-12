; DeepPeak installer - per-user, no admin, clean uninstall.
; Build: .\build-installer.ps1   (output in dist\)

Unicode true
SetCompressor /SOLID lzma
SetCompressorDictSize 32

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !define VERSION "1.0.0"
!endif

!define APPNAME    "DeepPeak"
!define APPEXE     "DeepPeak.exe"
!define PUBLISHER  "theacidblack"
!define HOMEPAGE   "https://github.com/theacidblack/DeepPeak"
!define REGKEY     "Software\${APPNAME}"
!define UNINSTKEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
!define RUNKEY     "Software\Microsoft\Windows\CurrentVersion\Run"

Name "${APPNAME}"
BrandingText "${APPNAME} ${VERSION}"
OutFile "dist\DeepPeak-${VERSION}-setup.exe"
InstallDir "$LOCALAPPDATA\Programs\${APPNAME}"
InstallDirRegKey HKCU "${UNINSTKEY}" "InstallLocation"
RequestExecutionLevel user

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName"     "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} installer"
VIAddVersionKey "FileVersion"     "${VERSION}"
VIAddVersionKey "ProductVersion"  "${VERSION}"
VIAddVersionKey "CompanyName"     "${PUBLISHER}"
VIAddVersionKey "LegalCopyright"  "MIT License"
VIAddVersionKey "OriginalFilename" "DeepPeak-${VERSION}-setup.exe"

; ------------------------------------------------------------------ look

!define MUI_ICON   "app.ico"
!define MUI_UNICON "app.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Install ${APPNAME}"
!define MUI_WELCOMEPAGE_TEXT "DeepPeak lives in the system tray and tells you when DeepSeek V4.1 Flash is billed at peak or off-peak rates.$\r$\n$\r$\nIt is a single small app: no background services, no network access, no admin rights. Uninstalling removes every file and registry value it created."
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APPEXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APPNAME} now"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------ install

Section "DeepPeak (required)" SEC_CORE
    SectionIn RO

    ; Stop a running copy so the exe is not locked (works for upgrades too).
    ${If} ${FileExists} "$INSTDIR\${APPEXE}"
        nsExec::Exec '"$INSTDIR\${APPEXE}" --quit'
        Sleep 700
    ${EndIf}

    SetOutPath "$INSTDIR"
    File "build\${APPEXE}"
    File "LICENSE"

    SetOutPath "$INSTDIR\sounds"
    File "sounds\peak.mp3"
    File "sounds\offpeak.mp3"

    SetOutPath "$INSTDIR"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    CreateDirectory "$SMPROGRAMS\${APPNAME}"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${APPEXE}"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\Uninstall.exe"

    ; Default for the sound preference, never overwriting an existing choice.
    ReadRegDWORD $0 HKCU "${REGKEY}" "SoundNotifications"
    ${If} ${Errors}
        WriteRegDWORD HKCU "${REGKEY}" "SoundNotifications" 1
    ${EndIf}

    WriteRegStr HKCU "${UNINSTKEY}" "DisplayName"     "${APPNAME}"
    WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion"  "${VERSION}"
    WriteRegStr HKCU "${UNINSTKEY}" "Publisher"       "${PUBLISHER}"
    WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon"     "$INSTDIR\${APPEXE}"
    WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTKEY}" "URLInfoAbout"    "${HOMEPAGE}"
    WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1

    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" "$0"
SectionEnd

Section "Start with Windows" SEC_AUTOSTART
    WriteRegStr HKCU "${RUNKEY}" "${APPNAME}" '"$INSTDIR\${APPEXE}"'
SectionEnd

Section /o "Desktop shortcut" SEC_DESKTOP
    CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${APPEXE}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_CORE}      "The tray app, its two sound clips and the uninstaller."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_AUTOSTART} "Launch DeepPeak when you sign in. You can also toggle this later from the tray menu."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP}    "Add a shortcut to the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ----------------------------------------------------------------- uninstall

Section "Uninstall"
    ; Ask a running copy to exit before deleting its files.
    ${If} ${FileExists} "$INSTDIR\${APPEXE}"
        nsExec::Exec '"$INSTDIR\${APPEXE}" --quit'
        Sleep 900
    ${EndIf}

    Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
    Delete "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk"
    RMDir "$SMPROGRAMS\${APPNAME}"
    Delete "$DESKTOP\${APPNAME}.lnk"

    DeleteRegValue HKCU "${RUNKEY}" "${APPNAME}"
    DeleteRegKey HKCU "${REGKEY}"
    DeleteRegKey HKCU "${UNINSTKEY}"

    Delete "$INSTDIR\sounds\peak.mp3"
    Delete "$INSTDIR\sounds\offpeak.mp3"
    RMDir  "$INSTDIR\sounds"
    Delete "$INSTDIR\${APPEXE}"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
