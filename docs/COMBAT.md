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
Another attack cannot directly cancel the current one. Stance changes are
allowed while idle or dodging.

Starting an attack or dodge releases guard. A hit interrupts an unprotected
fighter for 0.28 seconds on a light strike or 0.42 seconds otherwise. Dodging,
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
The next attack consumes that opportunity: damage ×1.35, posture damage ×1.4,
and windup ×0.75. Stance switching preserves it.

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
frame-size equivalence, repeated action use, defense cancels and commitment,
parry/evade boundaries, held-guard pressure, counter rewards, execution, death,
and restart. Set `CXX` if the compiler is not on the standard path. The engine
live-world test additionally checks bound-input cancellation, swept dodge travel
at two frame sizes, pause, and rematch cleanup. Engine compilation, GPU review,
and controller playtesting are separate verification steps.
