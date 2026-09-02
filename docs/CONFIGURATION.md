# Configuration reference

## Launch configuration

Edit `AliceCoop-LaunchConfig.bat` when launching the game through the supplied
host/client BAT files.

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

## AliceCoop.ini

Normal releases ship conservative defaults. The most relevant sections are:

- `[Network]`: relay address, port, update rate and timeout fallback.
- `[VisualProxy]`: interpolation, collision policy, hair alignment and optional
  movement-trail history.
- `[SharedWorld]`: enemy health/transform authority and reconciliation limits.
- `[Window]`: legacy per-process window management; launch scripts normally
  override it.
- `[Performance]`: process FPS and worker-thread limits.
- `[Trace]`: diagnostic logging. Leave every trace disabled for normal play.

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

For a normal Steam launch, set `EnableWithoutLauncher = 1` and explicitly set
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
