@echo off
rem AliceCoop shared launch settings.
rem Edit this file once; all normal server/host/client scripts read it.

rem Address of the PC running AliceCoopServer.exe.
rem Use 127.0.0.1 when the server is on this PC, or a Radmin VPN IP.
set "SERVER_IP=127.0.0.1"
set "SERVER_BIND_IP=0.0.0.0"
set "COOP_PORT=27018"

rem Normal HOST/CLIENT display mode: fullscreen, windowed, or borderless.
set "DISPLAY_MODE=fullscreen"

rem Used by windowed mode. Borderless uses these values when both are above 0;
rem set either to 0 to use the current monitor bounds automatically.
set "WINDOW_WIDTH=1920"
set "WINDOW_HEIGHT=1080"
set "WINDOW_X=0"
set "WINDOW_Y=0"

rem Local two-window test layout used by AliceCoop-Both.bat.
set "TEST_WINDOW_WIDTH=1440"
set "TEST_WINDOW_HEIGHT=900"
set "TEST_WINDOW_Y=0"

rem Leave this enabled for local two-window testing only.
set "TEST_BACKGROUND_DAMAGE_GUARD=1"
