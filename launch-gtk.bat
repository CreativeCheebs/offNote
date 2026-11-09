@echo off
setlocal
set "PATH=C:\msys64\mingw64\bin;%PATH%"
set "XDG_DATA_DIRS=C:/msys64/mingw64/share"
if not exist "%~dp0gtk-offnote\OffnoteGtk.exe" call "%~dp0gtk-offnote\build.bat"
if errorlevel 1 exit /b %ERRORLEVEL%
start "" "%~dp0gtk-offnote\OffnoteGtk.exe"
