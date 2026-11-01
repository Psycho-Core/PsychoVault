@echo off
setlocal
set "NOTES=%~dp0build-notes"
if not exist "%NOTES%" mkdir "%NOTES%"
(
 echo PsychoVault Android build notes
 echo Status: not built
 echo The Android port is being restarted on Chromium. The PsychoVault UI and build are not ready.
 echo No APK was created. No signing or upload was attempted.
 echo See android-port\README.md for the current state.
) > "%NOTES%\Release-Notes-Android.txt"
echo The new Android build is not ready. No APK was created.
echo See "%NOTES%\Release-Notes-Android.txt" and "%~dp0README.md".
exit /b 1
