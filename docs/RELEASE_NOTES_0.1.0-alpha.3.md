# Alice Co-op 0.1.0-alpha.3

[Watch the Alpha.3 showcase](https://www.youtube.com/watch?v=I5nUTZjpCXY)

Alpha.3 focuses on the remote Alice's presentation and the multiplayer interface,
built on MadnessPatch 3.3.3 by Wemino and contributors.

## What's new

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

Detailed animation, cosmetic and world traces and movement-trail history are
disabled by default. Normal connection, travel, recovery and error logs remain
available. The first-sound crash found in the private audio build is fixed.

## Updating and joining

**Protocol 43: update both game DLLs and the relay together.** Alpha.2 uses
protocol 29; earlier Alpha.3 test builds using other protocols are incompatible.

1. Close both games and the old relay.
2. Extract `AliceCoop-0.1.0-alpha.3-installer.zip` completely.
3. Run its `AliceCoopLauncher.exe` and choose Install/Repair for the game copy
   actually being launched on each PC. Opening the new launcher alone does not
   replace an already installed DLL.
4. Use the relay supplied beside that launcher and keep the launchers open.
5. The host starts or continues a level. The client selects **Multiplayer >
   Join host** once the host is ready.

Join uses an isolated session copy of the host's checkpoint and keeps the client's
selected profile. **Receive host profile** is a separate replacement action with
explicit confirmation and a backup. Back up saves and use a dedicated co-op profile.

Advanced users can use the drop-in archive beside `AliceMadnessReturns.exe`.
See [installation](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.3/docs/INSTALL.md), [manual installation](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.3/docs/MANUAL_INSTALL.md), and
the [changelog](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.3/CHANGELOG.md).

## Testing status

Alpha.2 received extensive playtesting. Alpha.3 mainly updates visuals, animation,
audio and UI and has not received the same depth of testing. Enemy synchronization
and interaction logic were not intentionally changed, but regressions remain
possible. Alpha.2 remains available if you need to return to the previous build;
roll back both players and the relay together.

The fixed audio build passed a user-reported two-player test on 2026-10-16.
Automated ABI, protocol, menu and package checks also passed. This does not establish
full weapon/level coverage or long-session stability. Known scripted encounters,
bosses and locally simulated world mechanisms retain alpha limitations. See
[known issues](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.3/docs/KNOWN_ISSUES.md) and the dated
[validation record](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.3/docs/RELEASE_VALIDATION_0.1.0-alpha.3.md).

Created by Docwitson. Built on MadnessPatch by Wemino and contributors.
Open-source project, GPL-2.0-only; see LICENSE and THIRD_PARTY_NOTICES.md.
