# Combat haptics

`HapticCore` produces four normalized standard-rumble channels: left large, left
small, right large, and right small. Unreal delivers these through force feedback.
The actual motors available and their mapping depend on the controller, operating
system, and driver. Adaptive triggers and advanced DualSense haptics are not
implemented or promised by these standard force-feedback signals.

## Event feel and interruption

Each event has a piecewise-linear amplitude envelope and a motor balance. Large
channels emphasize weight; small channels emphasize a sharper impact. Feedback
starts immediately when an event is accepted and returns to zero at its end.

| Event | Duration, seconds | Priority | Pattern |
| --- | ---: | ---: | --- |
| Swing | 0.11 | 1 | Short, light pulse emphasizing the right small channel |
| Dodge | 0.16 | 2 | Soft swell emphasizing the left large channel |
| Block | 0.16 | 3 | Immediate impact with a short fading tail |
| Damage | 0.32 | 4 | Heavy impact with a smaller secondary shudder |
| Parry | 0.24 | 5 | Two crisp pulses, separated by a brief zero crossing |
| Guard break | 0.5 | 6 | Growing heavy impact and a second pulse |
| Execute | 0.72 | 7 | Strong initial impact, dip, and sustained second impact |

One envelope plays at a time. A higher-priority event interrupts the current one;
an equal-priority event restarts it. Lower-priority events are discarded without
changing the current envelope or queuing a delayed vibration. Swing, dodge,
block, and damage feedback therefore cannot mask an execution or guard break.

## Settings and lifecycle

Global intensity defaults to 0.7 and accepts values from 0 to 1. Each trigger also
accepts a strength multiplier from 0 to 1. Both are clamped, and every output
channel remains within 0 to 1. Negative and non-finite strengths produce no new
event; non-finite global intensity becomes zero. Adjusting intensity changes the
currently playing envelope immediately. Zero intensity mutes output while its
timer continues; it does not pause or replay the effect.

`SetEnabled(false)` clears the current event and outputs zero. Re-enabling cannot
resume an old vibration. `Stop()` clears the envelope while preserving settings;
the engine must call it for pause, restart, controller loss, and gameplay shutdown
and send the resulting zero channels to the device. A fresh event is required
after either operation. Triggering an event while disabled is ignored.

Call `Tick` with elapsed seconds and send `GetOutput()` to the controller each
frame, including zero output after an effect ends. Invalid or nonpositive time
steps are ignored. The core owns timing and scaling only; it does not detect
devices, persist settings, or deliver feedback itself.

## Tuning and verification

Change envelope keyframes, motor balances, and event priorities in
`HapticCore.cpp`; update this reference with behavior changes. Run
`scripts/test-haptics.sh` for checks of event duration and decay, distinct pulses,
interruption priority, output bounds, scaling, disable/stop behavior, invalid
inputs, and frame-size equivalence. `CXX` can select a C++17 compiler executable.
Controller feel, device mapping, and disconnect handling require engine and
hardware playtesting.

On Linux, the optional driver diagnostic can distinguish device support from
game integration. It probes the connected Xbox event device without reading
input events. Only the explicit `--rumble` option plays a gentle 250 ms pulse,
then stops and removes the effect:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror scripts/controller-rumble.cpp -o /tmp/ronin-controller-rumble
/tmp/ronin-controller-rumble
/tmp/ronin-controller-rumble --rumble
```

Use `--device /dev/input/eventN` if multiple Xbox controllers are connected.
A successful driver pulse does not establish that Unreal delivers gameplay
feedback correctly; verify that separately in the game.
