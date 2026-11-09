@echo off
setlocal
set "APP_DIR=%~dp0winforms-quicknote"
set "EXE_RELEASE=%APP_DIR%\bin\Release\net7.0-windows\QuickNote.exe"
set "EXE_DEBUG=%APP_DIR%\bin\Debug\net7.0-windows\QuickNote.exe"

if exist "%EXE_RELEASE%" (
    start "" "%EXE_RELEASE%"
) else if exist "%EXE_DEBUG%" (
    start "" "%EXE_DEBUG%"
) else (
    echo No WinForms build found. Run "dotnet build winforms-quicknote\QuickNoteWinForms.csproj" first.
    pause
    exit /b 1
)
