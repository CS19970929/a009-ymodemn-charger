@echo off
setlocal

set "CXX="
if exist "C:\qp\qtools\MinGW32\bin\g++.exe" set "CXX=C:\qp\qtools\MinGW32\bin\g++.exe"
if "%CXX%"=="" set "CXX=g++"

if not exist build mkdir build
if not exist release mkdir release

set "COMMON_FLAGS=-std=gnu++98 -Os -Wall -Wextra -D_WIN32_WINNT=0x0501 -DUNICODE -D_UNICODE -finput-charset=UTF-8 -fexec-charset=UTF-8"
set "LINK_FLAGS=-mwindows -static -static-libgcc -static-libstdc++ -lcomctl32 -lcomdlg32"

"%CXX%" %COMMON_FLAGS% src\main.cpp src\ymodem.cpp -o build\ChargerUpdater_debug.exe %LINK_FLAGS%
if errorlevel 1 exit /b 1

"%CXX%" %COMMON_FLAGS% -DSIMPLE_UI src\main.cpp src\ymodem.cpp -o build\ChargerUpdater_user.exe %LINK_FLAGS%
if errorlevel 1 exit /b 1

copy /Y build\ChargerUpdater_debug.exe release\ChargerUpdater_debug.exe >nul 2>nul
if errorlevel 1 exit /b 1

copy /Y build\ChargerUpdater_user.exe release\ChargerUpdater_user.exe >nul 2>nul
if errorlevel 1 exit /b 1

echo release\ChargerUpdater_debug.exe
echo release\ChargerUpdater_user.exe
