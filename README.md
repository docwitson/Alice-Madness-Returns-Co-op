# Alice Co-op

![Alice Co-op — experimental two-player co-op alpha](docs/media/alice-co-op-banner.png)

<p align="center">
  <strong><a href="https://www.youtube.com/watch?v=I5nUTZjpCXY">▶ WATCH THE ALPHA.3 SHOWCASE</a></strong><br>
  <strong><a href="https://www.youtube.com/watch?v=QO0yarqS1-k">▶ WATCH THE ORIGINAL RELEASE TRAILER</a></strong><br>
  <strong><a href="https://www.youtube.com/watch?v=uD7oxI4mNWY">▶ WATCH THE LAUNCHER INSTALLATION &amp; SETUP GUIDE</a></strong><br>
  <strong><a href="https://www.youtube.com/watch?v=j5VLSzZCsK0">▶ WATCH THE COMPLETE 10-HOUR PLAYTHROUGH (EARLIER BUILDS)</a></strong><br>
  <strong><a href="https://www.nexusmods.com/alicemadnessreturns/mods/87">⬇ DOWNLOAD ON NEXUS MODS</a></strong>
</p>

[Русская версия](README_RU.md) · [Launcher installation](docs/INSTALL.md) ·
[Manual/drop-in installation](docs/MANUAL_INSTALL.md) ·
[Known issues](docs/KNOWN_ISSUES.md) · [GPL-2.0](LICENSE)

Alice Co-op is an experimental two-player cooperative mod for the Windows version
of *Alice: Madness Returns*. Each player runs their own game process; a small UDP
relay exchanges player, animation and world state while the host provides the
authoritative view of ordinary enemies.

## Alpha.3.1 display hotfix

Alpha.3.1 fixes forced window resizing, display-mode persistence and cropped or
squeezed menus, gameplay and video cutscenes when fullscreen resolution differs
from the desktop aspect ratio. Close the game and use **Install/Repair** with the
new launcher to update the DLL.
Protocol **43** is unchanged and public Alpha.3 remains compatible.
See the [hotfix notes](docs/RELEASE_NOTES_0.1.0-alpha.3.1.md).

## Alpha.3

- Improved partner animations, weapon effects and spatial weapon sounds.
- Native cloth and hair simulation for the remote Alice.
- Multiplayer in the original game menu, with connection status, profile
  transfer, About and matching pause/cutscene notices.
- Built on MadnessPatch 3.3.3 by Wemino and contributors.

Alpha.3 uses **protocol 43**. Update both players and the relay together; Alpha.2
and Alpha.3 test builds using other protocols are incompatible. See the
[release notes](docs/RELEASE_NOTES_0.1.0-alpha.3.md) and [changelog](CHANGELOG.md).

> [!WARNING]
> This is an alpha playtest build. Back up your saves, use a dedicated co-op
> profile, and expect occasional checkpoint restarts or manual recovery in
> heavily scripted encounters.

## Gameplay preview

![Actual Alice Co-op gameplay](docs/media/alice-co-op-gameplay-preview.webp)

*Actual Alice Co-op gameplay. The header is unofficial
promotional artwork containing AI-generated elements.*

## What currently works

- Host and client can see and independently control their own Alice.
- Partner movement, jumps, gliding, shrinking, dodges, melee attacks and ranged
  upper-body aiming are synchronized.
- The remote Alice uses native cloth and hair simulation.
- Weapon trails, pepper/teapot projectiles and impacts, hobby-horse shockwaves
  and clock-bomb animations, including the rabbit reveal, are reproduced.
- Partner weapon sounds are spatial, including animation sound events,
  projectiles and the clock bomb; sustained sounds stop with their source.
- Ordinary enemies share host-authoritative position, health and death state.
- Enemies can change focus between the host and client, and damage both players.
- Many breakable props are destroyed for both players while loot remains local.
- Shared death, checkpoint restart and return to the main menu are coordinated.
- Common interaction cinematics and trigger cutscenes can be synchronized.
- The client can join the host checkpoint without replacing their selected
  profile. Optional host-profile replacement is a separate, explicitly confirmed
  action with integrity verification and a backup.
- Partner teleports and shared-cutscene recovery controls are available in game.

This is not native Unreal Engine 3 networking. Both games still simulate parts
of the level locally, and Alice Co-op reconciles the pieces that are practical to
identify safely at runtime.

## Requirements

- A legitimate PC installation of *Alice: Madness Returns* (Steam or EA App).
- Windows 10 or Windows 11, 64-bit OS; the game and mod are 32-bit.
- The Microsoft Visual C++ 2015–2022 Redistributable **x86**.
- .NET Framework 4.8 for the graphical launcher.
- The exact same Alice Co-op release on both computers.
- Localhost, a trusted LAN, or a trusted VPN such as Radmin VPN.

