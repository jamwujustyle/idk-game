# SAMURAI

A compact Unreal Engine 5 C++ sword-combat prototype: a lone fighter, a stone courtyard, and an opponent built around readable attacks, guard pressure, and timed counters.

The planned campaign, [The Drowned Road](docs/STORY.md), follows a samurai retainer confronting the consequences of an order he once obeyed.

**Status: combat prototype under active development.** The SAMURAI branding build compiles; original armored character integration and runtime checks remain in progress. See the [naming and build verification](reports/2026-10-16-samurai-rename.md) and the [last rendered combat verification](reports/2026-10-16-combat-reset.md) for their respective evidence and remaining playtest work.

## Start

Install the official Unreal Engine 5.7 Linux build and its native toolchain, then follow [SETUP.md](docs/SETUP.md). With `UE_ROOT` pointing to the extracted engine:

```sh
./scripts/unreal.sh setup
./scripts/unreal.sh build
./scripts/unreal.sh bootstrap
./scripts/unreal.sh play
```

The bootstrap creates `/Game/Maps/Arena`, an opaque material with runtime color/roughness/metallic parameters, and five original synthesized sound effects. The game mode constructs the arena and fighters when play begins. Existing generated assets are preserved when bootstrap runs again.

The fighters require the licensed **Paragon: Kwang** pack, installed separately through Fab. Its raw assets are not included in this repository. The courtyard's CC0 environment assets are included. See [asset sources and setup](docs/ASSETS.md).

## Controls

The opening screen waits for **Enter** or **A** before starting the duel. Combat and enemy movement remain stopped while assets and shaders load. Press **H** or **X** to see controls; help starts hidden. During a duel, pause or help shows performance and duel statistics.

| Action | Keyboard / mouse | Gamepad (Xbox-style labels) |
| --- | --- | --- |
| Start the first duel | Enter | A |
| Move / camera | W A S D / mouse | Left / right stick |
| Light attack | Left mouse | RB |
| Heavy attack | E | RT |
| Hold guard; time the press to parry | Right mouse | LB |
| Dodge | Space | B |
| Execute a vulnerable opponent | F | Y |
| Flow / Stone / Gale stance | 1 / 2 / 3 | D-pad left / up / right |
| Toggle target lock | Tab | Right-stick click |
| Rematch after starting | R | Select / View |
| Pause / resume | Escape | Start / Menu |
| Cycle quality setting | F2 while paused | A while paused |
| Cycle frame limit | F3 | Y while paused |
| Cycle rumble intensity: off / 35% / 70% / 100% | F4 | D-pad down |
| Toggle help | H | X |

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

The first slice combines scanned environment assets, project-authored courtyard structures, and original synthesized audio. Character integration, combat tuning, visual review, and packaged-build verification remain in progress. Multi-round progression is not implemented.
