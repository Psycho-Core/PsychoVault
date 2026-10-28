; PsychoVault installer
; Build: makensis -DPAYLOADDIR=/path/to/staged -DOUTFILE=/path/out.exe installer.nsi

Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"

!define PRODUCT   "PsychoVault"
!ifndef VERSION
  !define VERSION "1.0.1"
!endif
!define PUBLISHER "Psycho-Core"
!define APPURL    "https://github.com/"

!ifndef PAYLOADDIR
  !define PAYLOADDIR "/var/tmp/stage/PsychoVault"
!endif
!ifndef OUTFILE
  !define OUTFILE "PsychoVault-Setup-${VERSION}.exe"
!endif
!ifndef MUI_ICON
  !define MUI_ICON "assets/psychovault.ico"
!endif
!define MUI_UNICON "${MUI_ICON}"

Name "${PRODUCT} ${VERSION}"
OutFile "${OUTFILE}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\PsychoVault"
InstallDirRegKey HKCU "Software\PsychoVault" "InstallDir"
SetCompressor /SOLID lzma
SetCompressorDictSize 32
SetDatablockOptimize on
AllowRootDirInstall false
XPStyle on

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName"     "${PRODUCT}"
VIAddVersionKey "ProductVersion"  "${VERSION}"
VIAddVersionKey "FileVersion"     "${VERSION}.0"
VIAddVersionKey "CompanyName"     "${PUBLISHER}"
VIAddVersionKey "LegalCopyright"  "(c) 2026 ${PUBLISHER}. MIT. Chromium and Electron components under their own licences."
VIAddVersionKey "FileDescription" "${PRODUCT} Setup"
VIAddVersionKey "Comments"        "Browser application built on Electron 45.1.0 (Chromium 140)."

!define MUI_ABORTWARNING
!define MUI_HEADERIMAGE_RIGHT
!define MUI_WELCOMEPAGE_TITLE "Install ${PRODUCT} ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "A browser I wrote myself, running on Chromium.\r\n\r\nIt installs into your own user folder - no administrator rights, no services, no system changes.\r\n\r\n- Own tab strip, omnibox, bookmarks, history and downloads\r\n- 262-domain ad and tracker blocking, on by default\r\n- Private windows that write nothing to disk\r\n- Split view, command palette, sidebar panel\r\n- Workspaces, tab pinning, per-page blocker counters\r\n- A local password vault, encrypted, syncing to nothing\r\n- GX Control: CPU, RAM and bandwidth limiters, hot tabs, panic button\r\n- Service rail with real site icons, built-in spell checking, extension manager\r\n- Save As for every download, with settings grouped by category\r\n- Dark navy interface with cyan accents\r\n\r\nAbout 1 GB of disk space. First start takes a few seconds."

