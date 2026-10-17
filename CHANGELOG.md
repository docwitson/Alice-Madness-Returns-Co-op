# Changelog

All notable user-facing changes are documented here. Alice Co-op follows
[Semantic Versioning](https://semver.org/) where practical; compatibility also
depends on the protocol version shown in each release.

## 0.1.0-alpha.3.1

### Fixed

- Windowed launches no longer repeatedly resize or reposition the game using
  legacy INI dimensions. In-game resolution and fullscreen changes remain usable.
- Borderless starts at the monitor resolution, then accepts in-game resolution
  changes without forcing a different render-target size behind the window/input
  layout.
- Native menu viewport and scaling are preserved instead of applying a co-op
  ExactFit override that cropped or misaligned menus on ultrawide displays.
- Fullscreen menus (including pause and HUD), 3D scenes and video cutscenes use
  the actual render aspect instead of the cached desktop aspect. Selecting
  1920x1080 on a 3440x1440 desktop no longer crops menus or squeezes the scene/video.
- The launcher saves the selected display mode immediately, including when
  closed without starting a session.

The DLL and launcher are updated: close the game and use **Install/Repair**.
Protocol **43** is unchanged; this hotfix remains compatible with public Alpha.3.

## 0.1.0-alpha.3

### Added

- Native spatial proxy weapon audio: animation sound notifies and actual
  weapon/projectile/clock-bomb sound calls, including tracked audio components.
  Sustained audio uses start/update/stop events and a bounded keepalive timeout.

- Windows VERSIONINFO for the native DLL and relay server, including product,
  description, authorship and alpha.3 version metadata.
- MadnessPatch 3.3.3, including high-FPS hair/cloth interpolation, language
  selection, borderless mode and optional loose-file mod loading. Asset dumping
  and loose-file loading are disabled by default.
- Native Scaleform Multiplayer menus with the game's fonts, button templates
  and mouse/keyboard/controller navigation. Actions depend on the host/client
  role; About includes project credits and selectable social/source links.
- Native Canvas co-op notices and pause controls with thin translucent frames
  matching Multiplayer, game fonts and controller glyphs, replacing the ImGui/D3D9
  overlay. The cutscene eye uses the mod's transparent icon. No game assets or
  fonts are bundled.

### Fixed

- Proxy audio playback-state queries now reserve the full four-byte UE3 boolean
  result, preventing stack corruption and a crash-handler freeze when audio starts.

- Upgrading an alpha.2 INI without native cloth/hair keys now enables the new
  simulation defaults; explicit zero values remain respected.

- Enlarged native menu GFX streams load completely: Chapter select, Memories
  and return transitions no longer lose interface elements or leave an empty menu.
- Join loads the host's saved subarea and checkpoint through native Continue
  using a verified isolated session cache, preserving the client's profile.
- P/O teleports preload host-visible sublevels before arrival, including distant
  and airborne destinations. Source-camera streaming volumes no longer cancel
  the preload; timeout and cancellation restore their original participation.
- Proxy cloth controller and skirt attachments recover after native death cleanup.
- Cinematic visibility changes defer cloth reattachment/reset until the proxy is
  visible again; recreated simulators discard stale high-FPS interpolation history.
- Join refreshes both native persistent-profile and checkpoint caches before
  Continue, rejecting inconsistent host/client chapter data before level travel.
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

- Protocol 43 carries proxy SoundCue start/update/stop events, distinguishes
  isolated Join transfers from explicit profile replacement, and includes
  host-visible streaming identities. Both games and the relay must be updated
  together; Alpha.2 (protocol 29) and earlier Alpha.3 test builds are incompatible.
- Profile replacement requires explicit confirmation, with Back last and selected
  by default. Obsolete L/K/Y/N main-menu shortcuts are removed; L remains available
  while waiting for a shared cutscene.
- Main-menu version/role status is hidden during gameplay. Co-op panels use native
  fonts, compact layouts and translucent frames; successful teleports stay quiet.
- Cutscene waits use two centered lines at the top with an L hotkey reminder,
  a lighter background and no extra button.
- Frequent animation, cosmetic, enemy-pose and cutscene diagnostic samples are
  opt-in through the existing trace settings; release defaults keep them disabled.

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
