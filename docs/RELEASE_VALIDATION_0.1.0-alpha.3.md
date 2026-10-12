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
