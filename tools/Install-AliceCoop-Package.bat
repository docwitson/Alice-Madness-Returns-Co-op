@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "INSTALL_SCRIPT=%SCRIPT_DIR%tools\Install-AliceCoop-Package.ps1"
if not exist "%INSTALL_SCRIPT%" set "INSTALL_SCRIPT=%SCRIPT_DIR%Install-AliceCoop-Package.ps1"
if "%~1"=="" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%INSTALL_SCRIPT%"
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%INSTALL_SCRIPT%" -GameRoot "%~1"
)

if errorlevel 1 (
    echo.
    echo AliceCoop installation failed.
) else (
    echo.
    echo AliceCoop installation completed.
)
pause
