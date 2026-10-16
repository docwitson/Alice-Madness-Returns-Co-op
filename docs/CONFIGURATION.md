# Configuration reference

## Launcher session

The graphical launcher is the normal entry point. It writes the selected role,
game directory, role, relay address, port and display mode atomically to a
per-launcher file under:

```text
%LOCALAPPDATA%\AliceCoop\sessions\session-<id>.ini
```

The file contains a unique named-mutex identifier. The client DLL accepts this
session only while the launcher instance that created it is alive and only when
its game directory matches the DLL's own installation. This lets two launcher
instances control two different local game copies without overwriting each
other, while a Steam relaunch keeps the intended role. Environment overrides
still take priority, and `AliceCoop.ini` remains the fallback when no matching
live launcher session exists.

The launcher remembers non-sensitive UI preferences separately in
`%LOCALAPPDATA%\AliceCoop\launcher.ini`.

## Manual launch configuration

Advanced users can edit `Advanced\Payload\Manual\AliceCoop-LaunchConfig.bat`
in the extracted installer package. In a drop-in installation, the same files
are under `AliceCoop\Advanced\Manual`. The supplied host/client BAT files read
that shared configuration.

| Variable | Default | Description |
| --- | --- | --- |
| `SERVER_IP` | `127.0.0.1` | Address of the PC running the relay server |
| `SERVER_BIND_IP` | `0.0.0.0` | Local interface used by the relay server |
| `COOP_PORT` | `27018` | UDP port used by every participant |
| `DISPLAY_MODE` | `fullscreen` | `fullscreen`, `windowed`, or `borderless` |
| `WINDOW_WIDTH/HEIGHT` | `1920/1080` | Normal window or explicit borderless size |
| `WINDOW_X/Y` | `0/0` | Normal window position |
| `TEST_WINDOW_WIDTH/HEIGHT` | `1440/900` | Local two-window test size |
| `TEST_BACKGROUND_DAMAGE_GUARD` | `1` | Prevents damage in the unfocused local test window |

Host/client scripts also accept temporary overrides:

```text
AliceCoop-Host.bat 127.0.0.1 27018
AliceCoop-Client.bat 26.x.x.x 27018
AliceCoop-Server.bat 0.0.0.0 27018
```

## Native co-op UI

The experimental MadnessPatch 3.3.3 integration adds Multiplayer to the original
Scaleform main menu using the game's button template, fonts and navigation.
Its submenu shows role-specific connection information. The host sees only
About and Back; the client can Join host and Receive host profile when available,
plus About and Back. About groups the creator, MadnessPatch credits and license,
and offers native selectable links to GitHub (Source & Releases), Nexus Mods, X and
YouTube. Mouse, arrows/D-pad and Enter/A use the normal menu navigation; links
open in the default browser. Back is last and selected by default on entry.
Receiving a profile opens a separate native confirmation page with Back at the
bottom, selected by default. Profile replacement requires the explicit Receive and replace
choice, checks that the selected profile has not changed, and keeps a backup.
Select the role and server address in the launcher.

The version/role/connection label appears only in the main menu. Native headings and normal
body fonts come from the game's imported font library. Connection states use
separate size/color styling. The information column sits on the right, with
translucent vector panels separating status, instructions and navigation.
The patch validates the known movie layout and
leaves unsupported versions untouched. No game assets or fonts are bundled.

Event notifications and pause controls use compact UE3 Canvas panels with thin
borders and translucent dark backgrounds matching the Multiplayer information
blocks. The cutscene wait appears at the top center as two centered lines with
an L reminder. The watching indicator uses the transparent eye icon. Passive
messages do not capture gameplay input.

Native navigation uses arrows / D-pad, Enter / A and Escape / B. Menu-specific
L (join), K (profile), and Y / N confirmation shortcuts were removed. L remains
only for an active shared-cutscene wait; T toggles movement trails while paused,
and Home opens achievements when supported.

