@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0git-net.ps1" %*
endlocal
