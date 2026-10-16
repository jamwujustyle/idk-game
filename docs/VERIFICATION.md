# Verification

The Unreal Engine 5.7.4 editor build and asset bootstrap completed successfully on 2026-10-16. All five engine integration tests passed with zero errors and zero warnings in the [recorded Unreal results](../reports/unreal-tests/run-20261016T080105Z-U0gArY/index.json); the [console log](../reports/unreal-tests/run-20261016T080105Z-U0gArY/console.log) preserves the run output. See the [dated verification report](../reports/2026-10-16-verification.md) for the broader acceptance status. These automated results do not establish GPU rendering, audible playback, gameplay rumble, or a working packaged build.

## Fast checks without Unreal

```sh
./scripts/test-core.sh
./scripts/test-haptics.sh
bash -n scripts/unreal.sh scripts/check-environment.sh scripts/test-unreal.sh
python3 -m py_compile scripts/generate_audio.py scripts/bootstrap_project.py
```

The standalone suites cover combat simulation and rumble-envelope logic. Engine tests intentionally focus on integration rather than repeating their arithmetic.

## Engine integration tests

With `UE_ROOT` selected as described in [SETUP.md](SETUP.md):

```sh
./scripts/unreal.sh build
./scripts/unreal.sh bootstrap
./scripts/test-unreal.sh
```

The runner starts `UnrealEditor-Cmd` with NullRHI, audio output disabled, and the `Ronin` automation filter. It loads the Bink decoder module before the tests so SoundWave assets can initialize without an audio device. It stores a separate dated run under visible, non-ignored `reports/unreal-tests/`, preserving the console log and Unreal's JSON/HTML report. It fails if the process fails/times out, if any declared test is missing, skipped, incomplete or failed, or if the successful completion marker is absent. A zero exit code from an editor that never ran the suite is insufficient. `RONIN_TEST_TIMEOUT` changes the default 600-second process deadline.

The tests in `Source/Ronin/Private/Tests/RoninAutomationTests.cpp` check:

| Test | Integration covered |
| --- | --- |
| ClassDefaults | Unreal constructs fighter/arena/game-mode classes; meshes resolve; capsule and movement components exist; camera uses its spring arm and permits a 16:10 viewport; floor supports pawn collision at ground height; game mode selects the HUD and custom controller. |
| Assets | The actual arena map, opaque parameterized material with instanced-mesh usage enabled, and all five nonempty sound-wave assets load through Unreal. |
| InputCoverage | Merged `UInputSettings` contains usable keyboard/mouse and gamepad action coverage and effective gamepad movement/camera axes. It checks device reachability without duplicating physical button choices. |
| CookConfiguration | Startup map/game mode resolve and packaging includes the arena plus runtime-loaded audio/material directories. |
| LiveWorld | UE's `FTestWorldWrapper` starts the configured game mode in a temporary game world. Real BeginPlay spawns the arena and two fighters, possession installs pawn input, 102 fixed ticks check collision settling and pause, and camera transforms remain finite. Actual bound actions exercise stance, guard press/release, pause/resume, and light attack. The world is torn down after the test. |

The live-world test uses the same procedural startup as the empty arena map, with a controller supplied by the game mode's factory. It invokes installed input delegates; it does not simulate physical device events or a viewport. Cook configuration checks do not prove a packaged build contains every asset. NullRHI does not test GPU rendering, frame time, audio playback, or physical controller rumble. The suite is guarded by `WITH_DEV_AUTOMATION_TESTS` and registered for the editor context.

The runner follows Epic's [automation command-line workflow](https://dev.epicgames.com/documentation/en-us/unreal-engine/run-automation-tests-in-unreal-engine) and verifies the documented [JSON results and completion log](https://dev.epicgames.com/documentation/en-us/unreal-engine/review-test-results-in-unreal-engine).

## Live acceptance pass

Use the selected Xbox controller and the keyboard/mouse in a real game window, then repeat the essential pass with a packaged Linux build. Keep findings in a dated report under `reports/`; include the engine version, build revision, display resolution, GPU, input device, and any unresolved issue.

1. Confirm NVIDIA device selection in the log, 16:10 presentation, visible HUD, a stable floor, and both fighters spawning without errors.
2. Check camera/lock-on, movement, light/heavy attacks, guard/parry, dodge, stance changes, execution, pause, and restart using both input methods.
3. Check Xbox button prompts, stick dead zones, paused quality/frame-limit controls, and the rumble intensity cycle; confirm feedback stops on pause/restart/disconnect and does not stick on.
4. Listen for slash, clash, hit, dodge, and execution cues. Check impact feedback and frame time under combat load at each quality setting.
5. Build with `./scripts/unreal.sh package-linux`, launch the archived executable outside the editor, and repeat spawn, controls, collision, material/audio, and rumble checks.

A successful automated report and a successful live/package pass are separate outcomes and should be recorded separately.
