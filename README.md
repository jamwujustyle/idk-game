# SAMURAI

A Linux Unreal Engine 5 C++ sword-combat prototype with an original armored samurai, a courtyard duel, and a walkable forest valley. Combat combines a five-cut chain, reactive blocking, timed parries, perfect dodges, and critical counters.

The planned campaign, [The Drowned Road](docs/STORY.md), follows a samurai retainer confronting the consequences of an order he once obeyed.

**Status: playable prototype under active development.** The original character is integrated. See the [combat and traversal verification](reports/combat-traversal-20261016.md) for the current build, automated checks, actual rendered sequences, and remaining limitations. Visual polish, physical gameplay feedback, and packaged-build acceptance still need work; this is not a finished AAA game.

## Start

Install the official Unreal Engine 5.7 Linux build and its native toolchain, then follow [SETUP.md](docs/SETUP.md). With `UE_ROOT` pointing to the extracted engine:

```sh
./scripts/unreal.sh setup
./scripts/unreal.sh build
./scripts/unreal.sh bootstrap
./scripts/unreal.sh play
```

The bootstrap creates `/Game/Maps/Arena`, an opaque material with runtime color/roughness/metallic parameters, and five original synthesized sound effects. The game mode constructs the arena and fighters when play begins. Existing generated assets are preserved when bootstrap runs again.

The fighters use the original Samurai mesh and katana. Combat motion is locally retargeted from the licensed **Paragon: Kwang** pack, installed separately through Fab; its raw assets and derived animation files are not included in this repository. The courtyard's CC0 environment assets are included. See [asset sources and setup](docs/ASSETS.md).

## Controls

The opening screen waits for **Enter** or **A** before starting the duel. Combat and enemy movement remain stopped while assets and shaders load. Press **H** or **X** to see controls; help starts hidden. During a duel, pause or help shows performance and duel statistics.

| Action | Keyboard / mouse | Gamepad (Xbox-style labels) |
| --- | --- | --- |
| Start the first duel | Enter | A |
| Move / camera | W A S D / mouse | Left / right stick |
| Toggle sprint while moving | Left Shift | Left-stick click (L3) |
| Jump | Space | A |
| Light attack / five-cut combo | Left mouse | RB |
| Heavy attack | E | RT |
| Hold guard; time the press to parry | Right mouse | LB |
| Dodge | Left Ctrl | B |
| Execute a vulnerable opponent | F | Y |
| Flow / Stone / Gale stance | 1 / 2 / 3 | D-pad left / up / right |
| Toggle target lock | Tab | Right-stick click |
| Rematch after starting | R | Select / View |
| Pause / resume | Escape | Start / Menu |
| Cycle quality setting | F2 while paused | A while paused |
| Cycle frame limit | F3 | Y while paused |
| Cycle rumble intensity: off / 35% / 70% / 100% | F4 | D-pad down |
| Toggle help | H | X |

Press light attack again during a cut or its recovery to queue the next cut;
each press adds at most one follow-up, and the fifth cut finishes the chain.
Sprint is a toggle: stopping, guarding, attacking, dodging, or rematching cancels
it. A successful timed parry or in-range perfect dodge triggers a brief cinematic
slowdown and opens a single-use critical counter with the next light or heavy
attack. See [combat behavior](docs/COMBAT.md) for timing and tuning.

Jump is available while grounded and idle, with guard released, after the duel
starts. Movement, sprint, and jump remain available after victory. Follow the
paths through either courtyard exit to explore the [forest valley](docs/WORLD.md).
Target lock releases when the opponent dies or is more than 9 m away; use Tab
or right-stick click to lock on again when nearby. This is one duel and an
explorable valley, with the campaign still a proposal.

Xbox prompts appear after gamepad input. Rumble uses standard force feedback with distinct combat patterns, a saved intensity setting, and cleanup on pause, rematch, focus loss, and controller disconnection. USB recognition and a physical driver-level rumble pulse are verified on an Xbox Series X|S controller; gameplay feedback still needs a hardware playtest.

## Project layout

The game's displayed name is **SAMURAI**. `Ronin.uproject`, the `Ronin` module,
build targets, console commands, and settings keys are stable internal identifiers
used by existing assets and development tools.

- `Source/Ronin`: combat rules, fighters, game mode, and HUD.
- `Config`: input, rendering, maps, and packaging defaults.
- `scripts`: engine commands, asset bootstrap, audio synthesis, and environment checks.
- `docs/SETUP.md`: installation and build/run instructions.
- `docs/ENVIRONMENT.md`: dated observations from the development laptop.

The first slice combines scanned environment assets, project-authored courtyard structures, and original synthesized audio. Animation refinement, combat tuning, environment detail, and packaged-build verification remain in progress. Multi-round progression is not implemented.
