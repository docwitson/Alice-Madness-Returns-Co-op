# Known issues and recovery

This page describes alpha limitations that are not necessarily regressions.

Alpha.2 received extensive playtesting. Alpha.3 mainly updates visuals, animation,
audio and UI and has had more limited testing. Enemy synchronization and
interaction logic were not intentionally changed, but regressions remain possible.
The previous Alpha.2 release remains available; both games and the relay must
use that version if rolling back.

## Scripted encounters and bosses

Boss phases, miniboss introductions and scripted waves can advance at different
times in the two local simulations. If one player has no valid target or the
other is already waiting in a cutscene:

1. bring both players to the same area with `P` or client `O`;
2. wait a few seconds for host enemy state reconciliation;
3. use the `L` force action only if both players are stuck;
4. restart the checkpoint if the encounter still cannot progress.

## Interactions and world mechanisms

Many `C` interactions synchronize their cinematic and completed state. Some
switches, pig snouts, steam vents, platforms and secret-room scripts remain
local and may need to be activated by both players.

## Cutscenes

Alice Co-op delays selected trigger cinematics until the peer reaches the trigger.
A short fade or an enemy running in place can occur while waiting. The remote
proxy is hidden heuristically; forced cinematics can show a simplified Alice or
the wrong costume.

## Visual proxy

Alpha.3 implements native cloth/hair simulation, upper-body aiming, attack
blending, clock-bomb rabbit animations and projectile impact effects.

- A transient T-pose was reported during a Cardbridge join but could not be
  reproduced in subsequent tests. No speculative animation change is included.
- Remote particles are presentation-only; world/script state can still differ.
- Partner weapon audio is spatial. Unsupported sound argument expressions or
  cues absent from the client's loaded assets can leave some events silent.

The VFX guard bounds native proxy glide effects and clears orphan emitters on
map/proxy transitions. Optional frozen movement trails have a separate bounded
pool and can be disabled from the native pause panel with `T`.

## Joining and teleporting

Join loads the host's saved subarea/checkpoint through native Continue using
an isolated copy of the host session data. The client's selected profile is
preserved. The host must enter a level before Join becomes available.

`P` goes to the partner (to the host on a client). Client `O` retries a distant
rendezvous and host `O` summons the client. Host-visible sublevels are prepared
before arrival; airborne destinations are supported. A timeout leaves a retry
hint. If streaming or scripts still fail to settle, reload the checkpoint.

## Profiles and progression

Session weapon levels are mirrored, but UI availability and aiming depend on
profile unlock flags loaded by the game. Use a dedicated client profile and the
verified host-save synchronization action. This intentionally copies host
checkpoints, collectibles, statistics and weapon progression.

## Solo minigames

Side-scrolling ship stages run separately for each player. Shared-world,
cutscene-barrier and teleport behavior is suspended while the solo mode is
detected. Scores are not synchronized.

## Performance and crashes

The game is 32-bit. Two processes, PhysX, high-resolution textures and long
sessions can exhaust address space or GPU memory. Prefer 60 FPS, avoid excessive
diagnostic traces, and restart both processes between long chapters if memory
pressure becomes visible.
