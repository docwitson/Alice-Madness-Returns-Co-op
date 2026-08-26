@echo off
setlocal

for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI"
if not exist "%SCRIPT_DIR%\AliceCoop-LaunchConfig.bat" (
    echo ERROR: AliceCoop-LaunchConfig.bat not found in:
    echo %SCRIPT_DIR%
    pause
    exit /b 1
)
call "%SCRIPT_DIR%\AliceCoop-LaunchConfig.bat"
set "BIND_IP=%SERVER_BIND_IP%"
if not "%~1"=="" set "BIND_IP=%~1"
if not "%~2"=="" set "COOP_PORT=%~2"

set "COOP_DIR=%SCRIPT_DIR%"
if not exist "%COOP_DIR%\AliceCoopServer.exe" (
    for %%I in ("%SCRIPT_DIR%\..\..\Binaries\Win32\AliceCoop") do set "COOP_DIR=%%~fI"
)
set "SERVER_EXE=%COOP_DIR%\AliceCoopServer.exe"
if not exist "%SERVER_EXE%" (
    echo ERROR: AliceCoopServer.exe not found.
    echo Checked: %SERVER_EXE%
    pause
    exit /b 1
)

cd /d "%COOP_DIR%"
echo AliceCoop server
echo Bind: %BIND_IP%:%COOP_PORT%
echo Close this window or press Ctrl+C to stop the server.
echo.

"%SERVER_EXE%" --bind "%BIND_IP%" --port "%COOP_PORT%" --log-dir "%COOP_DIR%\logs"

echo.
echo Server stopped with exit code %ERRORLEVEL%.
pause
