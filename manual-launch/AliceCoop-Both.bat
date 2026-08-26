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

set "ALICECOOP_ROLE=host"
set "ALICECOOP_WINDOW_X=0"
echo Starting HOST on the left at %WINDOW_WIDTH%x%WINDOW_HEIGHT%...
start "AliceCoop HOST" /D "%GAME_DIR%" "%GAME_EXE%" -windowed

echo.
echo Wait until HOST has fully loaded into gameplay.
echo Then press any key to start CLIENT on the right.
pause >nul

set "ALICECOOP_ROLE=client"
set "ALICECOOP_WINDOW_X=%CLIENT_X%"
echo Starting CLIENT on the right at X=%CLIENT_X% of %PHYSICAL_SCREEN_WIDTH% physical pixels...
start "AliceCoop CLIENT" /D "%GAME_DIR%" "%GAME_EXE%" -windowed
