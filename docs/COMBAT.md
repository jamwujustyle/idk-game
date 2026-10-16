# Combat behavior and tuning

`CombatCore` owns combat state without Unreal dependencies. The engine supplies
movement, distance, facing, animation, and feedback. Times below are seconds;
reach and travel use Unreal centimeters. Tune attacks in `GetAttackSpec` and
keep this reference with changes to the defense and recovery rules.

The design references are the distinction between held block, timed parry, and
evasive movement in the [official Ghost of Tsushima guide](https://www.playstation.com/en-us/editorial/ghost-of-tsushima-beginners-guide/),
and Sucker Punch's emphasis on responsive defensive cancellation in
[Mastering the katana](https://blog.playstation.com/2020/06/23/ghost-of-tsushima-mastering-the-katana/).
The values and commitment rules below are this duel's own tuning.

## Actions and contact

There is no stamina resource, action cost, regeneration, or exhaustion gate.
Attacks start from idle and pass through windup, active, and recovery. A fresh
guard press or dodge can cancel windup and recovery. During the active strike,
defense becomes available only after a valid contact has resolved. A spatial
miss leaves the strike committed until recovery. Canceled attacks cannot later
hit; the next attack gets a new identifier and starts its phase clock at zero.
A queued light follow-up can shorten recovery as described below; it cannot
cancel windup or the active strike. Stance changes are allowed while idle or
dodging.

Starting an attack or dodge releases guard. A hit interrupts an unprotected
fighter for 0.28 seconds on a light strike or 0.42 seconds otherwise; a critical
counter causes 0.52 seconds of stagger. Dodging,
stagger, posture break, and death cannot be canceled into another defense.
Actions cannot begin after the opponent dies.

Call `ResolveHit` during active with actual distance and the attack cone test.
A spatial miss can be checked again; a valid contact resolves once per attack,
including parry, block, or dodge. `attackId` remains unique across restarts.
`actionTime` measures the current phase; `attackElapsed` spans the attack.
Attack values are captured in `FighterState::spec` when it begins.

| Stance / attack | Windup | Active | Recovery | Health damage | Posture damage | Reach |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Flow light | 0.19 | 0.11 | 0.25 | 15 | 18 | 210 |
| Flow heavy | 0.43 | 0.14 | 0.4 | 26 | 30 | 220 |
| Stone light | 0.28 | 0.13 | 0.36 | 20 | 25 | 210 |
| Stone heavy | 0.54 | 0.16 | 0.48 | 32 | 48 | 230 |
| Gale light | 0.14 | 0.12 | 0.21 | 12 | 12 | 225 |
| Gale heavy | 0.33 | 0.15 | 0.31 | 21 | 22 | 245 |
| Unblockable, any stance | 0.78 | 0.18 | 0.55 | 35 | 35 | 240 |

## Light combos and sprint

Light attacks form a five-cut chain. A fresh light-attack press during active
or recovery accepts one follow-up. Pressing during windup, repeatedly pressing
with a follow-up already queued, or holding the button does not enqueue more
attacks. The next cut begins after the first 0.09 seconds of recovery. A press
later in recovery begins it immediately. The fifth cut must finish its recovery
before another chain can begin; heavy attacks and critical counters do not enter
this light chain.

| Cut | Damage multiplier | Posture multiplier | Windup multiplier | Recovery multiplier |
| --- | ---: | ---: | ---: | ---: |
| First | 1 | 1 | 1 | 1 |
| Second | 1.08 | 1.15 | 0.9 | 1 |
| Third, quick return | 1.04 | 1.1 | 0.82 | 0.9 |
| Fourth, crosscut | 1.25 | 1.45 | 1.08 | 1.05 |
| Fifth, finishing cut | 1.55 | 1.85 | 1.2 | 1.35 |

The multipliers apply to that stance's base light attack. Guard, dodge, being
hit or parried, and either fighter's death cancel pending follow-ups. Finishing
recovery without a queued input ends the chain. `comboStep` identifies cuts
0–4; `comboQueued` records one pending input. `TryAttack` returns true for both
starting a strike and accepting that input. Presentation observes a change in
`attackId` to play each actual swing's audio and rumble once.
All five ordinary cuts retain the normal light-hit stagger duration; the
stronger late cuts do not extend it. A critical counter is one separate strike
with full recovery and no queued light follow-up. The next light attack after
that recovery starts a new chain at its first cut.

Click L3 or press Left Shift while moving to toggle sprint. Stopping, guarding,
attacking, dodging, or rematching cancels it. Normal movement is 260 cm/s, sprint
is 520 cm/s, and held guard is 130 cm/s before cinematic time scaling. Sprint
turns the fighter along movement instead of continually facing the locked
opponent; it does not clear the lock setting. There is no sprint stamina cost.

Space or gamepad A jumps while grounded and idle with guard released. Jumping
requires the duel to have started and the game to be unpaused. A's title-screen
confirmation does not also jump; while paused, A changes quality instead.
Keyboard dodge uses Left Ctrl; gamepad dodge remains B. There is one jump before
landing, and releasing the button ends its short hold period.

Traversal uses `CanTraverse`, which requires a started, unpaused game and a living
player. It continues after victory, while combat actions still require a living
opponent through `CanControl`. Target lock releases automatically beyond 9 m or
when the opponent dies. The enemy stops pursuing when the player is more than
8.5 m away. See [the valley layout](WORLD.md) for the routes around the courtyard.

## Visible defense and impact

An idle, facing enemy raises guard after seeing 0.08 seconds of an incoming
windup, and maintains it through contact and chained recovery. This automatic
brace never creates a parry window and never cancels an already committed enemy
attack. Health hits use the stagger pose; blocked hits retain the raised blade
and add a short body recoil. Impacts alternate a small torso reaction, with
stronger reactions on heavy attacks, finishing cuts, and critical counters.

A blocked strike pushes the defender up to 8 cm, an ordinary hit 18 cm, and a
strong hit 45 cm over 0.2 combat seconds. A successful parry also drives the
attacker into that stronger recoil while the core applies its stagger. Capsule sweeps stop the displacement at
walls. Movement does not immediately erase the recoil. Attack windups include a
bounded step toward the committed target direction (up to 32 cm normally and
170 cm for a nearby earned counter), keeping successive exchanges connected.
These steps also sweep against collision and stop when the attack is canceled.

## Parry and held guard

A fresh guard press opens a 0.18-second parry window and starts a 0.32-second
retry delay. Holding never refreshes the window. Release closes it immediately;
repeated presses during that delay allow blocking but no new parry window.
A successful parry consumes the window and clears the retry delay, allowing a
new deliberate press to deflect a following strike. Holding after success does
not automatically parry again.

The engine tracks the physical guard button separately. If held through an
attack, dodge, or stagger, it restores blocking on reaching idle with
`SetGuard(id, true, false)`. This cannot cancel an action or grant a parry window.
The default `allowParry=true` is for a physical press. `SetGuard(id, false)`
always clears guard and the remaining parry window.

A parry prevents damage, adds 34 posture to the attacker (42 when the defender
uses Stone), and staggers them for 0.58 seconds unless their posture breaks.
It removes 12 defender posture and grants a 1.1-second counter opportunity.
The next attack consumes that opportunity: damage ×1.8, posture damage ×1.8,
and windup ×0.6. Its confirmed hit causes 0.52 seconds of stagger. Stance
switching preserves the opportunity; expiration or taking damage loses it.
Starting the counter spends it even if the attack later misses or is canceled.
`counterSource` identifies the available parry/dodge opening;
`isCounterAttack` and `attackCounterSource` identify the attack that spent it.

Held guard prevents health damage but receives the listed posture damage;
Stone reduces that to 72%. Posture does not recover while guard is held, so
sustained blocking eventually breaks. Unblockable attacks bypass guard and
parry and receive a red dodge cue.

## Evasive step

Dodge starts immediately from idle or a cancellable attack phase. It is a short
directional step; without a movement direction it steps backward. Invulnerability
occupies only its opening, followed by committed vulnerable recovery. Another
dodge is available as soon as the step ends, without a resource or extra delay.

| Stance | Invulnerability | Total duration | Unobstructed travel |
| --- | ---: | ---: | ---: |
| Flow / Stone | 0.12 | 0.3 | 180 |
| Gale | 0.15 | 0.26 | 210 |

The fighter captures direction, duration, and travel at dodge start. Movement
uses the cumulative curve `distance × (1.6t − 0.6t²)` for normalized phase `t`,
so it starts fast and eases down. Each simulation step applies the curve's
interval with collision sweeps, including the final fraction after the core
reaches idle. A hit interrupts remaining travel; pause freezes it and rematch
clears it. Stance changes during a dodge do not alter its captured timing or
travel. The animation uses an authored directional step, not a roll.

Evading a valid incoming contact grants a 0.65-second counter opportunity with
the same next-attack bonuses as a parry. Dodging outside the attack's range or
cone gives no bonus. It does not damage the attacker's posture. A longer existing
counter opportunity is preserved. The attacker still resolves only one contact.

## Successful-defense presentation

Only a resolved incoming contact starts the success cue. Pressing guard or
dodging in empty space does not trigger it. A successful player parry receives
a 0.07-second contact beat, a 0.3-second hold at 18% combat speed, then a
0.15-second eased return to normal. An in-range perfect dodge has no contact
freeze: it holds at 25% speed for 0.1 seconds and returns over 0.2 seconds.
The counter window counts combat time during this effect.

Combat phases, fighter movement, dodge travel, and animation use the slowed
clock. Input, the camera, interface, and rumble remain on real time, so the
player can start the counter during the effect. Defense reaction poses, a brief
lens push, and `COUNTER READY` feedback identify the opening; a landed empowered
strike displays `CRITICAL COUNTER`. Pause, rematch, and focus loss clear the
cinematic state. See [haptic delivery](HAPTICS.md) for the contact patterns and
camera behavior. Rendering and controller feel require separate playtesting.

## Posture breaks and execution

Player health starts at 100; enemy health starts at 125. Both have 100 maximum
posture. Posture is accumulated damage: a full bar breaks the fighter. Unguarded
hits deal health damage and 55% of the listed posture damage. Health does not
regenerate. The HUD shows vitality and posture, with high player posture in red.

Posture starts recovering after 1.6 seconds without posture damage, only while
idle with guard released, at
`16 × (0.35 + 0.65 × current health / maximum health)` per second.

A posture break disables actions for 2.2 seconds. An idle opponent facing the
broken fighter within 240 cm can execute them, killing them immediately.
Execution clears the attacker's posture and gives 0.6 seconds of recovery.
If the opening expires, the broken fighter returns to idle with 30 posture
and a 1-second recovery delay. Damage during the opening does not refresh its
duration. Zero health is terminal until reset.

## Enemy rhythm and verification

The enemy starts in Stone with a 1.1-second initial delay. Its repeating attack
sequence is light, light, heavy, unblockable, heavy, light. Starting an attack
advances the sequence and schedules the next opportunity 0.55 seconds after its
complete duration. Being parried replaces that delay with 1.15 seconds.
`EnemyReady` requires idle, an expired decision timer, and a living player; the
engine still decides when spacing permits an attack. Failed attack attempts do
not advance the sequence. Reset restores health, posture, stances, and the delay.

Run `scripts/test-core.sh` for deterministic C++17 checks of phase/contact rules,
frame-size equivalence, five-cut chains, single-input buffering, chain
interruption, repeated action use, defense cancels and commitment, parry/evade
boundaries, held-guard pressure, single-use counter rewards, execution, death,
and restart. Set `CXX` if the compiler is not on the standard path. The engine
live-world test additionally checks bound-input cancellation, swept dodge travel
at two frame sizes, pause, and rematch cleanup. Engine compilation, GPU review,
and controller playtesting are separate verification steps.
