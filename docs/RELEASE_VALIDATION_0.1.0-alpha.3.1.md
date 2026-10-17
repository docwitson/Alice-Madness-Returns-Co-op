# Alpha.3.1 hotfix validation

Local checks completed on 2026-10-17:

- Fresh source snapshot: Debug and Release x86 builds; unit tests, relay
  self-test and launcher self-test passed in both configurations.
- ProcessEvent bridge layout and native Scaleform action/navigation checks passed.
- Release installer and drop-in passed exact contents, manifests, checksums,
  quiet defaults, isolated install/repair/uninstall and packaged relay probing.
  Package lint reported zero failures and zero warnings.
- Actual game launches were checked directly and through Steam `-applaunch 19680`,
  using live per-install launcher sessions and legacy positive INI dimensions.
  Windowed passed twice on each path, including restart: a changed window size
  and position remained stable across six two-second observations.
  Borderless used a frameless 3440x1440 monitor-sized window. Fullscreen startup
  also completed on both paths. Temporary game DLLs were restored after testing;
  Steam test configuration files were restored from their pre-test backups.
- The actual WPF ComboBox SelectionChanged event saved Windowed immediately
  without launching a game and retained it on close. This used an isolated WPF
  host; original launcher preferences were restored afterward.

These checks cover startup and window management, not another full campaign
playthrough. In-game display-setting navigation and Alt+Enter were not driven
automatically. The startup-only preference guard has a regression test covering
later viewport calls; explicit Borderless mode retains monitor-sized rendering.

Protocol 43 and gameplay/network code are unchanged. The DLL and launcher must
be updated using Install/Repair. Final CI artifacts have their own checksums;
earlier antivirus results do not apply to new binary hashes. No VirusTotal
upload is performed by the release process.
