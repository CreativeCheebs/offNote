@echo off
setlocal
set "PATH=C:\msys64\mingw64\bin;%PATH%"
for /f "delims=" %%I in ('C:\msys64\mingw64\bin\pkg-config.exe --cflags gtk+-3.0 sqlite3') do set "CFLAGS=%%I"
for /f "delims=" %%I in ('C:\msys64\mingw64\bin\pkg-config.exe --libs gtk+-3.0 sqlite3') do set "LIBS=%%I"
C:\msys64\mingw64\bin\gcc.exe -O2 -Wno-deprecated-declarations -mwindows -o "%~dp0OffnoteGtk.exe" "%~dp0main.c" %CFLAGS% %LIBS%
exit /b %ERRORLEVEL%
