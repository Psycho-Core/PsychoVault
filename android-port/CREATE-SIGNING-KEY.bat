@echo off
setlocal DisableDelayedExpansion
set "KEYDIR=%USERPROFILE%\PsychoVault-Android-Keys"
set "KEYFILE=%KEYDIR%\psychovault-android.p12"
where keytool >nul 2>&1
if errorlevel 1 goto no_keytool
if exist "%KEYFILE%" goto already_exists
if not exist "%KEYDIR%" mkdir "%KEYDIR%"
if errorlevel 1 goto failed
echo This creates a private Android signing key in your user profile.
echo Keep an offline backup of the key and its password. Do not share them.
echo The keytool will ask for a password in this window. Do not put it in chat.
keytool -genkeypair -v -keystore "%KEYFILE%" -storetype PKCS12 -alias psychovault -keyalg RSA -keysize 4096 -validity 10000 -dname "CN=PsychoVault Android Preview"
if errorlevel 1 goto failed
echo Key created: "%KEYFILE%"
echo Back it up securely before using it. Read SIGNING.md for the next steps.
pause
exit /b 0
:already_exists
echo The key already exists. It will not be overwritten: "%KEYFILE%"
pause
exit /b 1
:no_keytool
echo keytool was not found. Install JDK 21 and add it to PATH.
goto failed
:failed
echo Key creation failed. Read the first error above.
pause
exit /b 1
