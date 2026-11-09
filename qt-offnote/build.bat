@echo off
setlocal
set "PATH=C:\msys64\mingw64\bin;%PATH%"
cmake -S "%~dp0." -B "%~dp0build" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/msys64/mingw64
if errorlevel 1 exit /b %ERRORLEVEL%
cmake --build "%~dp0build" --config Release
exit /b %ERRORLEVEL%