Alice Co-op includes a combined client DLL based on
[MadnessPatch 3.3.3](https://github.com/Wemino/MadnessPatch/releases/tag/3.3.3).
A separate MadnessPatch installation is not required.

## Launcher

![Alice Co-op launcher with installation, host and join controls](docs/media/alice-co-op-launcher.jpg)

The graphical launcher is the recommended way to install and play. It finds
Steam installations, supports additional game copies, manages the relay and
keeps each game role isolated without requiring BAT or INI editing.

## Quick start

1. Close both games and the old relay before installing or updating.
2. Download the latest `AliceCoop-*-installer.zip` from
   [Releases](https://github.com/docwitson/Alice-Madness-Returns-Co-op/releases).
3. Extract it into a normal writable folder, run `AliceCoopLauncher.exe`, select
   the folder containing `AliceMadnessReturns.exe`, and choose **Install/Repair**.
   Opening a new launcher alone does not update an already installed DLL.
   Use **Add game** if you keep more than one installed copy.
4. On the host PC, choose a trusted LAN/VPN address, allow the relay through
   the private-network firewall when prompted, and press **Host Game**. Use
   **Copy address** and send the copied endpoint to the other player.
5. On the second PC, enter that address, press **Test connection**, then
   **Join Game**. The launcher uses the matching Steam installation when it
   can identify one; other installations are started directly.
6. Keep both launchers open during the session. The host launcher reports when
   both game processes have connected to the relay and owns the relay process.
7. Select a profile in each game. **Multiplayer > Receive host profile** is
   optional: it replaces the selected client profile with the host's progression
   after confirmation and creates a backup. It is not required for Join.
8. The host first starts or continues a game. The client chooses
   **Multiplayer > Join host**. Join receives an isolated session copy of the
   host's saved checkpoint and loads its subarea through native Continue.
   It keeps the client's selected profile intact. The client then prepares
   the host's visible sublevels and automatically arrives beside the host
   once the destination sublevels are ready. If loading times out, `O` retries.

The relay server may run on either player's computer. Allow inbound UDP traffic
for its configured port (default `27018`). Do not expose it directly to the
public Internet: the prototype protocol has no authentication or encryption.

See the full [launcher installation and update guide](docs/INSTALL.md). The
direct `drop-in.zip` and BAT/INI workflow remain available separately for
advanced users and development in the [manual installation guide](docs/MANUAL_INSTALL.md).
The [legacy manual installation and setup video](https://www.youtube.com/watch?v=UzjCvp7MhTM)
also demonstrates this workflow.

## Controls

| Key | Context | Action |
| --- | --- | --- |
| `P` | Gameplay | Go to the partner: client to host; host to client |
| `O` | Gameplay | Client: meet the host across the level; host: summon the client |
| `L` | Waiting for a shared cutscene | Continue together |
| `T` | Pause menu | Toggle bounded proxy movement-trail history |

P/O wait for destination areas to load when needed and also work while either
player is airborne. On timeout the player remains at the current location.
The pause panel lists these controls.

The normal game keeps ownership of these keys outside the listed contexts.

## Configuration

The launcher stores each temporary live session in:

```text
%LOCALAPPDATA%\AliceCoop\sessions\session-<id>.ini
```

The session is accepted only while that launcher instance is alive and only by
the selected game installation, so two local copies do not overwrite each
other. Steam may restart the game without losing its role. Manual launch
settings and the full mod configuration live in:

```text
Advanced\Payload\Manual\AliceCoop-LaunchConfig.bat
AliceCoop\AliceCoop.ini
```

The drop-in archive places manual launch files under `AliceCoop\Advanced\Manual`
instead. MadnessPatch settings remain in `MadnessPatch.ini` next to the game
executable. See [docs/CONFIGURATION.md](docs/CONFIGURATION.md).

## Known limitations

- Some bosses and scripted waves can desynchronize between game processes.
- Certain switches, pig snouts, moving platforms and secret rooms remain local.
- Cutscene barriers are heuristic; use `L` during a shared wait only when both
  players are stuck.
- The side-scrolling ship stages intentionally run independently in solo mode.
- Host profile synchronization overwrites the selected client profile.

Read the complete [known-issues and recovery guide](docs/KNOWN_ISSUES.md).

Alpha.2 received extensive playtesting. Alpha.3 mainly changes animation, physics,
effects, audio and UI and has not received the same depth of testing. Enemy
synchronization and interaction logic were not intentionally changed, but
regressions remain possible.
Alpha.2 remains available in Releases if you need to return to the previous build;
both players and the relay must roll back together.

## Building from source

Open `AliceCoop.sln` in Visual Studio 2022 and build `Release|x86`, or run:

```powershell
& .\tools\Build-AliceCoop-Package.ps1
```

The build requires the Visual Studio C++ workload and Windows 10/11 SDK. Build
outputs and release archives are intentionally ignored by Git. See
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Project status and support

Alice Co-op is community research and remains an alpha. When reporting a problem,
include the map/checkpoint, which player triggered the event, reproduction steps,
and the relevant host/client logs after removing personal paths and addresses.

## Credits and license

Alice Co-op is based on [MadnessPatch](https://github.com/Wemino/MadnessPatch) by
Wemino and contributors.

The combined work is distributed under the
[GNU General Public License version 2.0](LICENSE). See [NOTICE.md](NOTICE.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This project is unofficial and is not affiliated with or endorsed by Electronic
Arts, Spicy Horse, Epic Games, or the MadnessPatch project. No retail game files
are included.
