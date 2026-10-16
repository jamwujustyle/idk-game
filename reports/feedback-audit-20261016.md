# Audio and controller feedback audit — 2026-10-16

## Observed audio state

- The default PipeWire sink, Built-in Audio Analog Stereo, was muted at volume 1.
- Unmuted that sink without changing volume. A subsequent `wpctl get-volume @DEFAULT_AUDIO_SINK@` returned `Volume: 1.00`, without `MUTED`.
- The desktop game launcher calls `scripts/unreal.sh play -fullscreen`; it does not add `-nosound`.
- Normal-play logs show the SDL pulseaudio backend and the built-in analog stereo output initializing successfully. NullRHI tests and the motion capture explicitly use `-nosound`; other unattended captures may select SDL's dummy output.
- All five combat sound assets are present. Their original mono WAV files have a peak amplitude of 0.82 and RMS levels from approximately -13 to -22 dBFS. They contain audible-range, nonzero samples.
- The final interactive build was launched fullscreen with normal audio enabled. Its log confirms the SDL pulseaudio backend, Built-in Audio Analog Stereo output, and 5/5 combat cues loaded with engine sound enabled. Perceived gameplay loudness is not verified.

## Controller routing and strength

- The connected USB Xbox Series controller advertises force-feedback capability through Linux input metadata.
- UE's Linux backend routes standard force feedback by the local player's input index. The project now accepts real gamepad events already delivered to that controller without requiring a second exact user/device mapping. Known disconnects, focus loss, keyboard switching, and unattended runs still silence motors.
- Default and local saved rumble strength are now 100%. Parry uses a stronger double pulse; actual critical contact has its own Counter envelope. The strength control previews the selected level without a camera impulse.
- The engine's SDL haptic backend uses a 32767 magnitude multiplier. The project still uses that standard backend; full game intensity is not a measurement of physical motor maximum.
- Added first-input and first-submission diagnostics so the next interactive run can distinguish input reception from UE feedback submission. Neither log claims a motor response.

## Validation and remaining observation

- Portable haptic tests: 2,875 checks passed, including bounded output, stronger distinct profiles, Counter arbitration, parry separation, and cleanup.
- The subsequent Development Editor build succeeded. All eight Unreal integration tests passed in [run-20261016T122252Z-mVz6C2](unreal-tests/run-20261016T122252Z-mVz6C2/index.html): 8 succeeded, 0 succeeded with warnings, 0 failed, 0 skipped/not run, and 0 incomplete. The [JSON report](unreal-tests/run-20261016T122252Z-mVz6C2/index.json) and [console log](unreal-tests/run-20261016T122252Z-mVz6C2/console.log) preserve the result.
- Passing tests are AnimationTiming, Assets, ClassDefaults, CookConfiguration, ImpactCamera, InputCoverage, LiveWorld, and LockOnCamera. Revised defense timing, usable earned counters, bounded focus framing, cleanup, recoil, and jump/traversal are covered by those tests.
- The Unreal run used NullRHI and `-nosound`. It verifies software integration, not rendered presentation, audible gameplay, or physical motor response. The interactive game was subsequently opened, but no new physical sound/rumble acceptance result is recorded here; GPU captures are separate evidence.
