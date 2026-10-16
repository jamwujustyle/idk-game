# RONIN

A compact Unreal Engine 5 C++ sword-combat prototype: a lone fighter, a stylized arena, and an opponent built around readable attacks, guard pressure, and timed counters.

**Status: source prototype. Unreal compilation, rendering, gameplay testing, and Linux packaging are pending.** The repository contains the gameplay source and reproducible asset-generation tools; it does not contain a verified playable build.

## Start

Install the official Unreal Engine 5.7 Linux build and its native toolchain, then follow [SETUP.md](docs/SETUP.md). With `UE_ROOT` pointing to the extracted engine:

```sh
./scripts/unreal.sh setup
./scripts/unreal.sh build
./scripts/unreal.sh bootstrap
./scripts/unreal.sh play
```

The bootstrap creates `/Game/Maps/Arena`, an opaque material with runtime color/roughness/metallic parameters, and five original synthesized sound effects. The game mode constructs the arena and fighters when play begins. Existing generated assets are preserved when bootstrap runs again.

## Controls

| Action | Keyboard / mouse | Gamepad (Xbox-style labels) |
| --- | --- | --- |
| Move / camera | W A S D / mouse | Left / right stick |
| Light attack | Left mouse | RB |
| Heavy attack | E | RT |
| Hold guard; time the press to parry | Right mouse | LB |
| Dodge | Space | B |
| Execute a vulnerable opponent | F | Y |
| Flow / Stone / Gale stance | 1 / 2 / 3 | D-pad left / up / right |
| Toggle target lock | Tab | Right-stick click |
| Restart | R | Select / View |
| Pause / resume | Escape | Start / Menu |
| Cycle quality setting | F2 | A while paused |
| Cycle frame limit | F3 | Y while paused |
| Cycle rumble intensity: off / 35% / 70% / 100% | F4 | D-pad down |
| Toggle help | H | X |

Rumble uses standard gamepad force feedback where the controller and Linux driver expose it. Advanced DualSense haptics and adaptive triggers are not implemented. Controller recognition and physical rumble still require a hardware playtest.

## Project layout

- `Source/Ronin`: combat rules, fighters, game mode, and HUD.
- `Config`: input, rendering, maps, and packaging defaults.
- `scripts`: engine commands, asset bootstrap, audio synthesis, and environment checks.
- `docs/SETUP.md`: installation and build/run instructions.
- `docs/ENVIRONMENT.md`: dated observations from the development laptop.

The first slice uses procedural geometry and original synthesized audio. It still needs an Unreal compile/run pass, combat tuning, visual review, and packaged-build verification. Multi-round progression is not implemented.
