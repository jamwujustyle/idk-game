# Combat behavior and tuning

`CombatCore` owns the two fighters' combat state without Unreal dependencies. The
engine supplies movement, distance, facing, animation, and feedback. Times below
are seconds; reach uses Unreal centimeters. Tune attack values in
`CombatCore::GetAttackSpec` and defense/recovery values in the corresponding core
methods, updating this reference with behavior changes.

## Actions and contact

Attacks require idle state and sufficient stamina, then commit through windup,
active, and recovery. They cannot be canceled into another attack, guard, dodge,
or stance change. Stance changes are allowed while idle or dodging. Starting an
attack or dodge releases guard. A hit interrupts an unprotected fighter for 0.28
seconds on a light strike or 0.42 seconds otherwise.

Call `ResolveHit` during the active phase with the current distance and attack
cone test. A spatial miss can be checked again; a valid contact resolves once per
attack, including a parry, block, or dodge. `attackId` remains unique across
restarts. `actionTime` measures the current phase; `attackElapsed` spans the
attack. Attack values are captured in `FighterState::spec` when the attack begins.

| Stance / attack | Windup | Active | Recovery | Health damage | Posture damage | Stamina | Reach |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Flow light | 0.19 | 0.11 | 0.25 | 15 | 18 | 14 | 210 |
| Flow heavy | 0.43 | 0.14 | 0.4 | 26 | 30 | 25 | 220 |
| Stone light | 0.28 | 0.13 | 0.36 | 20 | 25 | 18 | 210 |
| Stone heavy | 0.54 | 0.16 | 0.48 | 32 | 48 | 30 | 230 |
| Gale light | 0.14 | 0.12 | 0.21 | 12 | 12 | 11 | 225 |
| Gale heavy | 0.33 | 0.15 | 0.31 | 21 | 22 | 21 | 245 |
| Unblockable, any stance | 0.78 | 0.18 | 0.55 | 35 | 35 | 32 | 240 |

## Parry, guard, and dodge

A fresh guard press while idle opens a 0.18-second parry window and starts a
0.32-second parry cooldown. Holding guard never refreshes the window. Releasing
guard closes it immediately; pressing again during cooldown permits blocking
but grants no new parry window. A successful parry consumes the window.

The engine tracks the physical guard button separately from the active guard.
If the button remains held through an attack, dodge, or stagger, it restores
blocking after the fighter returns to idle by calling
`SetGuard(id, true, false)`. The final `allowParry` argument prevents automatic
blocking from opening a parry window or changing the parry cooldown. It defaults
to `true` for a fresh physical press. Releasing with `SetGuard(id, false)` always
clears guard and any remaining parry window.

Parrying prevents damage, adds 34 posture to the attacker (42 in Stone), and
staggers them for 0.58 seconds unless their posture breaks. The defender regains
12 stamina, removes 12 posture, and gains a 1.1-second counter opportunity. The
next attack consumes that opportunity: damage ×1.35, posture damage ×1.4, and
windup ×0.75. Stance switching preserves the counter opportunity.

Holding guard beyond the parry window prevents health damage but receives the
listed posture damage; Stone reduces that amount to 72%. Blocking costs 65% of
the incoming attack's stamina cost. Insufficient stamina causes an immediate
guard break. Unblockable attacks bypass both guard and parry.

Dodges require idle state and enough stamina. Invulnerability begins immediately
and ends before the dodge action finishes, leaving a vulnerable ending.

| Stance | Stamina cost | Invulnerability | Total dodge duration |
| --- | ---: | ---: | ---: |
| Flow / Stone | 24 | 0.22 | 0.44 |
| Gale | 18 | 0.26 | 0.38 |

## Resources, guard breaks, and execution

Player health starts at 100; enemy health starts at 125. Both have 100 maximum
stamina and posture. Posture measures accumulated damage: a full bar breaks the
fighter. Unguarded hits deal the listed health damage and 55% of listed posture
damage. Health does not regenerate.

Stamina regenerates only while idle: 26 per second normally, or 12 while guarding.
The regeneration delay is 0.65 seconds after starting an attack or blocking, and
0.55 seconds after starting a dodge. Posture starts recovering after 1.6 seconds
without posture damage, only while idle, at
`16 × (0.35 + 0.65 × current health / maximum health)` per second.

A guard break disables actions for 2.2 seconds. An idle opponent facing the broken
fighter within 240 cm can execute them, killing them immediately. Execution
restores the attacker's stamina, clears their posture, and commits them to 0.6
seconds of recovery. If the opening expires, the broken fighter returns to idle
with 30 posture and a 1-second posture recovery delay. Damage during the opening
does not refresh its duration. Zero health is terminal until reset.

## Enemy rhythm and verification

The enemy starts in Stone with a 1.1-second initial delay. Its repeating attack
sequence is light, light, heavy, unblockable, heavy, light. Starting an attack
advances the sequence and schedules the next opportunity 0.55 seconds after the
attack's complete duration. Being parried replaces that delay with 1.15 seconds.
`EnemyReady` requires idle state, an expired decision timer, and a living player;
the engine still chooses when spacing permits an attack. Failed attack attempts
do not advance the sequence. Reset restores resources, initial stances, and the
opening delay.

Run `scripts/test-core.sh` for deterministic C++17 checks covering phase timing,
frame-size equivalence, contact rules, defense tradeoffs, guard spam prevention,
counter bonuses, stamina, execution constraints, death, and restart. Set `CXX`
to a compiler executable when it is not on the standard path. Engine compilation
and playtesting are separate verification steps.
