@echo off
setlocal
pushd "%~dp0android-offnote" || (
    echo Could not find the android-offnote directory beside this launcher.
    pause
    exit /b 1
)

call ".\gradlew.bat" :desktopApp:run --console=plain
set "OFFNOTE_EXIT=%ERRORLEVEL%"
popd

if not "%OFFNOTE_EXIT%"=="0" (
    echo.
    echo Offnote Compose desktop stopped with an error. Check the message above.
    pause
)
exit /b %OFFNOTE_EXIT%
