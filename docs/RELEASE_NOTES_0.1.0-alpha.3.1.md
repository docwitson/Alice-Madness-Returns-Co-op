# Alice Co-op 0.1.0-alpha.3.1 — Display Hotfix

Fixes unwanted window resizing and cropped or squeezed menus, gameplay and video
cutscenes when the fullscreen resolution has a different aspect ratio from the
desktop, such as 1920x1080 on a 3440x1440 display.

- Window size and position no longer repeatedly snap back to old settings.
- Fullscreen menus, pause, 3D scenes and video cutscenes use the current render
  dimensions. Borderless resolution changes keep rendering and mouse input aligned.
- The launcher saves the selected display mode immediately and applies startup
  preferences once, leaving later in-game resolution changes available.

## Updating

Close the game and old relay. Extract `AliceCoop-0.1.0-alpha.3.1-installer.zip`, run
its launcher and choose **Install/Repair** for the game installation you use.
Opening the new launcher alone does not update the installed DLL. Existing
configuration and saves are preserved. Advanced users can replace the mod files
with the drop-in package.

Protocol **43** is unchanged; public Alpha.3 and Alpha.3.1 are compatible. Both
players are encouraged to update. Alpha.2 and test builds using a different
protocol remain incompatible. Gameplay synchronization and partner audio are
unchanged from public Alpha.3.

This hotfix supersedes [Alpha.3](https://github.com/docwitson/Alice-Madness-Returns-Co-op/releases/tag/v0.1.0-alpha.3).
The original Alpha.3 archives and tag remain available unchanged.

## Included from Alpha.3

[Watch the Alpha.3 showcase](https://www.youtube.com/watch?v=I5nUTZjpCXY)

Alpha.3 focuses on the remote Alice's presentation and the multiplayer interface,
built on MadnessPatch 3.3.3 by Wemino and contributors.

- Improved partner movement and weapon animations: melee/dodge transitions,
  ranged upper-body aiming and animation timing.
- Native cloth and hair simulation for the remote Alice, with recovery across
  visibility changes, cinematics and proxy recreation.
- Restored weapon trails, pepper/teapot projectiles, impact effects and decals,
  hobby-horse ground shockwaves and the clock-bomb rabbit presentation.
- Spatial partner weapon audio, including animation sound events, native
  weapon/projectile sounds and tracked clock-bomb/weapon loops.
- Multiplayer in the original game menu, with role-specific actions, session
  status, confirmed profile replacement, About and external project links.
- Matching native pause/cutscene notices, a transparent watching indicator and
  a compact top-center waiting reminder using the `L` hotkey.
- Join loads the host's saved checkpoint/subarea. The client prepares streamed
  areas before arriving; `P`/`O` also support distant and airborne destinations.
- MadnessPatch 3.3.3 and Windows version metadata for the launcher, relay and DLL.

## Testing and documentation

The final display-fix candidate passed user manual checks on 2026-10-17, including
the previously affected fullscreen pause menu and video cutscenes. Debug/Release,
native menu, protocol and package checks also passed. This is not a new full
campaign playtest: Alpha.2 received more extensive testing, and known scripted
encounter, boss and world-mechanism limitations remain.

[Installation](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.3.1/docs/INSTALL.md)
· [Manual installation](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.3.1/docs/MANUAL_INSTALL.md)
· [Known issues](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.3.1/docs/KNOWN_ISSUES.md)
· [Changelog](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.3.1/CHANGELOG.md)
· [Validation record](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.3.1/docs/RELEASE_VALIDATION_0.1.0-alpha.3.1.md)

Created by Docwitson. Built on MadnessPatch by Wemino and contributors.
Open-source project, GPL-2.0-only; see LICENSE and THIRD_PARTY_NOTICES.md.
