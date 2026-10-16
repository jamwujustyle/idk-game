# Combat haptics

`HapticCore` shapes the large and small standard-rumble motors independently.
Unreal receives four normalized channels with mirrored large/small values. The
Linux Xbox backend combines them into one large and one small motor, so event
identity depends on frequency balance and timing, not left/right placement.
Adaptive triggers, trigger motors, and advanced DualSense haptics are not used.

## Event feel and interruption

Large-motor pulses emphasize weight; small-motor pulses emphasize a sharper
contact. Each motor has its own piecewise-linear envelope. Movement feedback is
brief and quiet so confirmed contacts stand out.

| Event | Duration, seconds | Priority | Pattern |
| --- | ---: | ---: | --- |
| Swing | 0.065 | 1 | Light, short treble pulse |
| Heavy swing | 0.095 | 1 | Heavier bass-led preparation |
| Dodge | 0.055 | 1 | Restrained movement tick |
| Hit | 0.14 | 2 | Landed hit: bass thud with a rapidly fading sharp edge |
| Block | 0.105 | 3 | Brief treble-led steel contact |
| Perfect dodge | 0.12 | 4 | Light, crisp confirmation of an incoming attack evaded in range |
| Damage | 0.24 | 5 | Received hit: stronger bass onset and a single decaying body |
| Parry | 0.18 | 6 | Sharp double tap with 35 ms of full silence between strikes |
| Guard break | 0.29 | 7 | Heavy initial crunch with a smaller rebound |
| Execute | 0.4 | 8 | Strong initial contact, separation, and a second impact |

One envelope plays at a time. A higher-priority event interrupts the current one;
a different equal-priority event replaces it. Lower-priority events and repeated
callbacks for the currently active event are discarded without queuing. This
keeps movement from filling the parry gap and repeated callbacks from sustaining
a continuous buzz. A new instance of the same event can start after it finishes.

## Delivery, camera, and lifecycle

The player controller submits feedback after action bindings and before Unreal's
force-feedback processing in the same frame, then advances the envelope. Dynamic
feedback uses a short renewed lease that also covers the current frame duration.
A stalled delivery loop therefore does not deliberately request endless rumble.

Confirmed contacts also produce a small additive camera impulse for either input
method. It affects the final view, leaving control rotation and lock-on intact.
The strongest event is bounded to 2.4 cm of displacement and 0.675 degrees of
pitch/FOV change. Swings and ordinary dodges do not shake the camera. Contact
impulses use the same priority rules and do not stack their amplitudes.

Global rumble intensity defaults to 0.7 and accepts values from 0 to 1. Each
trigger also accepts a strength multiplier from 0 to 1. Values are clamped;
negative and non-finite strengths produce no event. Non-finite global intensity
becomes zero. A positive intensity adjustment scales the current event without
restarting it. Zero intensity clears the event; raising it again cannot restore
an old pulse. Camera feedback is independent of the rumble intensity setting.

`SetEnabled(false)` and `Stop()` clear the current envelope while preserving
intensity. Re-enabling cannot resume an old vibration. Triggering while disabled
or at zero intensity is ignored. The controller clears rumble and camera feedback
on pause, rematch, focus loss, active controller disconnection, and shutdown.
Switching to keyboard/mouse input also clears gamepad rumble.

Call `Tick` with elapsed seconds and send `GetOutput()` each frame, including zero
when an effect ends. Invalid or nonpositive time steps are ignored. The core does
not detect devices, persist settings, or deliver feedback itself.

## Tuning and verification

Change motor keyframes and priorities in `HapticCore.cpp` and update this reference
with behavior changes. Run `scripts/test-haptics.sh` to verify distinction after
the two-motor Xbox mapping, parry separation at 30/60/120 Hz, short movement
feedback, duplicate/priority arbitration, output bounds, scaling, cleanup,
invalid inputs, and frame-size equivalence. `CXX` selects a C++17 compiler.
`Ronin.Integration.ImpactCamera` checks bounded view offsets, movement silence,
priority, decay, and reset without a rendered game or controller.

These checks establish software behavior. Perceived strength, controller motor
response, disconnect handling, and the combined camera/rumble feel still require
an actual gameplay check. Do not run a diagnostic pulse while someone is playing.

On Linux, the optional driver diagnostic probes an Xbox event device without
reading input events. Only its explicit `--rumble` option plays a gentle 250 ms
pulse, then stops and removes the effect:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror scripts/controller-rumble.cpp -o /tmp/ronin-controller-rumble
/tmp/ronin-controller-rumble
/tmp/ronin-controller-rumble --rumble
```

Use `--device /dev/input/eventN` if multiple Xbox controllers are connected.
A successful driver pulse does not establish Unreal gameplay delivery; check that
separately in the game.
