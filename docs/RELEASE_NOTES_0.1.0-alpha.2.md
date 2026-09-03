# Alice Co-op 0.1.0-alpha.2

This alpha focuses on making Alice Co-op easier to install, launch and maintain
without changing protocol version 29 or substantially changing gameplay.

The release adds a graphical launcher and installer with support for multiple
game installations, guided host/join setup, connection checks, relay management
and compact session diagnostics. A cleaned-up direct drop-in package remains
available for manual installation and development.

It also includes the completed structural refactoring and safety infrastructure,
improved overlay placement after resolution changes, and a relay keepalive fix
for slower game startup.

Both players must use the same release. Back up saves and use a dedicated client
co-op profile. The relay is intended only for localhost, a trusted LAN, or a
trusted VPN. Existing alpha limitations in scripted encounters, remote visuals
and locally simulated world mechanisms still apply.
