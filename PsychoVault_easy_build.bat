@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Psycho_start-build.ps1"
if errorlevel 1 (
  echo.
  echo I could not start the GitHub-hosted build. See the message above.
  pause
  exit /b 1
)
endlocal
