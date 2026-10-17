# Alice Co-op 0.1.0-alpha.3.1 — Window Management Hotfix

This hotfix supersedes [Alpha.3](https://github.com/docwitson/Alice-Madness-Returns-Co-op/releases/tag/v0.1.0-alpha.3).

- Fixed the game repeatedly snapping back to a fixed window size and position
  when launched in Windowed mode.
- Borderless now uses the native MadnessPatch monitor-resolution viewport.
- Display mode is saved immediately in the launcher, even when closing without
  starting a game. In Fullscreen and Windowed modes, in-game display changes
  are no longer forced back to the startup preference. Borderless explicitly
  keeps a windowed viewport at the monitor resolution.

## Updating

Close the game and old relay. Extract the new installer, run the launcher and
choose **Install/Repair**. Opening the new launcher alone does not update the
installed DLL. Existing configuration and saves are preserved. For manual
installation, replace the mod files with the new drop-in package.

No gameplay or network protocol changes. Protocol **43** remains unchanged;
public Alpha.3 and Alpha.3.1 are compatible. Alpha.2 and test builds using a
different protocol remain incompatible. Both players are encouraged to update.

The original Alpha.3 archives and tag remain available unchanged.
