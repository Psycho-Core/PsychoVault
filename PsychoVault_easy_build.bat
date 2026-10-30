@echo off
rem ===========================================================================
rem  PsychoVault easy build  -  double-click me on Windows.
rem
rem  What it does, in order:
rem    1. checks Node.js is installed and reads the version
rem    2. npm install in app\      (downloads the Electron 45 runtime, ~120 MB)
rem    3. copies that runtime to build\payload and rebrands electron.exe
rem    4. npm ci in tools\        (downloads resedit, used to set icon/version)
rem    5. copies the app source into build\payload\resources\app
rem    6. checks the payload really contains what the browser needs
rem    7. if NSIS is installed, builds PsychoVault-Setup-<version>.exe
rem    8. writes Release-Notes-<version>.txt next to this file
rem
rem  Result: build\payload\PsychoVault.exe is a portable, runnable browser.
rem  Nothing here needs administrator rights and nothing costs money.
rem  Full instructions: BUILD-WINDOWS.md
rem ===========================================================================

setlocal EnableExtensions EnableDelayedExpansion
title PsychoVault easy build
color 0B
cd /d "%~dp0"

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD=%ROOT%\build"
set "PAYLOAD=%BUILD%\payload"

rem Files the browser cannot start or update without. Checked in step 6.
set "VERIFY=PsychoVault.exe LICENSE.txt resources\app\package.json resources\app\main.js"
set "VERIFY=%VERIFY% resources\app\preload.js resources\app\auto-updater.js resources\app\update-public-key.pem"
set "VERIFY=%VERIFY% resources\app\blocklist.json resources\app\spell-words.txt resources\app\ui\index.html"
set "VERIFY=%VERIFY% resources\app\ui\renderer.js resources\app\ui\style.css resources\app\ui\settings.html"
set "VERIFY=%VERIFY% resources\app\builtin\proton-vpn\manifest.json"

