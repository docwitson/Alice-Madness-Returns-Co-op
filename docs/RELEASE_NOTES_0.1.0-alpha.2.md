# Alice Co-op 0.1.0-alpha.2

Launcher, installation and stability update. The network protocol remains
version 29; gameplay synchronization is largely unchanged from alpha.1.

## Download

### Recommended — launcher installer

[**Download AliceCoop-0.1.0-alpha.2-installer.zip**](https://github.com/docwitson/Alice-Madness-Returns-Co-op/releases/download/v0.1.0-alpha.2/AliceCoop-0.1.0-alpha.2-installer.zip)

Extract the archive to any folder, run `AliceCoopLauncher.exe`, select the game,
and choose **Install**. The launcher handles hosting, joining, the relay and
normal connection settings.

### Advanced — manual drop-in

[Download AliceCoop-0.1.0-alpha.2-drop-in.zip](https://github.com/docwitson/Alice-Madness-Returns-Co-op/releases/download/v0.1.0-alpha.2/AliceCoop-0.1.0-alpha.2-drop-in.zip)

Use this only for manual drag-and-drop installation, BAT/INI launching or
development. Extract it directly beside `AliceMadnessReturns.exe`.

[SHA-256 checksums](https://github.com/docwitson/Alice-Madness-Returns-Co-op/releases/download/v0.1.0-alpha.2/AliceCoop-0.1.0-alpha.2-SHA256SUMS.txt)

![Alice Co-op launcher](https://raw.githubusercontent.com/docwitson/Alice-Madness-Returns-Co-op/v0.1.0-alpha.2/docs/media/alice-co-op-launcher.jpg)

## What's new

- Graphical installation, repair and removal for Steam, EA App and custom game
  copies.
- Guided **Host Game** and **Join Game** setup with address copying, connection
  testing, firewall setup and user-facing session status.
- Multiple saved game installations and isolated settings for each launcher
  window.
- Cleaner packages: normal installations copy only the runtime payload; manual
  scripts and documentation stay under `Advanced`.
- Fixed overlay placement after resolution or window-size changes.
- Fixed relay clients timing out while a game is still starting.
- Completed internal code reorganization, automated package verification and
  lifecycle diagnostics without changing protocol 29.

## Updating from alpha.1

1. Close the game and relay on both PCs.
2. Download and extract the recommended installer above.
3. Run the new launcher, select the existing game installation and choose
   **Install** or **Repair**.
4. Update both players to alpha.2 before connecting.

The installer preserves customized `AliceCoop.ini` and `MadnessPatch.ini` and
backs up an existing `dinput8.dll` when needed.

## Before playing

- Back up saves and use a dedicated client co-op profile.
- Use localhost, a trusted LAN or a trusted VPN; do not expose the relay directly
  to the public Internet.
- Existing alpha limitations in scripted encounters, remote visuals and locally
  simulated world mechanisms still apply.

[Launcher installation guide](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.2/docs/INSTALL.md) ·
[Manual installation guide](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.2/docs/MANUAL_INSTALL.md) ·
[Known issues](https://github.com/docwitson/Alice-Madness-Returns-Co-op/blob/v0.1.0-alpha.2/docs/KNOWN_ISSUES.md)