## Proxy weapon audio (protocol 43)

Both games and the relay must use the audio build; protocol 42 cannot connect.
Weapon/body sound notifies are selectively replayed without enabling damage or
collision notifies. Native weapon, projectile and clock-bomb SoundCues are
captured from the owner's actual audio calls; UI/music/NPC audio is excluded.
Spatial components keep the original cue, volume and pitch. Tracked loops send
100 ms keepalives and expire after 600 ms without updates; stopping, weapon
changes, cinematics, disconnects and map changes retire their components.
The log records installed audio hooks and the first transmitted/received cue.
The fixed audio build passed a user-reported two-player check on 2026-10-16.
Positional balance and full weapon/level coverage remain subject to playtesting.
Unsupported argument expressions are not evaluated by capture.

Missing native cloth/hair keys in an old alpha.2 INI now default to enabled;
explicit `NativeClothSimulation = 0` or `NativeHairSimulation = 0` stays disabled.

## AliceCoop.ini

Normal releases ship conservative defaults. The most relevant sections are:

- `[Network]`: relay address, port, update rate and timeout fallback.
- `[VisualProxy]`: interpolation, collision policy, hair alignment and optional
  movement-trail history.
- `[SharedWorld]`: enemy health/transform authority and reconciliation limits.
- `[Window]`: fallback per-process window management; the graphical launcher
  or manual launch scripts normally override it.
- `[Performance]`: process FPS and worker-thread limits.
- `[Trace]`: diagnostic logging. Leave every trace disabled for normal play.

Normal play retains startup, connection, travel, recovery and error logs.
Animation transition/pose samples require `AnimationLifecycleEnabled` (transition
state also accepts `AnimationComparisonEnabled`). Cosmetic snapshots require
`VfxLifecycleEnabled` or the explicit local-mirror diagnostic mode. Periodic enemy
pose summaries require `WorldEnabled`, and detailed cutscene state requires
`ControlLifecycleEnabled`. These options default to 0; disabling them does not
turn off native animation, cloth, effects or enemy reconciliation.

`[Trace] InvariantEnabled = 1` enables passive completion-point checks for
world reset, remote-player teardown, cutscene barrier release and save sync.
Violations are emitted as machine-readable `[CoopInvariant]` log records. The
checks do not dereference stale objects, modify game state or run every frame.
`AliceCoop-Diagnostic-Both.bat` enables this setting through the
`ALICECOOP_INVARIANT_TRACE` environment override.

`[Trace] ProcessEventBridgeEnabled = 1` enables per-thread accounting around
the Alice Co-op portion of the shared engine `ProcessEvent` hook. Terminal
decisions and quiescent map/menu summaries are emitted as `[CoopBridge]`
records. The diagnostic launcher enables it through
`ALICECOOP_PROCESS_EVENT_BRIDGE_TRACE`; leave it disabled for normal play.

See [`SMOKE_TEST.md`](SMOKE_TEST.md) for the diagnostic test sequence and log
acceptance criteria.

For a manual Steam launch without the graphical launcher, set
`EnableWithoutLauncher = 1` and explicitly set
`Role = host` or `Role = client`. Steam may relaunch the game from its existing
client process, which discards the per-process environment supplied by the BAT
files. Set `ServerAddress` to the relay PC's reachable LAN/VPN address, or to
`127.0.0.1` when the relay runs on the same PC.

Keep `EnableWithoutLauncher = 0` when using the supplied host/client BAT files;
those scripts set the role and enable co-op separately for each process.

## MadnessPatch.ini

The combined DLL reads the normal MadnessPatch configuration beside
`AliceMadnessReturns.exe`. Alice Co-op does not require all optional graphics
features to be enabled. Two simultaneous 32-bit game processes may benefit from
lower texture pressure and a 60 FPS limit.

Keep `CrashFixes`, high-FPS physics fixes, atomic saves and input fixes enabled.
