# Manual drop-in installation

This is the advanced/legacy installation path. New users should use the
[graphical launcher installer](INSTALL.md) instead.

## Install

1. Close the game and any Alice Co-op relay process.
2. Back up saves and any existing `dinput8.dll`, `MadnessPatch.ini` and
   `AliceCoop.ini` files.
3. Download `AliceCoop-<version>-drop-in.zip` and extract its contents directly
   into the directory containing `AliceMadnessReturns.exe`.
4. Allow files to merge when prompted. Preserve customized INI files when
   updating an existing installation.

Typical game directories:

```text
Steam:  <SteamLibrary>\steamapps\common\Alice Madness Returns\Binaries\Win32
EA App: <EA folder>\Alice Madness Returns\Game\Alice2\Binaries\Win32
```

The resulting layout is:

```text
AliceMadnessReturns.exe
dinput8.dll
MadnessPatch.ini
AliceCoop\AliceCoopLauncher.exe
AliceCoop\AliceCoopServer.exe
AliceCoop\AliceCoop.ini
AliceCoop\images\...
AliceCoop\Advanced\Manual\...
```

The combined `dinput8.dll` already contains MadnessPatch 3.1.1. Do not replace
it with another proxy DLL.

## Start with the packaged launcher

Run `AliceCoop\AliceCoopLauncher.exe`, select the current game directory, then
use **Host Game** or **Join Game** normally. Because this launcher lives inside
the game folder, keep that folder intact while playing.

## Legacy BAT/INI launch

Manual scripts are under `AliceCoop\Advanced\Manual`:

1. Edit `AliceCoop-LaunchConfig.bat` and set the relay address, port and display
   options.
2. On the relay PC, start `AliceCoop-Server.bat`.
3. Start `AliceCoop-Host.bat` for the host and `AliceCoop-Client.bat` for the
   client. `AliceCoop-Both.bat` is intended only for local testing.
4. If Steam restarts the game and loses launcher environment variables, set
   `EnableWithoutLauncher = 1`, `Role`, `ServerAddress` and `ServerPort` in each
   installation's `AliceCoop\AliceCoop.ini`, then launch through Steam.

Use a trusted LAN or VPN and allow `AliceCoopServer.exe` through the private
Windows Firewall profile. Do not expose the unauthenticated relay directly to
the public Internet.

## Update or remove

For an update, close the game and relay, replace packaged files, and preserve
customized `AliceCoop.ini` and `MadnessPatch.ini`. Both players must use the
same release.

To remove the mod, run
`AliceCoop\Advanced\Tools\Uninstall-AliceCoop.bat`. If this installation was
created purely by drag-and-drop and has no install manifest, verify any existing
MadnessPatch backup before removing `dinput8.dll` manually.
