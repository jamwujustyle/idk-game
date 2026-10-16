# Asset sources

## Environment

The courtyard uses the following Poly Haven assets. Each is released under
[CC0 1.0](https://polyhaven.com/license), which permits modification, commercial
use, and redistribution. Poly Haven provides the public downloads
[without an account](https://polyhaven.com/about-contact).

| Asset | Author | Use | Selected files |
| --- | --- | --- | --- |
| [Monastery Stone Floor](https://polyhaven.com/a/monastery_stone_floor) | Amal Kumar | Dueling floor, stone caps, and retaining walls | 2K diffuse, DirectX normal, packed AO/roughness/metallic |
| [Japanese Cedar Planks](https://polyhaven.com/a/japanese_cedar_planks) | Charlotte Baglioni and Rico Cilliers | Gate, screens, and lantern timber | 2K diffuse, DirectX normal, packed AO/roughness/metallic |
| [Rock Moss Set 01](https://polyhaven.com/a/rock_moss_set_01) | Kless Gyzen | Scanned garden rocks | FBX geometry and 2K surface maps |
| [Fir Sapling](https://polyhaven.com/a/fir_sapling) | Rico Cilliers and Rob Tuytel | Foliage around the court | FBX geometry and 1K branch/needle maps, including opacity |
| [Forest Ground 01](https://polyhaven.com/a/forrest_ground_01) | Rob Tuytel | Soil, moss and leaf litter outside the court | 2K diffuse, DirectX normal, packed AO/roughness/metallic |
| [Fir Sapling Medium](https://polyhaven.com/a/fir_sapling_medium) | Rico Cilliers (modeling), Rob Tuytel (photography) | Larger trees beyond the gate | FBX geometry and 1K branch/needle maps, including opacity |
| [Overcast Soil (Pure Sky)](https://polyhaven.com/a/overcast_soil_puresky) | Jarod Guest and Sergej Majboroda | Sky background, skylight, and reflections | 2K HDR |

[The source manifest](../assets/environment-manifest.json) records the provider
URL, exact source-file URL, size, MD5, and actual Unreal import paths. The selected
19 source files total 50,980,041 bytes. The importer writes derived assets only
under `Content/Environment`; the downloaded originals are cached under
`.cache/environment` and can be retrieved again from the manifest.

The [ground manifest](../assets/ground-manifest.json) records the three additional
2K ground maps (9,902,772 bytes); the [forest manifest](../assets/forest-manifest.json)
records the medium-fir FBX and seven 1K maps (55,891,112 bytes). Both include
official API sizes/MD5 checksums, local SHA-256 checksums and Unreal paths.
Their originals are cached under `.cache/ground` and `.cache/forest`; restore
them with `python3 scripts/import_ground.py --download` and
`python3 scripts/import_forest.py --download`.
Each manifest's top-level `scene_assignment` records runtime use separately from
import metadata; downloading or reimporting the assets preserves that record.

The selected medium-fir FBX contains three LOD0 variants, about 5.9–8.8 m tall,
with 685,046, 420,822 and 427,645 triangles. It contains no lower LOD meshes,
despite the provider page's broader LOD tag. Their original UVs and three
material slots are preserved. The FBX import reports missing smoothing groups,
degenerate tangent bases and nearly zero binormals. Matching source triangle
counts, heights and UVs does not establish correct rendered tangent shading.
The arena places seven instances, 6.8–9.3 m tall, with Nanite disabled and one
LOD each. Inspect close-up shading and measure GPU cost before increasing that
count; source triangle counts are not a frame-time measurement.

To reproduce the assets from the project root with the official Linux engine at
`.tools/unreal`:

```bash
python3 scripts/download_environment.py
.tools/unreal/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/Ronin.uproject" \
  -run=pythonscript "-script=$PWD/scripts/import_environment.py" \
  -nullrhi -corelimit=2 -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput
```

The downloader verifies provider sizes and MD5 values before recording a
completed source file, and caps the total selected source payload at 300 MB.
The importer constructs the PBR materials, sets normal and mask color spaces,
assigns foliage opacity, and records mesh bounds and material slots. It also
enables instanced-static-mesh material usage for the runtime scenery.

Packed ARM outputs use explicit single-channel masks: R for ambient occlusion,
G for roughness, and B for metallic; all other flags are false. Unreal's node
creation defaults enable R and G together. Run `scripts/repair_surface_channels.py`
in the same NullRHI commandlet with `--apply` to repair existing surface materials
without reimporting assets; `--verify` checks the saved channel wiring. Each run
writes graph checks and environment-asset hashes under `reports/assets`.

The ground and medium-fir importers use `--import-assets` with the same bounded
NullRHI commandlet flags. They create their own materials and texture names;
medium trees are stored in `/Game/Environment/FirsMedium`. The medium-fir needle
material uses Two Sided Foliage shading, its original alpha mask, and tinted
diffuse color multiplied by the `SubsurfaceStrength` parameter (default 0.35)
for restrained backlighting. It does not use emissive light. To rebuild only
this needle material from existing textures, without writing meshes or textures:

```bash
.tools/unreal/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/Ronin.uproject" \
  -run=pythonscript "-script=$PWD/scripts/import_forest.py --material-only" \
  -nullrhi -corelimit=2 -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput
```

`RoninArena` assigns the ground material to the shaped `ForestTerrain` mesh and
uses a shared height function to anchor foliage and rocks. The outer terrain
and decorative meshes have no collision; the courtyard floor and invisible
boundary boxes govern movement. Bounds-based planting still needs visual review
across broad footprints on sloping ground.

The floor, retaining walls, and screen timber use world-scaled texture projection;
the imported rocks and foliage retain their UVs. The cloudy HDR is used as an
actual sky texture and light source. The small lantern structures remain
construction geometry. Fir saplings are used as general garden foliage, not represented
as Japanese maple trees. No third-party showcase or concept image is used as
gameplay evidence.

Import verification checks data and asset creation. Current assignments,
material-channel verification and the observed in-game view are recorded in the
[environment report](../reports/2026-10-16-environment.md). A saved frame does not
establish motion quality, GPU performance or packaged-build behavior. The project
packaging configuration must include `/Game/Environment` because the arena loads
those assets at runtime.

## Original torii

The gate is authored project geometry created through Higgsfield's 3D Jutsu
Blender workflow. Its [source and export manifest](../assets/generated/torii/manifest.json)
records the editable Blender file, GLB, modelling scripts, revision, checksums,
and usage terms. It contains curved beams, tapered posts, joinery, bevelled
edges and stone footings, with 37,064 triangles and three material sections.
The Unreal mesh is `/Game/Environment/Torii/SM_ToriiGate`; `BuildGate` places it
at unit scale on the outer approach, with yaw 90 degrees facing the court.
Its two wood materials use the CC0 cedar maps above, with UVs
oriented along the beams. Stone footings use the existing stone-edge material.

Run `scripts/import_torii.py` through the same NullRHI Python commandlet to
recreate the Unreal assets; `--verify` checks existing bounds, topology, slots
and UVs without writing them. The importer verifies the stored cap tint rather
than trusting UE 5.7's always-false vector-parameter setter return value.

## Character

[Paragon: Kwang](https://www.fab.com/listings/f4c67e92-b976-4b5b-ab9f-4c25b010f6f3)
is provided by Epic Games through Fab, under the listing's
[Fab Standard License](https://www.fab.com/eula). It supplies the rigged character,
material variants, and authored animations. Developers must acquire the pack in
their own Fab library and use **Window → Fab → My Library → Paragon: Kwang → Add
to Project** to populate `Content/ParagonKwang`.

The raw character pack is ignored by Git and is not redistributed in this public
source repository. Keep its original asset paths so code references resolve.
Distributable game builds should contain cooked assets required by gameplay;
acquiring this pack does not make its source assets CC0.

The player uses `Kwang_GDC` and the sentinel uses `KwangRosewood`, both on the
original Kwang skeleton. `URoninAnimInstance` blends the authored cardinal jog
clips, maps sword swings onto the combat system's windup/contact/recovery
phases, and plays the separate recovery, stagger, and death clips. Capsule
movement remains authoritative; animation root translation is removed.

The pack has no dedicated guard or dodge roll. Guard adapts the upper body of
`Ability_RMB`; evasions use the directional jog-start clips as steps. These
adaptations need gameplay review. The native animation graph does not depend
on the pack's example character or animation Blueprints. Cook only gameplay
references, rather than adding the entire pack to `DirectoriesToAlwaysCook`.

Blade motion uses the pack's base `P_Kwang_Primary_Trail` effect with the authored
`FX_weapon_base` and `FX_weapon_tip` sockets. Emission is limited to the combat
system's Active phase, samples follow bone evaluation, and the existing tail
can fade after the swing. Pause and hit-stop freeze its samples and age; rematch
discards the ribbon before teleporting either fighter. The L/R variants with
additional smoke are not used.

Parries, blocks, and guard breaks use a transient copy of
`P_Kwang_Primary_Impact` containing only its `Sparks` and `Sparks_Smaller`
emitters. Their original LODs, spark material and GPU simulation modules are
preserved; continuous spawning and the source delay are removed, and the two
bursts are configured for 6 and 12 sparks. Each effect lasts at most 0.35 seconds,
with at most four concurrent effects. Sparks continue through contact hit-stop;
pause freezes them, and rematch clears them. Damage does not spawn this effect.
The source asset is never edited or played whole: fire, flare, plasma, smoke and
glow emitters are excluded before activation. Code keeps a hard reference to
the source for cooking, which also includes its dependencies even though the
runtime copy filters the emitters. Both effects still need a GPU review for
appearance, contact placement, timing and cleanup; headless checks cannot verify
their rendered appearance.
