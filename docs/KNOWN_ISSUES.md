# Known issues and recovery

This page describes alpha limitations that are not necessarily regressions.

## Scripted encounters and bosses

Boss phases, miniboss introductions and scripted waves can advance at different
times in the two local simulations. If one player has no valid target or the
other is already waiting in a cutscene:

1. bring both players to the same area with `P` or client `O`;
2. wait a few seconds for host enemy state reconciliation;
3. use the yellow `L` force action only if both players are stuck;
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

- The remote dress is rigid instead of fully simulated cloth.
- Remote hair uses an independent approximation and has limited collision.
- Upper-body aiming and some weapon transitions do not exactly match the owner.
- Particles are presentation-only and may be simplified.
- Hat-bomb rabbit and some projectile visuals are incomplete.

The VFX guard bounds native proxy glide effects and clears orphan emitters on
map/proxy transitions. Optional frozen movement trails have a separate bounded
pool and can be disabled from the pause overlay with `T`.

## Joining and teleporting

Joining loads the host chapter/checkpoint but intentionally does not teleport
during the initial world construction. Wait until the client can move, then use:

- `P` when both players are on the same map and reasonably close;
- client `O` for emergency long-distance teleport.

Using `O` while a level or cinematic is still loading can place Alice in an
invalid location. Reload the checkpoint if that happens.

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
