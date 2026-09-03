# Launcher installation and update guide

## Choose a package

- `AliceCoop-<version>-installer.zip` is the recommended package. Extract it
  anywhere and run `AliceCoopLauncher.exe`. The archive root contains only the
  launcher, relay server and an `Advanced` directory with the payload and
  documentation.
- `AliceCoop-<version>-drop-in.zip` is the legacy/manual alternative for
  advanced users and development. Follow the separate
  [manual installation guide](MANUAL_INSTALL.md).

Both packages contain identical client, relay and launcher binaries. Use the
same Alice Co-op release on both computers.

## Recommended installation

1. Close the game and any running Alice Co-op relay.
2. Back up your saves. A separate co-op profile is strongly recommended.
3. Extract the installer archive into a normal writable folder and keep that
   folder: the launcher and relay server run from it, not from the game folder.
4. Run `AliceCoopLauncher.exe`. It searches Steam libraries automatically; use
   **Add game** to register another installed copy and select the active one.
5. Verify that the selected directory contains `AliceMadnessReturns.exe`, then
   choose **Install** and approve the Windows prompt.
6. The same window can now start a host or join a session. Keep it open while
   the game is running so Steam relaunches retain the selected role.

The target directory is normally:

```text
Steam:  <SteamLibrary>\steamapps\common\Alice Madness Returns\Binaries\Win32
EA App: <EA folder>\Alice Madness Returns\Game\Alice2\Binaries\Win32
```

The installer preserves an existing `AliceCoop.ini` and `MadnessPatch.ini`,
backs up a previous `dinput8.dll`, and records installed hashes for uninstall.
Its status log is `%LOCALAPPDATA%\AliceCoop\installer-status.txt`.
Only the runtime payload is copied into the game: the combined `dinput8.dll`,
configuration, overlay images and recovery metadata. The launcher, relay,
manual tools, source notices and documentation remain in the extracted package.

## Starting a session

### Host

1. Select a LAN or trusted VPN address to share with the other player.
2. Keep the default UDP port `27018` unless both players intentionally change it.
3. Use **Allow through Windows Firewall** once, or allow `AliceCoopServer.exe` for
   private networks when Windows asks.
4. Choose **Host Game**. The launcher starts one relay and launches the game.
5. Use **Copy address** and send the resulting endpoint to the other player.

### Client

1. Enter the host's LAN/VPN IPv4 address and the same port.
2. Choose **Test connection**. If it fails, check the address, VPN, host relay
   and host firewall before continuing.
3. Choose **Join Game** to launch the game as the client.

When connected, the host launcher log shows both `HOST connected` and
`CLIENT connected`. Load compatible profiles. The client can use
`SYNC HOST SAVE` in the main menu after reading its destructive warning.

The launcher starts a Steam installation through Steam only when the selected
game directory matches that installation's app manifest. EA App, copied and
development installations are started directly. Its short-lived session file
is accepted by the mod only while the originating launcher remains open and
only by the selected game copy. Local two-copy testing generally requires two
launcher windows and a separate non-Steam game installation; Steam normally
cannot launch two copies of the same app at once.

Do not expose the relay directly to the public Internet. The prototype protocol
does not provide authentication or encryption; use localhost, trusted LAN or a
trusted VPN.

## Manual/legacy alternative

The drag-and-drop archive and BAT/INI workflow are intentionally documented
separately so they do not complicate the normal launcher setup. See
[Manual drop-in installation](MANUAL_INSTALL.md).

## Updating

For the installer package, close both the game and relay, start the new
launcher and choose **Repair** for the same game directory. For a
drop-in update, replace the packaged files while preserving your customized
`AliceCoop.ini` and `MadnessPatch.ini`. Always update both players to the same
release and protocol version.

## Profiles

Use a dedicated client co-op profile. Profile synchronization:

- displays a destructive confirmation;
- transfers selected host progress files in acknowledged chunks;
- verifies SHA-256 before applying anything;
- backs up the client's previous files with an `.alicecoop-presync.bak` suffix;
- requires restarting the client after success.

Do not close either game or the relay during the transfer.

## Uninstall

Use **Remove** beside the selected installation in the launcher, or run
`Advanced\Tools\Uninstall-AliceCoop.bat` from the extracted installer package.
For a drop-in installation, the script remains under `AliceCoop\Advanced\Tools`.
If the installer backed up a previous `dinput8.dll`, it restores it. Retail game
files, logs and client save backups are not removed.
