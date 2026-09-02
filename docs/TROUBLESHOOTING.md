# Troubleshooting

## The game does not start

- Confirm the mod is in the directory containing `AliceMadnessReturns.exe`.
- In the launcher, confirm that the selected path is the same copy of the game
  that Steam or EA App starts. Use **Add game** when multiple copies exist.
- Install Microsoft Visual C++ Redistributable 2015–2022 x86.
- Install or repair .NET Framework 4.8 if the launcher itself does not open.
- Remove unrelated `dinput8.dll` proxy mods temporarily; only one file with that
  name can be loaded.
- Restore the previous DLL with
  `AliceCoop\Advanced\Tools\Uninstall-AliceCoop.bat` to confirm whether the
  issue is mod-related.

## Installation failed

- Close every game and hosting process, then retry **Repair**.
- Approve the Windows administrator prompt.
- Read `%LOCALAPPDATA%\AliceCoop\installer-status.txt` for the exact failure.
- Extract the installer before running it; do not start the launcher inside the
  ZIP preview.

## Server or peer stays offline

- Both players must use the same IP, UDP port, release and protocol version.
- Run only one relay server.
- Allow `AliceCoopServer.exe` through Windows Firewall for private networks.
- Use **Test connection** on the client before launching the game.
- Ping the VPN address and confirm both players are in the same VPN network.
- Do not use the host's public Internet address without a trusted VPN.

## Join does nothing

- Wait until the host is fully in gameplay and not loading a map.
- A join action is temporarily unavailable during a host cutscene or checkpoint
  transition; wait for the yellow action to return and press `L`.
- If the client profile has never initialized controls, start a normal new game
  once or use verified host-profile synchronization and restart the client.

## Client loaded far away

This is intentional during world construction. Wait until geometry and controls
are ready, then press `O`. Prefer `P` during ordinary same-area gameplay.

## A cutscene is waiting forever

Move both players into the trigger area. If both are present and no encounter is
still active, press `L` while the yellow force button is visible. Restart the
checkpoint if the resulting world state is incomplete.

## Proxy effects remain in the world

Current builds periodically retire orphaned native glide effects and bound the
optional static trail pools. Make sure both players use the same latest DLL. If
the problem repeats, disable trails with `T` in the pause menu and attach the
latest client log to an issue after redacting personal information.
