@echo off
rem ===========================================================================
rem  PsychoVault easy build  -  double-click me on Windows.
rem
rem  What it does, in order:
rem    1. checks Node.js is installed
rem    2. npm install in app\      (downloads the Electron 45 runtime, ~120 MB)
rem    3. copies that runtime to build\payload and rebrands electron.exe
rem    4. npm ci in tools\        (downloads resedit, used to set icon/version)
rem    5. copies the app source into build\payload\resources\app
rem    6. if NSIS is installed, builds PsychoVault-Setup-<version>.exe
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
set "FAILED="

echo ============================================================
echo   PsychoVault easy build
echo   Repo   : %ROOT%
echo   Output : %BUILD%
echo ============================================================
echo.

rem ---------------------------------------------------------------- 1. Node.js
echo [1/6] Checking for Node.js...
where node >nul 2>nul
if errorlevel 1 goto :no_node
for /f "delims=" %%v in ('node -p "process.versions.node"') do set "NODEV=%%v"
echo       Node.js v!NODEV! found.

pushd "%ROOT%\app"
set "VERSION=unknown"
for /f "delims=" %%v in ('node -p "require('./package.json').version"') do set "VERSION=%%v"
popd
echo       PsychoVault version: %VERSION%
echo.

rem ------------------------------------------------------- 2. Electron runtime
echo [2/6] Installing the Electron runtime (first run downloads ~120 MB)...
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
echo [3/6] Staging the runtime into build\payload...
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
echo [4/6] Setting the PsychoVault icon and version number...
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
echo [5/6] Copying the browser source into the build...
robocopy "%ROOT%\app" "%PAYLOAD%\resources\app" /E /XD node_modules /NFL /NDL /NJH /NJS /NP /R:2 /W:1 >nul
if errorlevel 8 goto :fail_copy
if not exist "%PAYLOAD%\resources\app\main.js" goto :fail_copy

copy /y "%ROOT%\LICENSE" "%PAYLOAD%\LICENSE.txt" >nul 2>nul
copy /y "%ROOT%\README.md" "%PAYLOAD%\README.txt" >nul 2>nul
copy /y "%ROOT%\VERIFICATION.md" "%PAYLOAD%\VERIFICATION.txt" >nul 2>nul

if exist "%ROOT%\app\update-token.json" (
  echo       Release read token: PRESENT - the update banner can check GitHub.
) else (
  echo       Release read token: absent - optional, only needed for the update
  echo       banner. See BUILD-WINDOWS.md, "Making the update banner work".
)
echo.

rem ---------------------------------------------------------- 6. Installer
echo [6/6] Looking for NSIS to build the installer...
set "MAKENSIS="
where makensis >nul 2>nul
if not errorlevel 1 set "MAKENSIS=makensis"
if not defined MAKENSIS if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
if not defined MAKENSIS if exist "%ProgramFiles%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles%\NSIS\makensis.exe"

if not defined MAKENSIS goto :no_nsis

echo       NSIS found: %MAKENSIS%
"%MAKENSIS%" /V2 /DWANT_UNINSTALLER "/DVERSION=%VERSION%" "/DMUI_ICON=%ROOT%\assets\psychovault.ico" "/DPAYLOADDIR=%PAYLOAD%" "/DOUTFILE=%ROOT%\PsychoVault-Setup-%VERSION%.exe" "%ROOT%\installer.nsi"
if errorlevel 1 goto :fail_nsis
if not exist "%ROOT%\PsychoVault-Setup-%VERSION%.exe" goto :fail_nsis
echo       Installer written.
echo.

goto :done

rem ===========================================================================
:done
echo ============================================================
echo   Build finished.
echo ============================================================
echo.
echo   Portable browser (just run it):
echo     %PAYLOAD%\PsychoVault.exe
if exist "%ROOT%\PsychoVault-Setup-%VERSION%.exe" echo   Installer, with shortcuts and an uninstall entry:
if exist "%ROOT%\PsychoVault-Setup-%VERSION%.exe" echo     %ROOT%\PsychoVault-Setup-%VERSION%.exe
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
:no_node
echo.
echo   FAILED: Node.js was not found on this PC.
echo.
echo   Get the installer here (free, pick the Windows .msi, 64-bit LTS):
echo     https://nodejs.org/en/download
echo   Or in a terminal:  winget install OpenJS.NodeJS.LTS
echo.
echo   Install it, close this window, then double-click this file again.
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
echo   FAILED: could not set the icon and version on PsychoVault.exe.
echo   The portable folder may still be usable - check %PAYLOAD%.
goto :end

:fail_copy
echo.
echo   FAILED: copying files failed. If PsychoVault is running, close it,
echo   delete the build folder and run this file again.
goto :end

:no_nsis
echo.
echo   NSIS is not installed, so no installer .exe was made.
echo   That is fine - build\payload\PsychoVault.exe already runs.
echo.
echo   For a proper Setup .exe (shortcuts, uninstall entry) install NSIS:
echo     https://nsis.sourceforge.io/Download
echo   Or in a terminal:  winget install NSIS.NSIS
echo   Then run this file again.
echo.
echo ============================================================
echo   Build finished (portable only).
echo ============================================================
echo.
echo   Run it:
echo     %PAYLOAD%\PsychoVault.exe
echo.
echo   Windows SmartScreen may warn about an unsigned app. That is
echo   expected: choose "More info" then "Run anyway".
echo.
choice /c YN /n /m "Start PsychoVault now? [Y/N] "
if errorlevel 2 goto :end
start "" "%PAYLOAD%\PsychoVault.exe"
goto :end

:fail_nsis
echo.
echo   FAILED: NSIS ran but the installer was not produced.
echo   The portable build at %PAYLOAD% is still fine.
goto :end
