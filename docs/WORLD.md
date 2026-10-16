# Courtyard and forest valley

The duel takes place on the central stone courtyard, surrounded by a walkable
180 m by 180 m forest valley. The ground has matching procedural collision;
the former courtyard boundary boxes are removed. A continuous slope leads
through the east torii gate, and the west curb also has an open exit.

Start with Enter or A, then move through either exit. L3 or Left Shift toggles
sprint while moving; Space or A jumps from grounded idle with guard released.
Walking, sprinting, and jumping continue after defeating the opponent. The
victory banner stays compact at the top of the screen, with an exploration
hint and R / View for a rematch; it does not dim the world or block movement.
Pause and defeat retain their full-screen menus. The
camera releases target lock beyond 9 m or after victory; Tab or right-stick
click can restore it near a living opponent. The enemy stops pursuing beyond
8.5 m separation, leaving room to explore.

Stone paths form a circuit between three places:

- Resting stones southeast of the courtyard, with cedar benches and lanterns.
- A raised ridge overlook farther east, looking back toward the valley.
- A lantern grove northwest of the courtyard, connected to the west exit.

A southern path provides a shorter return between the resting stones and the
courtyard. Each clearing has level ground, markers, benches, and surrounding
saplings. Visible steep escarpments mark the finite outer extent of the valley.
The opponent and duel rules remain separate from the landscape; this is an
explorable setting, without a campaign, settlements, or additional encounters.

`RoninArena::BuildTerrain` creates the colliding ground, `BuildTrail` places the
stone routes on it, and `BuildRestSite` dresses the three clearings. The duel
platform keeps its own flat collision. Decorative trail strips use the terrain
below for support. All scenery reuses the project's existing materials and meshes.

Validation results and rendered traversal evidence are recorded in the
[combat and traversal report](../reports/combat-traversal-20261016.md). The live
world checks cover terrain support, sprinting through the gate, upright moving
jumps, landing, airborne pause/attacks, and movement after victory. Performance
and exploration quality still require playtesting.
