# Changelog

All notable user-facing changes are documented here. Alice Co-op follows
[Semantic Versioning](https://semver.org/) where practical; compatibility also
depends on the protocol version shown in each release.

## Unreleased

### Added

- Windows VERSIONINFO for the native DLL and relay server, including product,
  description, authorship and alpha.3 version metadata.
- MadnessPatch 3.3.3, including high-FPS hair/cloth interpolation, language
  selection, borderless mode and optional loose-file mod loading. Asset dumping
  and loose-file loading are disabled by default.
- Native Scaleform Multiplayer menus with the game's fonts, button templates
  and mouse/keyboard/controller navigation. Actions depend on the host/client
  role; About includes project credits and selectable social/source links.
- Native Canvas co-op notices and pause controls with game frames and controller
  glyphs, replacing the ImGui/D3D9 overlay. No game assets or fonts are bundled.

### Fixed

- Join loads the host's saved subarea and checkpoint through native Continue
  using a verified isolated session cache, preserving the client's profile.
- P/O teleports preload host-visible sublevels before arrival, including distant
  and airborne destinations. Source-camera streaming volumes no longer cancel
  the preload; timeout and cancellation restore their original participation.
- Proxy cloth controller and skirt attachments recover after native death cleanup.
- Menu music initialization, full-viewport presentation, title/profile transition
  overlap and stale button focus after opening Multiplayer or confirmation.
- Proxy pepper and teapot projectile flight, impact particles and decals,
  including teapot splashes on walls.
- Hobby-horse ground shockwaves and melee trail effects, with native spawn
  positions and complete trail lifecycles.
- Dodge butterflies and fading while preserving the proxy dress and cloth
  simulation across visibility changes.
- Clock-bomb mesh placement, animation and rabbit reveal during the countdown.
- Proxy ranged upper-body aiming through the native combat stance and aim tree.
- Melee and dodge root displacement, animation phase correction and native
  transitions, including held attack poses and instant blend-in timing.

### Changed

- Protocol 42 distinguishes isolated Join session transfers from explicit profile
  replacement and carries host-visible streaming identities. Both games and the
  relay must use matching builds; older protocols are incompatible.
- Profile replacement requires explicit confirmation, with Back last and selected
  by default. Obsolete L/K/Y/N main-menu shortcuts are removed; L remains available
  while waiting for a shared cutscene.
- Main-menu version/role status is hidden during gameplay. Co-op panels use native
  fonts, compact layouts and translucent frames; successful teleports stay quiet.

## 0.1.0-alpha.2 — 2026-09-03

### Added

- A graphical launcher and installer for managing game installations, hosting,
  joining and common connection settings.
- Automated protocol, package and lifecycle checks used by development and CI.

### Changed

- Reorganized the AliceCoop client internals and isolated its ProcessEvent bridge
  without changing the wire protocol or intended gameplay behavior.
- Simplified the direct drop-in package and kept advanced/manual tools separate
  from the normal launcher workflow.

### Fixed

- Overlay placement after game-window resolution changes.
- Relay clients timing out while the game was still starting.

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
