# Environment assets

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
