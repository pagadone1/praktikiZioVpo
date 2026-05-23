@echo off
chcp 65001 >nul
title Pract2Service - install and start

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Need Administrator - click Yes in the next window...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

cd /d "%~dp0"
set "BIN=%~dp0out\build\x64-Debug"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo.
echo [1/6] Stop running apps (required before build)...
sc.exe stop Pract2Service >nul 2>&1
taskkill /F /IM TrayWin32App.exe >nul 2>&1
taskkill /F /IM Pract2Service.exe >nul 2>&1
timeout /t 2 /nobreak >nul

echo.
echo [2/6] Build...
if not exist "%VCVARS%" (
    echo ERROR: Visual Studio 2022 not found.
    pause
    exit /b 1
)
call "%VCVARS%" >nul
cmake -S . -B "%BIN%" -G Ninja -DCMAKE_BUILD_TYPE=Debug >nul
cmake --build "%BIN%" --config Debug
if errorlevel 1 (
    echo.
    echo BUILD FAILED - close TrayWin32App.exe if it is still running, then run GO.cmd again.
    pause
    exit /b 1
)

echo.
echo [3/6] Install service...
"%BIN%\Pract2Service.exe" --install
if errorlevel 1 (
    echo INSTALL FAILED - run as Administrator
    pause
    exit /b 1
)

echo.
echo [4/6] Start service...
sc.exe start Pract2Service
if errorlevel 1 (
    echo Service may already be running - continuing...
)
timeout /t 4 /nobreak >nul

echo.
echo [5/6] Check...
sc.exe query Pract2Service
echo.
tasklist /FI "IMAGENAME eq TrayWin32App.exe"
echo.
if exist "%ProgramData%\Pract2Service\tray.log" (
    echo --- tray.log (last lines) ---
    powershell -NoProfile -Command "Get-Content -Path $env:ProgramData\Pract2Service\tray.log -Tail 5"
)

echo.
echo [6/6] Done. Tray icon: click ^ arrow near the clock
echo.
pause