rem A ( or & in the folder name would break the unquoted echo lines further down.
echo "%ROOT%"| findstr /c:"(" /c:"&" >nul
if not errorlevel 1 goto :bad_path

echo ============================================================
echo   PsychoVault easy build
echo   Repo   : %ROOT%
echo   Output : %BUILD%
echo ============================================================
echo.

rem ---------------------------------------------------------------- 1. Node.js
echo [1/8] Checking for Node.js...
where node >nul 2>nul
if errorlevel 1 goto :no_node
for /f "delims=" %%v in ('node -p "process.versions.node"') do set "NODEV=%%v"
echo       Node.js v!NODEV! found.

pushd "%ROOT%\app"
set "VERSION=unknown"
for /f "delims=" %%v in ('node -p "require('./package.json').version"') do set "VERSION=%%v"
popd
echo %VERSION%| findstr /r /c:"^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul
if errorlevel 1 goto :fail_version
echo       PsychoVault version: %VERSION%
echo.

rem ------------------------------------------------------- 2. Electron runtime
echo [2/8] Installing the Electron runtime (first run downloads ~120 MB)...
pushd "%ROOT%\app"
call npm install --no-audit --no-fund
if errorlevel 1 (
  popd
  goto :fail_npm
)
popd

if not exist "%ROOT%\app\node_modules\electron\dist\electron.exe" goto :no_electron
echo       Runtime is in place.
echo.

rem ------------------------------------------------------------ 3. Staging
echo [3/8] Staging the runtime into build\payload...
if not exist "%BUILD%" mkdir "%BUILD%" >nul 2>nul
robocopy "%ROOT%\app\node_modules\electron\dist" "%PAYLOAD%" /MIR /NFL /NDL /NJH /NJS /NP /R:2 /W:1 >nul
if errorlevel 8 goto :fail_copy

if exist "%PAYLOAD%\electron.exe" ren "%PAYLOAD%\electron.exe" PsychoVault.exe
if exist "%PAYLOAD%\resources\default_app.asar" del /f /q "%PAYLOAD%\resources\default_app.asar"
if exist "%PAYLOAD%\resources\app.asar" del /f /q "%PAYLOAD%\resources\app.asar"
if exist "%PAYLOAD%\resources\app" rmdir /s /q "%PAYLOAD%\resources\app"
if not exist "%PAYLOAD%\PsychoVault.exe" goto :fail_copy
echo       PsychoVault.exe staged.
echo.

rem ------------------------------------------------------- 4. Brand the exe
echo [4/8] Setting the PsychoVault icon and version number...
pushd "%ROOT%\tools"
call npm ci --no-audit --no-fund
if errorlevel 1 (
  popd
  goto :fail_tools
)
popd

node "%ROOT%\tools\patch-exe.mjs" "%PAYLOAD%\PsychoVault.exe" "%ROOT%\assets\psychovault.ico" %VERSION%
if errorlevel 1 goto :fail_patch
echo.

rem --------------------------------------------------------- 5. The app itself
echo [5/8] Copying the browser source into the build...
rem app\tools holds a 10 MB legacy wireproxy.exe that the browser never starts
rem (see app\tools\README.md), so it is left out of the package on purpose.
robocopy "%ROOT%\app" "%PAYLOAD%\resources\app" /E /XD node_modules tools /NFL /NDL /NJH /NJS /NP /R:2 /W:1 >nul
if errorlevel 8 goto :fail_copy
if not exist "%PAYLOAD%\resources\app\main.js" goto :fail_copy

copy /y "%ROOT%\LICENSE" "%PAYLOAD%\LICENSE.txt" >nul 2>nul
copy /y "%ROOT%\README.md" "%PAYLOAD%\README.txt" >nul 2>nul
copy /y "%ROOT%\VERIFICATION.md" "%PAYLOAD%\VERIFICATION.txt" >nul 2>nul

if not exist "%ROOT%\app\update-token.json" goto :token_absent
echo       Release read token: present, so the update banner can check GitHub.
findstr /c:"update-token.json" "%ROOT%\.gitignore" >nul 2>nul
if not errorlevel 1 goto :token_check_done
echo       WARNING: app\update-token.json is not in .gitignore. Never commit it.
goto :token_check_done
:token_absent
echo       Release read token: absent. Optional, only the update banner needs it.
echo       See BUILD-WINDOWS.md, section "Making the update banner work".
:token_check_done
echo.

rem ------------------------------------------------------------- 6. Verify
echo [6/8] Checking the staged payload...
for %%f in (%VERIFY%) do if not exist "%PAYLOAD%\%%f" goto :fail_verify
for %%f in (%VERIFY%) do if %%~zf EQU 0 goto :fail_verify
if exist "%PAYLOAD%\resources\default_app.asar" goto :fail_verify
echo       Payload complete: every file the browser needs is there.
echo.

rem ---------------------------------------------------------- 7. Installer
echo [7/8] Looking for NSIS to build the installer...
set "MAKENSIS="
where makensis >nul 2>nul
if not errorlevel 1 set "MAKENSIS=makensis"
if not defined MAKENSIS if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
if not defined MAKENSIS if exist "%ProgramFiles%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles%\NSIS\makensis.exe"

set "SETUP=%ROOT%\PsychoVault-Setup-%VERSION%.exe"
if not defined MAKENSIS goto :no_nsis

echo       NSIS found: %MAKENSIS%
"%MAKENSIS%" /V2 /DWANT_UNINSTALLER "/DVERSION=%VERSION%" "/DMUI_ICON=%ROOT%\assets\psychovault.ico" "/DPAYLOADDIR=%PAYLOAD%" "/DOUTFILE=%SETUP%" "%ROOT%\installer.nsi"
if errorlevel 1 goto :fail_nsis
if not exist "%SETUP%" goto :fail_nsis
echo       Installer written.
echo.
goto :notes

rem ------------------------------------------------------ 8. Release notes
:notes
echo [8/8] Writing release notes...
set "NOTES=%ROOT%\Release-Notes-%VERSION%.txt"
rem Deliberately left undefined: "if not defined" is how the first output line
rem of certutil is kept and the trailing status line is ignored. Seeding it with
rem a placeholder would defeat that and print the placeholder instead.
set "EXEHASH="
set "SETUPHASH=not built"
for /f "skip=1 delims=" %%h in ('certutil -hashfile "%PAYLOAD%\PsychoVault.exe" SHA256 2^>nul') do if not defined EXEHASH set "EXEHASH=%%h"
if defined EXEHASH goto :notes_setup_hash
set "EXEHASH=unavailable"
:notes_setup_hash
if not exist "%SETUP%" goto :notes_fields
set "SETUPHASH="
for /f "skip=1 delims=" %%h in ('certutil -hashfile "%SETUP%" SHA256 2^>nul') do if not defined SETUPHASH set "SETUPHASH=%%h"
if defined SETUPHASH goto :notes_fields
set "SETUPHASH=unavailable"
:notes_fields

> "%NOTES%" echo PsychoVault %VERSION% - release notes
>> "%NOTES%" echo Build date : %DATE% %TIME%
>> "%NOTES%" echo Source     : %ROOT%
>> "%NOTES%" echo.
>> "%NOTES%" echo WHAT IS IN THIS BUILD
>> "%NOTES%" echo   Version          : %VERSION%
>> "%NOTES%" echo   Runtime          : Electron 45.1.0, Chromium 140, Windows x64
>> "%NOTES%" echo   Portable folder  : build\payload
>> "%NOTES%" echo   Portable exe     : %PAYLOAD%\PsychoVault.exe
>> "%NOTES%" echo   SHA-256          : %EXEHASH%
>> "%NOTES%" echo   Installer        : %SETUP%
>> "%NOTES%" echo   Installer SHA-256: %SETUPHASH%
>> "%NOTES%" echo.
>> "%NOTES%" echo WHAT CHANGED
where git >nul 2>nul
if errorlevel 1 goto :notes_no_git
git -C "%ROOT%" rev-parse --git-dir >nul 2>nul
if errorlevel 1 goto :notes_no_git
for /f "delims=" %%l in ('git -C "%ROOT%" log -10 --oneline 2^>nul') do >> "%NOTES%" echo   %%l
goto :notes_from_source
:notes_no_git
>> "%NOTES%" echo   Source was a ZIP download or git is not installed, so there is no
>> "%NOTES%" echo   commit list. Re-download the ZIP to compare against.
:notes_from_source
>> "%NOTES%" echo.
>> "%NOTES%" echo HOW TO USE IT
>> "%NOTES%" echo   Portable: run build\payload\PsychoVault.exe. No installer, no admin rights.
>> "%NOTES%" echo   Installer: run PsychoVault-Setup-%VERSION%.exe, if it was built.
>> "%NOTES%" echo.
>> "%NOTES%" echo NOTES
>> "%NOTES%" echo   This build is unsigned. Windows SmartScreen will say Windows protected
>> "%NOTES%" echo   your PC the first time: choose More info, then Run anyway.
>> "%NOTES%" echo   Your profile - bookmarks, history, settings, vault.bin - lives in
>> "%NOTES%" echo   %%APPDATA%%\PsychoVault and is never touched by a rebuild.
>> "%NOTES%" echo   app\tools\wireproxy.exe is not packaged: the browser never starts it.
echo       Release notes: %NOTES%
echo.

goto :done

rem ===========================================================================
:done
echo ============================================================
echo   Build finished.
echo ============================================================
echo.
echo   Portable browser, just run it:
echo     %PAYLOAD%\PsychoVault.exe
if exist "%SETUP%" echo   Installer, with shortcuts and an uninstall entry:
if exist "%SETUP%" echo     %SETUP%
if exist "%NOTES%" echo   Release notes:
if exist "%NOTES%" echo     %NOTES%
echo.
echo   Windows SmartScreen may warn about an unsigned app. That is
echo   expected: choose "More info" then "Run anyway".
echo.
choice /c YN /n /m "Start PsychoVault now? [Y/N] "
if errorlevel 2 goto :end
start "" "%PAYLOAD%\PsychoVault.exe"
goto :end

:end
echo.
echo Done. Press any key to close.
pause >nul
exit /b 0

rem ===========================================================================
rem  Failure and help paths
rem ===========================================================================
:bad_path
echo.
echo   FAILED: the repo folder path contains a character batch files cannot
echo   handle reliably, either an opening bracket or an ampersand:
echo     %ROOT%
echo.
echo   Move or re-extract the folder somewhere simple, for example C:\PsychoVault,
echo   then run this file again.
goto :end

:no_node
echo.
echo   FAILED: Node.js was not found on this PC.
echo.
echo   Get the installer here, free, pick the Windows .msi 64-bit LTS:
echo     https://nodejs.org/en/download
echo   Or in a terminal:  winget install OpenJS.NodeJS.LTS
echo.
echo   Install it, close this window, then double-click this file again.
goto :end

:fail_version
echo.
echo   FAILED: could not read a three-part version from app\package.json
echo   It must look like 1.0.12. Found: %VERSION%
goto :end

:no_electron
echo.
echo   FAILED: npm finished but there is no Electron runtime at
echo     %ROOT%\app\node_modules\electron\dist\electron.exe
echo   Usually a proxy, antivirus, or an interrupted download.
echo   Try:  cd app  ^&^&  npm install --force
goto :end

:fail_npm
echo.
echo   FAILED: npm install in app\ failed. See the messages above.
echo   Check your internet connection, then run the file again.
goto :end

:fail_tools
echo.
echo   FAILED: npm ci in tools\ failed. It downloads a small package
echo   called resedit. Check your connection and try again.
goto :end

:fail_patch
echo.
echo   FAILED: could not set the icon and version on PsychoVault.exe,
echo   so the staged build is incomplete. The portable folder may still
echo   start - check %PAYLOAD% - but do not ship it.
goto :end

:fail_copy
echo.
echo   FAILED: copying files failed. If PsychoVault is running, close it,
echo   delete the build folder and run this file again.
goto :end

:fail_verify
echo.
echo   FAILED: the staged payload is missing files the browser needs,
echo   so the build was stopped before anything was packaged.
echo   Delete the build folder, then run this file again; if it keeps
echo   failing, check that app\ in the repo is complete.
goto :end

:no_nsis
echo.
echo   NSIS is not installed, so no installer .exe was made.
echo   That is fine - build\payload\PsychoVault.exe already runs.
echo.
echo   For a proper Setup .exe, with shortcuts and an uninstall entry,
echo   install NSIS:
echo     https://nsis.sourceforge.io/Download
echo   Or in a terminal:  winget install NSIS.NSIS
echo   Then run this file again.
echo.
goto :notes

:fail_nsis
echo.
echo   FAILED: NSIS ran but no installer was produced.
echo   The portable build at %PAYLOAD% is still fine.
goto :end
