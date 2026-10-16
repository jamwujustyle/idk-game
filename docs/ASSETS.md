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
| [Overcast Soil (Pure Sky)](https://polyhaven.com/a/overcast_soil_puresky) | Jarod Guest and Sergej Majboroda | Sky background, skylight, and reflections | 2K HDR |

[The source manifest](../assets/environment-manifest.json) records the provider
URL, exact source-file URL, size, MD5, and actual Unreal import paths. The selected
19 source files total 50,980,041 bytes. The importer writes derived assets only
under `Content/Environment`; the downloaded originals are cached under
`.cache/environment` and can be retrieved again from the manifest.

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

The floor, retaining walls, and timber use world-scaled texture projection;
the imported rocks and foliage retain their UVs. The cloudy HDR is used as an
actual sky texture and light source. The gate and small lantern structures are
still project-authored construction geometry; they are not scanned Japanese
architecture. Fir saplings are used as general garden foliage, not represented
as Japanese maple trees. No third-party showcase or concept image is used as
gameplay evidence.

Import verification checks data and asset creation. Material compilation and
appearance still require a GPU run. The parent project packaging configuration
must include `/Game/Environment` because the arena loads those assets at runtime.

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
