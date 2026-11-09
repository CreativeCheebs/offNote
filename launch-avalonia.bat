@echo off
setlocal
set "DOTNET_ROOT=%~dp0.dotnet10"
set "EXE=%~dp0avalonia-offnote\bin\Release\net10.0\Offnote.Avalonia.exe"
if not exist "%EXE%" (
    echo No Avalonia Release build found. Run .dotnet10\dotnet.exe build avalonia-offnote\Offnote.Avalonia.csproj -c Release first.
    pause
    exit /b 1
)
start "" "%EXE%"
