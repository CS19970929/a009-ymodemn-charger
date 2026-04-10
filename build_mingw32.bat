@echo off
setlocal

set "CXX="
if exist "C:\qp\qtools\MinGW32\bin\g++.exe" set "CXX=C:\qp\qtools\MinGW32\bin\g++.exe"
if "%CXX%"=="" set "CXX=g++"

if not exist build mkdir build
if not exist release mkdir release

"%CXX%" -std=gnu++98 -Os -Wall -Wextra -D_WIN32_WINNT=0x0501 -DUNICODE -D_UNICODE -finput-charset=UTF-8 -fexec-charset=UTF-8 src\main.cpp src\ymodem.cpp -o build\ChargerUpdater.exe -mwindows -static -static-libgcc -static-libstdc++ -lcomctl32 -lcomdlg32
if errorlevel 1 exit /b 1

copy /Y build\ChargerUpdater.exe release\ChargerUpdater.exe >nul 2>nul
if errorlevel 1 (
    copy /Y build\ChargerUpdater.exe release\ChargerUpdater_new.exe >nul 2>nul
    if errorlevel 1 exit /b 1
    echo release\ChargerUpdater_new.exe
    exit /b 0
)

echo release\ChargerUpdater.exe
