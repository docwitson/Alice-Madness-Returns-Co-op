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

if not "%~1"=="" set "SERVER_IP=%~1"
if not "%~2"=="" set "COOP_PORT=%~2"
set "WINDOW_WIDTH=%TEST_WINDOW_WIDTH%"
set "WINDOW_HEIGHT=%TEST_WINDOW_HEIGHT%"

for %%I in ("%SCRIPT_DIR%\..") do set "GAME_DIR=%%~fI"
if not exist "%GAME_DIR%\AliceMadnessReturns.exe" (
    for %%I in ("%SCRIPT_DIR%\..\..\Binaries\Win32") do set "GAME_DIR=%%~fI"
)
set "GAME_EXE=%GAME_DIR%\AliceMadnessReturns.exe"
if not exist "%GAME_EXE%" (
    echo ERROR: Game executable not found:
    echo %GAME_EXE%
    pause
    exit /b 1
)

set "CLIENT_X=0"
set "PHYSICAL_SCREEN_WIDTH=0"
for /f %%I in ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%\Get-PhysicalScreenWidth.ps1"') do set "PHYSICAL_SCREEN_WIDTH=%%I"
if %PHYSICAL_SCREEN_WIDTH% GTR %WINDOW_WIDTH% (
    set /a CLIENT_X=PHYSICAL_SCREEN_WIDTH-WINDOW_WIDTH
)

set "ALICECOOP_ENABLE=1"
set "ALICECOOP_SERVER=%SERVER_IP%"
set "ALICECOOP_PORT=%COOP_PORT%"
set "ALICECOOP_FORCE_WINDOWED=1"
set "ALICECOOP_MANAGE_WINDOW=1"
set "ALICECOOP_BORDERLESS=0"
set "ALICECOOP_BACKGROUND_GUARD=%TEST_BACKGROUND_DAMAGE_GUARD%"
set "ALICECOOP_WINDOW_Y=%TEST_WINDOW_Y%"
set "ALICECOOP_WINDOW_WIDTH=%WINDOW_WIDTH%"
set "ALICECOOP_WINDOW_HEIGHT=%WINDOW_HEIGHT%"
set "ALICECOOP_VFX_TRACE=1"
set "ALICECOOP_ANIMATION_TRACE=1"
set "ALICECOOP_CONTROL_TRACE=1"
set "ALICECOOP_INVARIANT_TRACE=1"
set "ALICECOOP_PROCESS_EVENT_BRIDGE_TRACE=1"
set "ALICECOOP_ACTION_TRACE=0"
set "ALICECOOP_WORLD_TRACE=0"

echo AliceCoop focused diagnostic run via %SERVER_IP%:%COOP_PORT%
echo VFX, animation, control, lifecycle and ProcessEvent bridge diagnostics are enabled.
echo Start AliceCoop-Server.bat first if the relay is not already running.
echo.

set "ALICECOOP_ROLE=host"
set "ALICECOOP_WINDOW_X=0"
echo Starting diagnostic HOST...
start "AliceCoop Diagnostic HOST" /D "%GAME_DIR%" "%GAME_EXE%" -windowed

echo.
echo Wait until HOST has fully loaded into gameplay.
echo Then press any key to start CLIENT in the same relay session.
pause >nul

set "ALICECOOP_ROLE=client"
set "ALICECOOP_WINDOW_X=%CLIENT_X%"
echo Starting diagnostic CLIENT...
start "AliceCoop Diagnostic CLIENT" /D "%GAME_DIR%" "%GAME_EXE%" -windowed
