# Alice Co-op smoke test

Use this checklist after structural, build-system or diagnostic changes. The
pass condition is behavior no worse than the current baseline: no new crash,
hang, desynchronization or `[CoopInvariant]` record.

## Preparation

1. Build `Debug|x86`, then rebuild `Release|x86`.
2. Run `bin\Release\AliceCoopTests.exe` and
   `bin\Release\AliceCoopServer.exe --self-test`.
3. Build both release packages and run `tools\Test-AliceCoop-Package.ps1`.
4. Create `artifacts\refactor-backup` (ignored by Git). Copy the currently
   installed `dinput8.dll` there and record its SHA-256 with
   `Get-FileHash -Algorithm SHA256`.
5. Back up both players' saves. Use dedicated co-op profiles; save sync testing
   must never use the only copy of a real profile.
6. Install the newly built DLL and start the relay before starting either game.

## Combined diagnostic run

Launch `AliceCoop-Diagnostic-Both.bat` and play for 20–30 minutes. Exercise:

- host/client connection, disconnect and reconnect;
- movement, jump, glide, dodge, shrink, hair, dress, particles and trails;
- weapon switching, ordinary combat, enemy targeting and breakables;
- interactions, a synchronized cutscene and a map change;
- safe/forced teleport (`P`/`O`), checkpoint restart and return to menu;
- pause UI, window resizing and recovery of the overlay after resolution
  changes;
- save sync on the dedicated test profiles only.

For a network-representative pass, repeat the connection, map-change and combat
parts on two computers over a trusted LAN or VPN.

## Log review and acceptance

Review the client and relay logs after disconnecting cleanly. The run passes
only when:

- neither game nor the relay crashes, hangs or enters a new error loop;
- connection, world and presentation behavior is not worse than baseline;
- no line begins with `[CoopInvariant]`;
- both packages contain the exact binaries used for the successful run and
  still pass the package verifier.

Existing gameplay bugs belong in a separate follow-up issue. Do not fix them as
part of a structural safety-net change.

## Baseline recovery

If the run fails, stop both games and the relay. Restore the backed-up
`dinput8.dll`, confirm its saved SHA-256, restore the dedicated test profiles if
save sync was exercised, and rerun the same scenario against the baseline.
Do not continue to later refactoring stages until the transfer or diagnostic
regression has been isolated and the same gate passes again.
