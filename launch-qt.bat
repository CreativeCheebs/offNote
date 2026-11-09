@echo off
setlocal
set "PATH=C:\msys64\mingw64\bin;%PATH%"
if not exist "C:\msys64\mingw64\bin\g++.exe" (
  echo MSYS2 MinGW not found. Install MSYS2 and: pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-qt6-base mingw-w64-x86_64-yaml-cpp mingw-w64-x86_64-sqlite3
  exit /b 1
)
rem Incremental build every time so the exe always matches the sources. If a copy of
rem OffnoteQt is already running, quit it first or the link step cannot replace the exe.
call "%~dp0qt-offnote\build.bat"
if errorlevel 1 exit /b %ERRORLEVEL%
start "" "%~dp0qt-offnote\build\OffnoteQt.exe"
