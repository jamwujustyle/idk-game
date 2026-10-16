# Combat and traversal verification — 2026-10-16

## Delivered behavior

The original armored Samurai now has a five-cut light combo, enemy blocking and
swept recoil, and single-use critical counters earned by timed parries or perfect
dodges. A parry gives a short contact freeze followed by slow motion and bounded
closer camera framing. A perfect dodge slows the exchange without stopping it.
Real input, camera response, and controller envelopes remain on real time.

L3 / Left Shift toggles sprint while moving. A / Space jumps; B / Left Ctrl dodges.
The target camera normalizes wrapped pitch, and moving jumps preserve an upright
capsule. Combat movement preserves vertical velocity, including an airborne attack
and pause/resume. Successful victory retains movement with a compact banner.

The courtyard has open east/west exits onto a colliding 180 m square valley with
connecting paths, three rest sites, and visible outer escarpments. Target lock
releases beyond 9 m and the opponent stops pursuit beyond 8.5 m. There is one duel;
no additional encounters or campaign have been implemented.

The default audio output was muted; it was unmuted without raising the volume.
Game rumble is set to 100%, with stronger parry/critical envelopes and corrected
Linux input routing. See the [feedback audit](feedback-audit-20261016.md) for the
observed device state and the remaining physical feedback check.

## Build and automated verification

- Unreal Engine 5.7.4 Development Editor build succeeded, two compile actions.
- Portable combat checks: 950 passed. Portable haptic checks: 2,875 passed.
- [Final Unreal report](unreal-tests/run-20261016T122252Z-mVz6C2/index.html):
  8 succeeded, 0 with warnings, 0 failed, 0 not run, 0 incomplete.
- The latest LiveWorld checks include real bound actions, jump/landing, upright
  moving jumps, airborne attack and pause, terrain support, gate traversal,
  enemy reactions, counter contact after a dodge, and cinematic cleanup.
- NullRHI tests deliberately disable sound and do not prove visual quality,
  audible playback, or physical motor response.

## Rendered combat evidence

GPU: NVIDIA GeForce RTX 5060 Laptop GPU; Vulkan, 1600×1000. Recorded at a fixed
30 simulation frames per second after shaders settled, using the game's actual
actions/contact resolution. Autonomous enemy attack selection was suppressed for
repeatable capture; blocking, movement, collision, and reactions remained active.
Audio and hardware rumble were disabled for these unattended captures. This is
visual evidence, not a real-time performance measurement or manual playtest.

- [Parry and critical counter](visuals/parry-counter-20261016.mp4): 90 frames,
  one resolved parry, one earned counter, one confirmed damage contact.
- [Perfect dodge and counter](visuals/dodge-counter-20261016.mp4): 90 frames,
  one resolved dodge, one earned counter, one confirmed damage contact.
- [Five-cut chain](visuals/five-cut-combo-20261016.mp4): 180 frames, five distinct
  attack IDs and all five steps, with five actual blocked contacts.
- [Capture event log](visuals/combat-capture-results-20261016.log).

Sampled frame sequences were visually inspected. They show distinct cuts,
blocking, a sidestep, impact sparks, closer defense framing, and recoil. These
combat captures precede only the final upright-jump/victory-HUD correction and
exploration capture addition; the final build passed the complete suite afterward.

## Rendered traversal evidence

[Valley traversal](visuals/valley-traversal-20261016.mp4) records 240 frames of
actual bound forward/sprint input from the normal courtyard spawn, through the
open torii gate, onto the valley ground. The player was not teleported through
the gate. It covered 40.9 m, ending grounded at `(3789.8, 0, 86.8)` cm, with finite
coordinates and no invisible courtyard wall. The enemy was placed off the route;
its normal pursuit/collision remained active. The capture fixture supplied held
input while the controller's hardware-input tick was suspended.

Sampled traversal frames were inspected for visible continuity through the gate
and onto terrain. This confirms the exit works, not complete exploration quality
or performance throughout the valley. The final compact victory banner compiled
and preserves the tested post-victory traversal path, but was not GPU-captured.

## Interactive handoff

The updated build was opened fullscreen using the X11 backend, with normal audio
and physical controller input enabled. Startup confirmed all five combat sounds
loaded and the built-in audio device initialized. Hardware perception of the new
rumble/audio profiles remains unverified. No paid generation service was used
for this batch.

## Remaining limits

This is still a prototype, not AAA quality. The hands have no individual finger
bones, so the weapon grip remains approximate. Jump uses a procedural pose over
an existing clip, not an authored jump cycle. Animation blending, hand placement,
terrain/foliage detail, lighting, and the sense of weight need further art and
playtest iteration. The valley does not yet contain additional gameplay content.
Physical in-game rumble strength and audible combat still need a player check.
A packaged Linux release has not been verified; this build runs through Unreal's
game mode.