!define MUI_FINISHPAGE_TITLE "${PRODUCT} is installed"
!define MUI_FINISHPAGE_TEXT "Your browser is ready.\r\n\r\nCtrl+K opens the command palette. Ctrl+Shift+P opens a private window. Ctrl+1..9 switches workspaces.\r\nSettings live in the gear at the bottom of the left rail - the Passwords section there is where you set a master password before you rely on the vault."
!define MUI_FINISHPAGE_RUN "$INSTDIR\PsychoVault.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${PRODUCT} now"
!define MUI_FINISHPAGE_LINK "Open README.txt"
!define MUI_FINISHPAGE_LINK_LOCATION "$INSTDIR\README.txt"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PAYLOADDIR}/LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install"
  SetOverwrite on
  SetOutPath "$INSTDIR"
  ; an update should not leave stale files behind: the payload subfolders are ours,
  ; your profile (bookmarks, history, vault) lives in %APPDATA% and is never touched here
  RMDir /r "$INSTDIR\locales"
  RMDir /r "$INSTDIR\resources"
  File /r "${PAYLOADDIR}/*.*"
  ${If} ${Errors}
    ; something still had a handle on it - close the browser and try once more
    nsExec::ExecToLog 'taskkill /F /IM PsychoVault.exe'
    Sleep 1500
    ClearErrors
    RMDir /r "$INSTDIR\locales"
    RMDir /r "$INSTDIR\resources"
    File /r "${PAYLOADDIR}/*.*"
    ${If} ${Errors}
      MessageBox MB_OK|MB_ICONEXCLAMATION 'Some files could not be written - they are still in use.$\r$\n$\r$\nClose any PsychoVault window and run Setup again. Nothing was removed from your profile.'
      Abort
    ${EndIf}
  ${EndIf}

  ; start menu + desktop shortcuts
  CreateDirectory "$SMPROGRAMS\PsychoVault"
  CreateShortcut "$SMPROGRAMS\PsychoVault\PsychoVault.lnk" "$INSTDIR\PsychoVault.exe" "" "$INSTDIR\PsychoVault.exe" 0
  CreateShortcut "$SMPROGRAMS\PsychoVault\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\PsychoVault.lnk" "$INSTDIR\PsychoVault.exe" "" "$INSTDIR\PsychoVault.exe" 0

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "Software\PsychoVault" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PsychoVault" "DisplayName"     "${PRODUCT} ${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PsychoVault" "DisplayVersion"  "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PsychoVault" "Publisher"       "${PUBLISHER}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PsychoVault" "DisplayIcon"     "$INSTDIR\PsychoVault.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PsychoVault" "URLInfoAbout"    "${APPURL}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PsychoVault" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PsychoVault" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PsychoVault" "NoRepair" 1

  ; register the http/https handlers so Windows can offer PsychoVault as a browser
  WriteRegStr HKCU "Software\Clients\StartMenuInternet\PsychoVault" "" "${PRODUCT}"
  WriteRegStr HKCU "Software\Clients\StartMenuInternet\PsychoVault\Capabilities" "ApplicationName" "${PRODUCT}"
  WriteRegStr HKCU "Software\Clients\StartMenuInternet\PsychoVault\Capabilities" "ApplicationDescription" "Private browser built on Chromium"
  WriteRegStr HKCU "Software\Clients\StartMenuInternet\PsychoVault\Capabilities" "ApplicationIcon" "$INSTDIR\PsychoVault.exe,0"
  WriteRegStr HKCU "Software\Clients\StartMenuInternet\PsychoVault\Capabilities\URLAssociations" "http"  "PsychoVaultHTM"
  WriteRegStr HKCU "Software\Clients\StartMenuInternet\PsychoVault\Capabilities\URLAssociations" "https" "PsychoVaultHTM"
  WriteRegStr HKCU "Software\Classes\PsychoVaultHTM\shell\open\command" "" '"$INSTDIR\PsychoVault.exe" "%1"'
  WriteRegStr HKCU "Software\RegisteredApplications" "${PRODUCT}" "Software\Clients\StartMenuInternet\PsychoVault\Capabilities"
SectionEnd

Function .onInit
  ; Installing over a live copy used to end in "Can't write: ...PsychoVault.exe", because
  ; Windows will not overwrite a running executable. So: ask, close it, then carry on.
  ${If} ${FileExists} "$INSTDIR\PsychoVault.exe"
    ClearErrors
    ; opening a running exe for append fails with a sharing violation - that is the test
    FileOpen $9 "$INSTDIR\PsychoVault.exe" a
    ${If} ${Errors}
      MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION 'PsychoVault is running, and Windows will not let Setup overwrite the files it has open.$\r$\n$\r$\nOK closes the browser and continues updating. Cancel leaves your existing install exactly as it is.' IDOK pv_kill IDCANCEL pv_abort
      pv_abort:
        Abort
      pv_kill:
      nsExec::ExecToLog 'taskkill /F /IM PsychoVault.exe'
      Sleep 1500
    ${Else}
      FileClose $9
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "Uninstall"
  ; same reason as the install: a running app holds its own files
  nsExec::ExecToLog 'taskkill /F /IM PsychoVault.exe'
  Sleep 800
  Delete "$DESKTOP\PsychoVault.lnk"
  Delete "$SMPROGRAMS\PsychoVault\PsychoVault.lnk"
  Delete "$SMPROGRAMS\PsychoVault\Uninstall.lnk"
  RMDir  "$SMPROGRAMS\PsychoVault"

  RMDir /r "$INSTDIR\locales"
  RMDir /r "$INSTDIR\resources"
  RMDir /r "$INSTDIR\swiftshader"
  RMDir /r "$INSTDIR\Dictionaries"
  Delete "$INSTDIR\*.*"
  RMDir /r "$INSTDIR"

  DeleteRegKey HKCU "Software\PsychoVault"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PsychoVault"
  DeleteRegKey HKCU "Software\Clients\StartMenuInternet\PsychoVault"
  DeleteRegKey HKCU "Software\Classes\PsychoVaultHTM"
  DeleteRegValue HKCU "Software\RegisteredApplications" "${PRODUCT}"

  ; app data (history, bookmarks, settings) is left alone on purpose
  MessageBox MB_YESNO "Also delete your PsychoVault profile? Bookmarks, history and settings live there." IDNO +2
  RMDir /r "$APPDATA\PsychoVault"
SectionEnd
