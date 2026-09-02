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

set "ALICECOOP_ENABLE=1"
set "ALICECOOP_ROLE=host"
set "ALICECOOP_SERVER=%SERVER_IP%"
set "ALICECOOP_PORT=%COOP_PORT%"
set "ALICECOOP_FORCE_WINDOWED=0"
set "ALICECOOP_MANAGE_WINDOW=0"
set "ALICECOOP_BORDERLESS=0"
set "GAME_ARGUMENTS=-fullscreen"

if /i "%DISPLAY_MODE%"=="windowed" (
    set "ALICECOOP_FORCE_WINDOWED=1"
    set "ALICECOOP_MANAGE_WINDOW=1"
    set "ALICECOOP_WINDOW_X=%WINDOW_X%"
    set "ALICECOOP_WINDOW_Y=%WINDOW_Y%"
    set "ALICECOOP_WINDOW_WIDTH=%WINDOW_WIDTH%"
    set "ALICECOOP_WINDOW_HEIGHT=%WINDOW_HEIGHT%"
    set "GAME_ARGUMENTS=-windowed -ResX=%WINDOW_WIDTH% -ResY=%WINDOW_HEIGHT%"
)
if /i "%DISPLAY_MODE%"=="borderless" (
    set "ALICECOOP_FORCE_WINDOWED=1"
    set "ALICECOOP_MANAGE_WINDOW=1"
    set "ALICECOOP_BORDERLESS=1"
    set "ALICECOOP_WINDOW_X=%WINDOW_X%"
    set "ALICECOOP_WINDOW_Y=%WINDOW_Y%"
    set "ALICECOOP_WINDOW_WIDTH=%WINDOW_WIDTH%"
    set "ALICECOOP_WINDOW_HEIGHT=%WINDOW_HEIGHT%"
    set "GAME_ARGUMENTS=-windowed"
    if not "%WINDOW_WIDTH%"=="0" if not "%WINDOW_HEIGHT%"=="0" set "GAME_ARGUMENTS=-windowed -ResX=%WINDOW_WIDTH% -ResY=%WINDOW_HEIGHT%"
)

for %%I in ("%SCRIPT_DIR%\..\..") do set "INSTALLED_COOP_DIR=%%~fI"
if exist "%INSTALLED_COOP_DIR%\AliceCoop.ini" (
    for %%I in ("%INSTALLED_COOP_DIR%\..") do set "GAME_DIR=%%~fI"
) else (
    for %%I in ("%SCRIPT_DIR%\..") do set "GAME_DIR=%%~fI"
    if not exist "%GAME_DIR%\AliceMadnessReturns.exe" (
        for %%I in ("%SCRIPT_DIR%\..\..\Binaries\Win32") do set "GAME_DIR=%%~fI"
    )
)
set "GAME_EXE=%GAME_DIR%\AliceMadnessReturns.exe"
if not exist "%GAME_EXE%" (
    echo ERROR: Game executable not found:
    echo %GAME_EXE%
    pause
    exit /b 1
)

echo Starting AliceCoop HOST via %SERVER_IP%:%COOP_PORT%
echo Display mode: %DISPLAY_MODE%
start "AliceCoop HOST" /D "%GAME_DIR%" "%GAME_EXE%" %GAME_ARGUMENTS%
