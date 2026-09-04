@echo off
set DIR=%~dp0
set EXE_RELEASE=%DIR%src-tauri\target\release\tauri-app.exe
set EXE_DEBUG=%DIR%src-tauri\target\debug\tauri-app.exe

if exist "%EXE_RELEASE%" (
    start "" "%EXE_RELEASE%"
) else if exist "%EXE_DEBUG%" (
    start "" "%EXE_DEBUG%"
) else (
    echo No build found. Run "npm run tauri build" or "npm run tauri dev" first.
    pause
)
