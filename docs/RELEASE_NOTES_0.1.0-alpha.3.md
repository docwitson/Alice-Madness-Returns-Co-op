# Alice Co-op 0.1.0-alpha.3

Local release candidate. No public tag or release has been created.
Both players and the relay must use this build: protocol 42 is incompatible
with earlier protocols.

## Changes

- Updated MadnessPatch to 3.3.3.
- Native Multiplayer menus with role-aware actions, profile confirmation,
  About, and links to source/releases and community pages.
- Native fonts, compact translucent pause/cutscene notices and a transparent
  watching indicator. Waiting uses a top-center L hotkey reminder.
- Native proxy cloth and hair, improved dodge/melee transitions and ranged
  upper-body aiming; restored projectile impacts/decals, ground shockwaves
  and the clock-bomb rabbit.
- Join loads the host checkpoint/subarea through isolated session files without
  replacing the client profile. P/O prepare streamed areas before teleporting.
- Fixed native Chapter select/Memories entry and return, menu presentation,
  focus and music initialization.
- Detailed traces and movement-trail history are disabled by default. Normal
  connection, travel and error logs remain available for troubleshooting.
- Windows version metadata for launcher, relay and DLL.

## Installation

Extract the installer archive, run AliceCoopLauncher.exe and install/repair the
selected game. Close both games before updating. Update both players and the
relay together. Back up saves and use a dedicated client co-op profile.

Choose roles and server address in the launcher. The host starts or continues
a level, then the client chooses Multiplayer > Join host. Receive host profile
is a separate, explicitly confirmed replacement action with a backup.

Advanced users can install the drop-in archive beside AliceMadnessReturns.exe.
The source ZIP and SHA-256 file accompany the local candidate.

## Validation and limitations

See RELEASE_VALIDATION_0.1.0-alpha.3.md and KNOWN_ISSUES.md. Automated checks
cannot confirm the final notice placement or long-session cloth recovery.
The previously reported Cardbridge T-pose has not been reproduced again.
Scripted encounters and local world mechanisms retain alpha limitations.
VirusTotal results from earlier binaries do not apply to this new DLL; another
scan of the final candidate is pending and requires the owner's decision.

Created by Docwitson. Built on MadnessPatch by Wemino and contributors.
Open-source project, GPL-2.0-only; see LICENSE and THIRD_PARTY_NOTICES.md.
