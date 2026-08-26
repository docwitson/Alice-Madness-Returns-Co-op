@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall-AliceCoop.ps1"
if errorlevel 1 (
    echo.
    echo AliceCoop uninstall failed.
) else (
    echo.
    echo AliceCoop was removed. Any previous dinput8.dll was restored when available.
)
pause
