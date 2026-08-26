# Architecture

## Process model

Alice Co-op deliberately avoids enabling the unfinished UE3 online subsystem.
Instead, a normal session consists of three independent processes:

```text
Host game  <---- UDP ---->  AliceCoopServer  <---- UDP ---->  Client game
```

The relay assigns stable participant roles and forwards protocol messages. It
does not load game assets, simulate the world, or act as an authoritative game
server. The host game process remains authoritative for the synchronized subset
of world state.

## Client injection

`dinput8.dll` is a combined MadnessPatch/Alice Co-op proxy DLL loaded by the game.
The BAT scripts set per-process environment values for role, relay endpoint,
window policy and diagnostic modes before starting `AliceMadnessReturns.exe`.

## Protocol

The protocol in `src/Coop/Protocol.hpp` uses compact versioned UDP datagrams.
Its major state flows are:

- client input/action state to host;
- host player/world snapshot to client;
- evaluated animation graph samples in both directions;
- projectile and shared-world events;
- cutscene/lifecycle coordination;
- acknowledged profile-transfer chunks with SHA-256 verification.

Both participants must run the same protocol version.

## Remote player presentation

Each game spawns a presentation-only `AlicePawn` for the peer. It is not a full
network pawn and does not execute remote gameplay scripts. Alice Co-op applies
interpolated root transform, safe animation states, cosmetic components, weapon
presentation and bounded effects. Avoiding remote SpecialMove execution prevents
the proxy from mutating the locally controlled Alice or world scripts.

## Shared world

The host identifies nearby ordinary enemies and publishes stable state keys,
health, death, pose and target decisions. The client reconciles compatible local
actors. This works well for ordinary combat but cannot fully replace encounter-
specific Kismet state, which is why boss and scripted-event fallbacks remain.

Breakable props use shared events, while pickups intentionally remain local so
each player can collect their own resources.

## Cutscenes and lifecycle

Cutscene barriers advertise stable trigger/action keys and wait for peer
readiness. Interaction cinematics can be replayed locally without forcing the
remote pawn to walk through the world. Emergency activation is explicit and
visible because bypassing level script prerequisites can produce imperfect
cinematic actors.

Death, checkpoint restart, return to menu and peer disconnect have separate
lifecycle commands to prevent a stale proxy or world-authority state surviving
a process transition.

## Save isolation

Normal client checkpoint writes are redirected to `AliceCoop\client-saves`.
Explicit profile synchronization is transactional: receive to staging, verify
both files, back up the selected client profile, then replace. The relay does not
silently merge arbitrary save structures.
