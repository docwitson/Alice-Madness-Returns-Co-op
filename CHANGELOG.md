# Changelog

All notable user-facing changes are documented here. Alice Co-op follows
[Semantic Versioning](https://semver.org/) where practical; compatibility also
depends on the protocol version shown in each release.

## 0.1.0-alpha.1 — 2026-08-27

Initial alpha release.

### Added

- Two-player host/client relay over UDP for LAN, localhost and VPN adapters.
- A visible remote Alice proxy with movement, partial combat animation, weapons,
  hair, dress and presentation effects.
- Host-authoritative ordinary-enemy health, death, transforms and target changes.
- Shared breakable-object destruction while keeping pickups local to each player.
- Cutscene barriers, synchronized interaction cinematics and an emergency force
  action for known script desynchronization cases.
- Shared death/checkpoint restart and return-to-menu lifecycle handling.
- Safe and forced player teleports.
- Explicit, verified host-to-client profile synchronization with backups.
- Main-menu and pause-menu status overlays.
- Optional bounded proxy movement trails.
- Standalone relay server, manual launch scripts and a common launch configuration.
- MadnessPatch 3.1.1 functionality in the combined client DLL.

### Known limitations

- This is an experimental alpha, not native UE3 multiplayer.
- Bosses and heavily scripted encounters may require checkpoint restart,
  teleport, or the emergency cutscene action.
- Some world mechanisms remain locally simulated and can visually desynchronize.
- Remote animation, cloth, hair and weapon effects are approximations.
- The relay has no authentication or encryption and must not be exposed directly
  to the public Internet.
