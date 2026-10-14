# Alpha.3 local release validation

## VERSIONINFO comparison - 2026-10-13

The local `0.1.0-alpha.3-versioninfo` variant adds Windows version resources to
`AliceCoopServer.exe` and `dinput8.dll`. The resources identify Alice Co-op,
Docwitson, each binary's purpose, file version `0.1.0.3` and product version
`0.1.0-alpha.3`. The DLL also credits MadnessPatch by Wemino and contributors.
The binaries remain unsigned.

The baseline alpha.3 server was reported by the user as detected by CrowdStrike
Falcon (`Win/malicious_confidence_60% (D)`), and the DLL by MaxSecure
(`Trojan.Malware.300983.susgen`). After scanning the comparison variant, the user
reported that those detections had disappeared. This is a user-reported result;
the complete VirusTotal reports were not independently inspected.

Repeat scanning later is still pending. The comparison does not establish the
cause of the changed verdicts or guarantee the same result for future builds.

### Scanned comparison binaries

| File | SHA-256 |
| --- | --- |
| AliceCoopServer.exe | `3e843a2d00b365af7625a4910151a97c8f8369308c19685b74aa7b4a952830a0` |
| dinput8.dll | `7cfea872411fe3ae8617f7661a8b033fda83e67f072045c03e1f57a53848e6f5` |

### Local checks

- Release x86 build, AliceCoop tests and relay self-test passed; protocol is 42.
- Native `.text`, `.data` and `.reloc` sections, imports and exports match the
  baseline. Differences in `.rdata` are confined to debug directory/data.
- The launcher is unchanged, including its SHA-256.
- Installer/drop-in package validation passed, including isolated installation,
  repair and uninstall. Package lint: 42 files, no failures or warnings.
- Original alpha.3 archives and installed game binaries were preserved.

The comparison archives, complete source ZIP and checksums are stored locally
under `artifacts/local-release/0.1.0-alpha.3-versioninfo/`. No upload or public
release was performed by the build process.

## Local release candidate - 2026-10-15

Candidate label: `0.1.0-alpha.3-rc-20261015`; runtime version `0.1.0-alpha.3`,
file version `0.1.0.3`, protocol 42, MadnessPatch 3.3.3. This includes the native
menu stream fix, persistent session/cache Join fix, deferred cinematic cloth
recovery, thin Canvas notices/transparent eye and quieter diagnostic defaults.
No speculative Cardbridge T-pose change is included.

### Completed checks

- Debug and Release x86 solution builds completed. Release reported the existing
  EnginePointers.cpp int-to-float conversion warning; no build errors.
- Debug/Release AliceCoop tests and relay self-tests passed, including bounded
  archive-cache recognition and allocation-preserving invalidation cases.
- ProcessEvent bridge layout and native Scaleform action/navigation checks passed.
- Installer and drop-in package verification passed: exact contents, binary and
  manifest hashes, isolated install/repair/uninstall and packaged relay probe.
- Package tests require all diagnostic traces, mirror/tuning, movement history,
  background damage guard, status debug overlay and asset dumping/loose mod
  loading to be disabled. A deliberately enabled trace was rejected.
- Universal-modder package lint passed: 42 files, zero failures and warnings.

### Remaining manual checks

- Confirm the final top-center, two-line cutscene wait and its L reminder in game.
- Repeat a longer two-player session with cinematics to check cloth restoration.
- Repeat antivirus scans on the final candidate if desired. The earlier reported
  results above apply only to the listed comparison hashes, not this new DLL.

The user confirmed the latest thin-frame styling and could not reproduce the
Cardbridge T-pose again. The final notice placement and quiet logging changes
have not been tested by controlling the game. Runtime recovery/error logs and
explicit diagnostic launch modes remain available.

Local installer/drop-in archives, corresponding source ZIP, release notes and
SHA-256 checksums are kept under
`artifacts/local-release/0.1.0-alpha.3-rc-20261015/`. The package manifest records
the source commit and clean-worktree status. Actual artifact hashes are recorded
beside the archives so rebuilding the launcher for the committed revision does
not require a self-referential source commit. No push, tag, upload or publication
is part of this validation.
