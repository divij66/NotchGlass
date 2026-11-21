@echo off
setlocal
cd /d "%~dp0"
title NotchGlass C++20 Installer

echo ==================================================
echo   NotchGlass - Windows 10/11 App Installer
echo ==================================================
echo.

if not exist "%~dp0scripts\install_windows.ps1" (
    echo [ERROR] Please EXTRACT the ZIP file first!
    echo Right-click notchglass-cpp20.zip and choose "Extract All...".
    echo.
    pause
    exit /b 1
)

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install_windows.ps1"
set "PS_EXIT=%ERRORLEVEL%"

echo.
if %PS_EXIT% neq 0 (
    echo [ERROR] Installation failed with exit code %PS_EXIT%.
    echo Check "%~dp0install_log.txt" for details.
    echo If your antivirus blocked PowerShell, allow "%~dp0" and retry.
) else (
    echo [OK] Setup finished. You can close this window.
)
echo.
pause
exit /b %PS_EXIT%
