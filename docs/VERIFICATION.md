# Verification

Keep observed results in dated reports under `reports/`. The [initial verification report](../reports/2026-10-16-verification.md) , [visual integration report](../reports/2026-10-16-visual-integration.md), and [combat correction report](../reports/2026-10-16-combat-reset.md) distinguish automated checks, rendered evidence, and remaining live acceptance work. Automated integration results alone do not establish GPU appearance, audible playback, gameplay rumble, or a working packaged build.

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

The eight registered tests in `Source/Ronin/Private/Tests/RoninAutomationTests.cpp` check:

| Test | Integration covered |
| --- | --- |
| ClassDefaults | Unreal constructs fighter/arena/game-mode classes. The original Samurai body uses the native animation graph and preserves the 34 cm radius / 88 cm half-height capsule. The original katana and saya attach to the intended bones with valid grip/orientation transforms; the inactive reusable blade trail attaches to the katana. Camera and floor collision defaults remain valid. |
| Assets | The arena map, combat material and five sound cues load. The textured `SK_Samurai` has adult human bounds, the required Mixamo/weighted wrist bones, and all 20 retargeted full-pose clips on its skeleton. Original katana/saya bounds are valid, blade-trail sockets match the exposed blade, and the selected Cascade effect contains an animation-trail emitter. |
| AnimationTiming | Every stance/attack duration maps windup, active and recovery onto continuous authored clip boundaries; elapsed times clamp safely, including instantaneous phases. |
| ImpactCamera | Contact offsets remain bounded and movement produces no shake. Successful-defense focus pushes are visible in the computed view, remain within their displacement/rotation/FOV limits at 30/60/120 Hz, settle to the original view, and clear immediately on reset. |
| InputCoverage | Merged `UInputSettings` contains usable keyboard/mouse and gamepad action coverage, including sprint and jump, plus effective movement/camera axes. It checks device reachability without duplicating physical button choices. |
| CookConfiguration | Startup map/game mode resolve and packaging includes the arena plus runtime-loaded audio/material directories. Reflected defaults retain hard references to the original Samurai, katana/saya, retargeted clips, licensed blade trail and contact-spark source. |
| LiveWorld | A temporary game world exercises real spawn, possession, collision and bound input actions. Coverage includes the start gate, interruptible attacks, swept dodge travel, five-cut sequences, authored weapon motion, pose/clock freezing, and rematch resets. Real incoming contacts trigger parry/dodge slow motion, preserve usable counters, and clean up on pause/rematch. Enemy blocks and strong hits produce bounded recoil with wall collision. Sprint, jump, midair pause/resume, landing during attacks, gate traversal and outer-terrain support are exercised. Trails and transient contact sparks follow combat lifecycle without modifying source effects. |
| LockOnCamera | Equivalent wrapped pitches produce the same view; yaw crosses its wrap seam by the short route. Repeated retreat updates remain within playable pitch bounds, settle toward the intended angle, and handle coincident targets without invalid rotation. |

Cinematic defense and recoil checks run within `LiveWorld`; bounded focus
framing is checked by `ImpactCamera`.

The live-world test uses the same actor startup as the empty arena map, with a controller supplied by the game mode's factory. It invokes installed input delegates; it does not simulate physical device events or a viewport. Cook reference checks do not prove a packaged build contains every asset. NullRHI does not activate Cascade emitters, so visible trail emission, frozen ribbons, fading tails, contact-spark appearance/placement and particle cleanup on rematch require a GPU pass. It also does not test frame time, audio playback or physical controller rumble. The suite is guarded by `WITH_DEV_AUTOMATION_TESTS` and registered for the editor context.

The runner follows Epic's [automation command-line workflow](https://dev.epicgames.com/documentation/en-us/unreal-engine/run-automation-tests-in-unreal-engine) and verifies the documented [JSON results and completion log](https://dev.epicgames.com/documentation/en-us/unreal-engine/review-test-results-in-unreal-engine).

## Live acceptance pass

Use the selected Xbox controller and the keyboard/mouse in a real game window, then repeat the essential pass with a packaged Linux build. Keep findings in a dated report under `reports/`; include the engine version, build revision, display resolution, GPU, input device, and any unresolved issue.

1. Confirm NVIDIA device selection in the log, 16:10 presentation, visible HUD, a stable floor, and both fighters spawning without errors.
2. Check camera/lock-on, movement, sprint, jump, gate traversal, five-cut chains, heavy attacks, guard/parry, earned counters, dodge, stance changes, execution, pause, and restart using both input methods. Inspect enemy bracing/recoil and the successful-defense focus push in motion.
3. Check Xbox button prompts, stick dead zones, paused quality/frame-limit controls, and the rumble intensity cycle; confirm feedback stops on pause/restart/disconnect and does not stick on.
4. Listen for slash, clash, hit, dodge, and execution cues. Check impact feedback and frame time under combat load at each quality setting.
5. Build with `./scripts/unreal.sh package-linux`, launch the archived executable outside the editor, and repeat spawn, controls, collision, material/audio, and rumble checks.

A successful automated report and a successful live/package pass are separate outcomes and should be recorded separately.
