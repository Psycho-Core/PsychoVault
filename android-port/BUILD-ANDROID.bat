@echo off
setlocal
cd /d "%~dp0"
set "NOTES=%~dp0Release-Notes-Android.txt"
>"%NOTES%" echo PsychoVault Android experimental build
>>"%NOTES%" echo Status: started. Proton VPN and extensions are not implemented.
where node >nul 2>&1
if errorlevel 1 goto no_node
where npm >nul 2>&1
if errorlevel 1 goto no_npm
where java >nul 2>&1
if errorlevel 1 goto no_java
call npm ci
if errorlevel 1 goto npm_fail
call npm run sync
if errorlevel 1 goto sync_fail
call npm test
if errorlevel 1 goto test_fail
cd /d "%~dp0android"
call gradlew.bat assembleDebug
if errorlevel 1 goto gradle_fail
>>"%NOTES%" echo Status: debug APK built. It is an experimental preview, not a release.
>>"%NOTES%" echo APK: android\app\build\outputs\apk\debug\app-debug.apk
 echo Build finished. Read Release-Notes-Android.txt before testing.
pause
exit /b 0
:no_node
>>"%NOTES%" echo Failed: Node.js was not found.
goto failed
:no_npm
>>"%NOTES%" echo Failed: npm was not found.
goto failed
:no_java
>>"%NOTES%" echo Failed: Java was not found. Install the Android build prerequisites.
goto failed
:npm_fail
>>"%NOTES%" echo Failed: npm ci failed. Read the first error above.
goto failed
:sync_fail
>>"%NOTES%" echo Failed: Capacitor sync failed. Read the first error above.
goto failed
:test_fail
>>"%NOTES%" echo Failed: tests failed. Read the first error above.
goto failed
:gradle_fail
>>"%NOTES%" echo Failed: Android Gradle build failed. Read the first error above.
:failed
echo Build failed. Read the first error above and Release-Notes-Android.txt.
pause
exit /b 1
