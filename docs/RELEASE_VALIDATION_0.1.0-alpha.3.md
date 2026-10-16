# Alpha.3 local release validation

## Pre-commit audit - 2026-10-17

A new source-only snapshot of the current worktree, including the three new
audio files, built successfully as Debug and Release x86. Both configurations
passed AliceCoopTests, relay self-test and launcher self-test. The native
ProcessEvent layout and Scaleform action/navigation checks also passed. The
existing EnginePointers.cpp int-to-float conversion warning remains.

Fresh Release installer/drop-in packages passed exact contents, manifests,
checksums, quiet INI defaults, isolated install/repair/uninstall, restoration of
the previous proxy DLL and a packaged relay probe. Package lint passed with
45 files, zero failures and zero warnings, including a comparison against the
retail AliceGame content directory. The installed game and previously scanned
candidates were preserved. No automated game controls or new playtest were used.

Local Markdown links, tracked/untracked source checks for common secret formats
and private development paths, workflow YAML and its 15 PowerShell steps passed.
Version handling accepted tag/manual Alpha.3 inputs and rejected invalid inputs.
Installation guides now use the native Multiplayer actions; the bug template
uses protocol 43. The repository's GitHub About description still mentions
MadnessPatch 3.1.1 and needs a repository-settings update when Alpha.3 is published.

All three fresh audit binaries have different hashes from the previously
scanned fix1 package. Their build paths/metadata differ; prior scan results must
not be attributed to them. The final committed/CI build still needs its own
hash record, tests and any required antivirus review. Local checks did not run
GitHub-hosted Actions or establish a clean committed release revision. No
commit, push, tag or release publication was performed during this audit.

## Launcher selection - 2026-10-17

The release keeps the original launcher packaging: PNG/JPG artwork and the
custom executable icon are embedded. External-artwork experiment 14 is not the
release launcher. Its C# application logic and XAML were unchanged; only the
project resource packaging differed. Experiments remain outside tracked source.

The user initially reported no detections for experiment 14, SHA-256
`8cde230c238adaa49368060ec7a0fd20cae11005793fa18ec1dc8317816cb9e7`, then reported
Palo Alto Networks `Generic.ml` while rescanning the launcher from the candidate
folder. That folder contained the exact same 14 binary. No report URL or scan
timestamp was supplied for the later result. This is not a confirmed detection
on the original launcher hash below. Resource ablations did not identify an
exact detector trigger or provide a stable no-detection result.

The decision is to retain the original distribution layout and request vendor
review of any persistent suspected false positive, rather than select new
builds solely by their detection count. No vendor has confirmed a false positive
and no guarantee of future antivirus verdicts is implied. Final committed/CI
binaries still need their own hash comparison and any required review.

## Fixed audio candidate - 2026-10-16

This section supersedes the protocol-42 candidates below. Tested package:
`0.1.0-alpha.3-proxy-audio-fix1`, protocol **43**, MadnessPatch **3.3.3**.
The package manifest records base commit `ae0094e` and `sourceDirty=true`;
the corresponding source ZIP includes the uncommitted audio changes and fix.
It is a tested local candidate, not evidence of a published tag or a clean-tree build.

### Confirmed checks

- The user reported a successful two-player test after the audio crash fix.
  The first-sound crash was traced to a one-byte SDK result buffer for a
  four-byte UE3 `IsPlaying` return. The corrected ABI has compile-time and
  runtime checks; a canary test failed before the fix and passes afterward.
- Release x86 DLL build and Debug/Release unit tests passed. Relay self-test,
  ProcessEvent bridge layout and native menu action/navigation checks passed.
- Installer/drop-in validation passed, including manifests, binary/checksum
  identity, isolated installation/repair/uninstall and the relay probe.
- Package lint passed: 42 files, zero failures and zero warnings.
- Release defaults disable detailed traces, movement history, mirror/tuning,
  status debug overlays, asset dumping and loose-file mod loading.

### Exact binaries checked by the user

| File | SHA-256 | User-reported VirusTotal result, 2026-10-16 |
| --- | --- | --- |
| AliceCoopLauncher.exe | `48724fdc7c9adf5307a625f2eb58860629ad0b7b73b7f3c636c7abca6410f6e6` | 1/69: MaxSecure, `Trojan.Malware.300983.susgen` |
| AliceCoopServer.exe | `76f4032a2c4035109bf38f7c3f4ddbfef30387262ee763d8375560b878662902` | Reported clear |
| dinput8.dll | `1307f6e75193387be7618b48e2a8e181f5e5e4c9850fc5fe7392547f1f2a2c8b` | Reported clear |

Launcher report: [VirusTotal](https://www.virustotal.com/gui/file/48724fdc7c9adf5307a625f2eb58860629ad0b7b73b7f3c636c7abca6410f6e6).
These verdicts were supplied by the user; the report's JavaScript interface
could not be independently inspected here. They apply only to these hashes.
The launcher hash is unchanged from the earlier audio package; a changed verdict
does not establish a source change. Its unusual PE date comes from the enabled
deterministic .NET compiler, whose timestamp field is hash-derived, rather than
from an actual compilation date in 2082. See
[Microsoft's compiler documentation](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/compiler-options/code-generation#deterministic).

A suspected false positive remains unconfirmed by MaxSecure. VirusTotal recommends
rescanning and contacting the detecting vendor if it persists; a detection count
alone does not establish safety. See [VirusTotal's guidance](https://docs.virustotal.com/docs/false-positive-contacts).

### Readiness review - 2026-10-16

- Both READMEs now lead with the Alpha.3 showcase, describe audio/native physics
  and protocol 43, and explain the more limited Alpha.3 testing.
- Installation/recovery instructions and release notes match the native menus;
  changelog, release notes and this record are included in canonical packages.
- A local `0.1.0-alpha.3` repack with `-SkipBuild` passed installation/package
  checks and lint (45 files, zero failures/warnings). All three binaries match
  the scanned fix1 hashes above. The user's fix1 installer folder is untouched.
- YAML and PowerShell workflow steps were parsed locally. Version resolution
  was exercised for tags/manual input and rejects invalid values. The release
  workflow prepares a draft; no remote Actions run or release was initiated.
- Launcher defaults now declare the same version/product/author metadata used
  for the scanned build, so future ordinary source builds retain that metadata.
  Only the project defaults were changed; the scanned launcher was not rebuilt.
- Local Markdown links and current source-file checks for common secret formats
  and private development paths passed. These checks are not a security certification.

### Remaining release steps

- Commit the audio implementation, ABI fix and release documentation so the
  release tag points to all files, including currently untracked additions.
- Package the committed revision under the canonical `0.1.0-alpha.3` version,
  verify the source reference and repeat package checks.
- Compare the final three binary hashes with the table above. A rebuild,
  including a GitHub Actions build, may change them; scan any changed binaries.
- Resolve or explicitly assess the remaining MaxSecure result before publication.
  No zero-detection guarantee is implied for future rescans.
- Review and publish the draft release manually. No push, tag or publication was
  performed as part of this readiness check.

The two-player result does not replace Alpha.2's longer playtesting. Full
weapon/level audio coverage, long cinematic sessions and scripted encounters
remain subject to playtesting.

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
