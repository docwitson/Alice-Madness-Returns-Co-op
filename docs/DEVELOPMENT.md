# Development and release process

## Toolchain

- Visual Studio 2022
- Desktop development with C++ workload
- MSVC v143 x86 tools
- Windows 10 or Windows 11 SDK
- .NET Framework 4.8 targeting pack
- PowerShell 5.1 or later

Clone the repository and build:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  .\AliceCoop.sln /m /t:Build "/p:Configuration=Release;Platform=x86"
```

Expected outputs:

```text
bin\Release\dinput8.dll
bin\Release\AliceCoopServer.exe
bin\Release\AliceCoopLauncher.exe
```

Run the relay self-test:

```powershell
& .\bin\Release\AliceCoopServer.exe --self-test
```

Run the protocol and pure-helper tests:

```powershell
& .\bin\Release\AliceCoopTests.exe
```

The relay reports the wire protocol version without opening logs or sockets:

```powershell
& .\bin\Release\AliceCoopServer.exe --protocol-version
```

## Native menu changes

`tools/Generate-Coop-Scaleform.py` generates only our AVM1 additions into
`include/CoopScaleformActions.inl`; rerun it after changing that generator.
`src/Graphics/CoopScaleformPatch.inl` patches the user's loaded movie after
validating its layout, updates function/branch/tag sizes, and keeps existing
MadnessPatch byte patches at their original offsets. Never check extracted
movies or textures into the repository.

```powershell
python .\tools\Test-Coop-Scaleform-Actions.py
```

This pure action test covers own menu commands/back behavior and native menu
passthrough. A native stream test against the installed movie and manual checks
of fonts, mouse/controller navigation, submenu layout and return are also
required after changing the stream rewriter.

## Packaging

```powershell
& .\tools\Build-AliceCoop-Package.ps1 -Version 0.1.0-alpha.3
```

The script rebuilds `Release|x86`, runs the unit tests and server/launcher self-tests,
stages installer and drop-in packages, writes manifests/checksums, and creates
ZIP archives below `artifacts\deploy`.

Validate both archives against their manifests, checksums, exact file layouts
and the freshly built DLL/server binaries:

```powershell
& .\tools\Test-AliceCoop-Package.ps1 -Version 0.1.0-alpha.3
```

CI and release workflows add `-RequireCleanSource` to both packaging and
verification. Local packaging permits a dirty worktree unless that switch is
specified.

The release workflow creates a draft when a version tag is pushed. Review its
notes and assets before publishing. Local installer/drop-in archives include
the changelog and version-specific release notes. Local dirty builds need an
accompanying source snapshot; a base commit alone does not include uncommitted files.

Antivirus results are tied to file hashes. Repackaging documentation with
`-SkipBuild` preserves the binaries; rebuilding locally or in Actions can change
their hashes, even when behavior is unchanged. Record and scan the exact final
files intended for distribution. A deterministic .NET PE timestamp is hash-derived
and is not a wall-clock build date.

## Source layout

| Path | Purpose |
| --- | --- |
| `client/` | Combined MadnessPatch/Alice Co-op DLL project and default patch INI |
| `server/` | Standalone UDP relay |
| `launcher/` | WPF launcher/installer UI and Steam/game discovery |
| `src/Coop/` | Protocol integration, proxy, world and lifecycle synchronization |
| `src/Coop/Detail/` | Internal implementation fragments included only by `CoopClient.cpp` |
| `src/Coop/ProcessEventBridge.*` | Alice Co-op boundary inside the shared MadnessPatch `ProcessEvent` hook |
| `src/` | MadnessPatch base and shared hooks/features |
| `include/` | Project headers, vendored dependencies and UE3 SDK declarations |
| `lib/` | Vendored x86 static libraries required by the inherited build |
| `manual-launch/` | Advanced host/client/server and local-test scripts |
| `tools/` | Packaging, installation and uninstallation scripts |
| `images/` | Alice Co-op-owned runtime overlay images only |

## Testing levels

1. `AliceCoop-Both.bat`: ordinary local smoke test.
2. `AliceCoop-Animation-Test.bat`: focused animation comparison logs.
3. `AliceCoop-Diagnostic-Both.bat`: heavier lifecycle diagnostics; do not use for
   performance measurements.
4. Two computers over a trusted VPN for latency, join and long-session testing.

Never run destructive save synchronization tests against the only copy of a
real player profile.

The reproducible combined checklist and baseline recovery procedure are in
[`SMOKE_TEST.md`](SMOKE_TEST.md). The diagnostic launcher also enables passive
lifecycle invariant checks and per-thread ProcessEvent bridge accounting. Any
`[CoopInvariant]` or bridge balance failure must be investigated; the checks
only report state and never repair or assert on it.

## Release checklist

- Build from a clean tree.
- Confirm protocol and manifest versions match.
- Run server self-test.
- Run `AliceCoopTests.exe` in the tested configuration.
- Scan tracked files and commit history for secrets, logs and private paths.
- Validate both ZIP layouts, manifests and SHA-256 files with the package
  verifier.
- Test clean install, update, host, client and uninstall.
- Create a signed or annotated version tag where practical.
- Upload both archives and checksums to a draft GitHub Release.
- Review the repository and Actions logs before changing visibility to public.

The Alpha.3 MadnessPatch 3.3.3 build uses wire protocol 43 (Alpha.2 uses 29).
Host snapshots include up to 32 hashes of visible streaming packages (1,154 bytes
including the header, within the 1,200-byte UDP budget). Both games and the relay
must be updated together. Existing animation and weapon-event payloads are unchanged.
Proxy audio uses SoundCue start/update/stop kinds in the existing 148-byte weapon
event payload. Native Sound notifies are selected without enabling damage notifies.
The AudioComponent.IsPlaying result must use a four-byte UE3 UBOOL, with the
reflected return offset/size checked before invoking it; a one-byte C++ bool
corrupts the caller's stack. NativeAudioAbi.cpp exercises the actual SDK structure.
Safe arrival preloads these native sublevels without moving or suspending the pawn,
waits for visibility/loading, then uses native AlicePawn.FarMoveSetLocation with
bNoCheck (Actor.SetLocationNoCheck fallback), verifying the resulting position.
No floor, room, Walking-physics or stable-position gate is applied: teleportation
also works in flight. Timeout, death, disconnect or world change cancels the request.

During safe-arrival preparation the controller temporarily disables its native
streaming-volume participation. The engine otherwise recomputes destination
load/visibility from the still-distant source camera and cancels the preload.
The original flag is restored on cancellation (including travel with a retained
controller), or one second after successful native teleport for the camera
to consume the teleport. TELEPORT waiting diagnostics distinguish sublevel
load/visibility and a native teleport that did not move the pawn. Preparation UI
is shown only while loading actually waits; native Join skips a redundant arrival
when its valid checkpoint spawn is already within 250 units of the host.

Join uses SaveRequestJoinSession, separate from confirmed SaveRequestReplaceProfile.
Both files are verified and written under a unique client-saves/session-PID-ID cache.
Only game-issued read opens of the exact selected-profile checkpoint/PSD paths are
redirected during the session; native save writes remain isolated. Config reads and
profile-list metadata replacement are not part of Join. Returning to title disables
redirection before native profile reload. Native FindCheckpointData replaces the
cached object, followed by the title's LoadLastCheckpointFromTitleMenu command.
The manager processes this command asynchronously; do not require an immediate
PendingCheckpointAction transition. Saved map identity and host world epoch are
checked; no ambiguous chapter-start fallback is used.

Native menu rebuilds must update selectedButton as well as currentId.menu.
Stock setFirstMenu animates the button selected by the index but does not rebind
this reference; the first deselect otherwise targets a removed clip. Clear the
previous sfm interval before recreating buttons. Confirmation orders Receive and
replace above Back, with both the index and focus reference pointing to Back.
The native player-menu entrance at frame two releases outgoing title/profile
listeners and hides the sibling screens; input still unlocks at native frame22.
Only the patched title movie is fitted to the full viewport with GSM_ExactFit
when FixAspectRatio is enabled; its readiness resets on root movie reload.
Native main-menu music uses title.MainMenuMusicStart and the controller's uniqueAudio.
Recovery checks IsPlaying with native metadata preserved, leaves active cues alone,
and resumes the existing component or the title's PlayMusic callback while the
title movie owns focus and its APC is the local controller. Do not require the
controller's bInMainMenu flag: root callbacks may have run before it was ready,
leaving that flag false and uniqueAudio absent. Restore SetInMainMenu through
the movie's native script context, and report the component's post-call state.
Startup title visibility restores at native frame150.
