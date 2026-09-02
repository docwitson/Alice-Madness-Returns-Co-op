# Development and release process

## Toolchain

- Visual Studio 2022
- Desktop development with C++ workload
- MSVC v143 x86 tools
- Windows 10 or Windows 11 SDK
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

## Packaging

```powershell
& .\tools\Build-AliceCoop-Package.ps1 -Version 0.1.0-alpha.1
```

The script rebuilds `Release|x86`, runs the server self-test, stages installer
and drop-in packages, writes manifests/checksums, and creates ZIP archives below
`artifacts\deploy`.

Validate both archives against their manifests, checksums, exact file layouts
and the freshly built DLL/server binaries:

```powershell
& .\tools\Test-AliceCoop-Package.ps1 `
  -InstallerZip .\artifacts\deploy\AliceCoop-0.1.0-alpha.1-Installer.zip `
  -DropInZip .\artifacts\deploy\AliceCoop-0.1.0-alpha.1-DropIn.zip
```

CI and release workflows add `-RequireCleanSource` to both packaging and
verification. Local packaging permits a dirty worktree unless that switch is
specified.

## Source layout

| Path | Purpose |
| --- | --- |
| `client/` | Combined MadnessPatch/Alice Co-op DLL project and default patch INI |
| `server/` | Standalone UDP relay |
| `src/Coop/` | Protocol integration, proxy, world and lifecycle synchronization |
| `src/Coop/Detail/` | Internal implementation fragments included only by `CoopClient.cpp` |
| `src/Coop/ProcessEventBridge.*` | Alice Co-op boundary inside the shared MadnessPatch `ProcessEvent` hook |
| `src/` | MadnessPatch base and shared hooks/features |
| `include/` | Project headers, vendored dependencies and UE3 SDK declarations |
| `lib/` | Vendored x86 static libraries required by the inherited build |
| `manual-launch/` | Supported host/client/server and local-test scripts |
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
