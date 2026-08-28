# Installation and update guide

## Choose a package

- `AliceCoop-<version>-drop-in.zip`: extract directly into `Binaries\Win32`.
- `AliceCoop-<version>-installer.zip`: extract anywhere and run
  `Install-AliceCoop.bat`; the installer locates the game, preserves an existing
  `MadnessPatch.ini`, and backs up the previous `dinput8.dll`.

Both packages contain the same client DLL and relay server.

## Game directory

The target is the directory containing `AliceMadnessReturns.exe`, normally:

```text
Steam:  <SteamLibrary>\steamapps\common\Alice Madness Returns\Binaries\Win32
EA App: <EA folder>\Alice Madness Returns\Game\Alice2\Binaries\Win32
```

Do not install into the top-level `Alice Madness Returns` directory.

## Clean installation

1. Close all game processes.
2. Back up your save directory and any existing `dinput8.dll` or
   `MadnessPatch.ini`.
3. Extract the drop-in archive into `Binaries\Win32`, preserving its directory
   structure.
4. Confirm that the directory now contains:

   ```text
   AliceMadnessReturns.exe
   dinput8.dll
   MadnessPatch.ini
   AliceCoop\AliceCoopServer.exe
   AliceCoop\AliceCoop.ini
   AliceCoop\AliceCoop-LaunchConfig.bat
   ```

5. Install the Microsoft Visual C++ 2015–2022 Redistributable x86 if the game
   fails before opening a window.

The bundled `dinput8.dll` already includes MadnessPatch 3.1.1. Installing a
second `dinput8.dll` beside it is impossible; one would replace the other.

## Updating

Both players must update to the same Alice Co-op version and protocol version.

1. Close the game and server.
2. Keep a copy of `AliceCoop\AliceCoop-LaunchConfig.bat` and any customized
   `AliceCoop.ini`/`MadnessPatch.ini`.
3. Replace `dinput8.dll`, `AliceCoopServer.exe`, scripts, images and documentation
   with the new release files.
4. Review the changelog for newly added configuration keys.

The installer package performs backups automatically. A raw drop-in update does
not preserve files when the extraction program is told to overwrite them.

## Network setup

- Run exactly one `AliceCoopServer.exe` for a two-player session.
- The server can run on the host PC, client PC, or another trusted Windows PC.
- For Steam launch, set `EnableWithoutLauncher = 1` and the appropriate
  `Role = host` or `Role = client` in each `AliceCoop.ini`. Set `ServerAddress`
  to the relay PC and use the same `Port` on both computers, then launch through
  Steam normally.
- For BAT launch, both game processes must use the same `SERVER_IP` and
  `COOP_PORT` in `AliceCoop-LaunchConfig.bat`.
- The server binds to `0.0.0.0` by default, so localhost, LAN and VPN adapters
  are accepted.
- Allow inbound UDP on port `27018` or your configured alternative.
- Do not expose the port directly to the Internet.

## Profiles

Use a dedicated client co-op profile. The client may initially complete the
prologue normally, or use the main-menu `SYNC HOST SAVE` action (`K`). Profile
synchronization:

- shows a destructive confirmation;
- transfers the selected host progress files in acknowledged chunks;
- verifies SHA-256 before applying anything;
- backs up the client's previous files with an `.alicecoop-presync.bak` suffix;
- requires restarting the client after success.

Do not close either game or relay server during the transfer.

## Uninstall

Run `AliceCoop\Uninstall-AliceCoop.bat`. If the installer backed up a previous
`dinput8.dll`, it offers to restore it. Your retail game data is not removed.
