@echo off
cd /d "%~dp0"
set "BIN=%~dp0out\build\x64-Debug"

echo === Pract2Service ===
sc.exe query Pract2Service
echo.
echo === Processes ===
tasklist /FI "IMAGENAME eq Pract2Service.exe"
tasklist /FI "IMAGENAME eq TrayWin32App.exe"
echo.
echo === Build output ===
dir /B "%BIN%\*.exe" 2>nul
echo.
echo === launch.log ===
if exist "%ProgramData%\Pract2Service\launch.log" type "%ProgramData%\Pract2Service\launch.log"
echo.
echo === tray.log ===
if exist "%ProgramData%\Pract2Service\tray.log" type "%ProgramData%\Pract2Service\tray.log"
pause
