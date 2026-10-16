# Original katana and saya

An original, restrained weapon asset authored as mesh geometry in Higgsfield 3D Jutsu / Blender 5.2. The thin curved blade has separate edge, ridge, and spine planes, a tapered kissaki, copper habaki, dark iron fittings, a woven indigo grip, and a separate hollow-mouth lacquer scabbard. No paid image/video generation, third-party model, or photographic texture was used.

`preview.png` and `hilt-detail.png` are actual Blender EEVEE renders. Unreal import and fresh-load metadata checks have passed. In-game appearance and character attachment alignment require separate GPU verification.

## Files and scale

| File | Purpose |
| --- | --- |
| `KatanaAndSaya.blend` | Editable source with named parts and review lighting |
| `KatanaAndSaya.glb` | Complete portable scene; saya is offset for presentation |
| `SM_Katana.glb` | Sword only; attachment pivot at grip centre |
| `SM_Saya.glb` | Scabbard only; attachment pivot at mouth centre |
| `textures/` | Nine original 512 px embedded color maps extracted without modification |
| `audit.json` | Blender geometry, UV, manifold, volume, and dimensions audit |
| `export-audit.json` | GLB normals/UV/material validation and exact checksums |
| `unreal-import-audit.json` | Fresh-load Unreal bounds, slots, build settings, material maps, and 20 asset hashes |
| `manifest.json` | Provenance, orientation, material slots, and delivery state |

Source units are metres. The sword is 1.02 m overall, with a 0.72 m exposed blade, 0.24 m grip, 0.076 m guard width, and 0.064 m guard depth. Sword local bounds are Z −0.12 to 0.9 m. Its local +Z points toward the tip and −X is the cutting edge; Y is thickness. The separate saya bounds are Z −0.003 to 0.758 m. Its +Z points from mouth toward closed tip.

Blender converts (X, Y, Z) into glTF (X, Z, −Y). Unreal's GLTF translator converts this into centimetres with (X, −Y, Z). Imported sword length should therefore be 102 cm, tip +Z, edge −X. Apply the attachment rotation required by the chosen hand socket; do not bake a guessed character socket orientation into the asset.

The sword contains 21,938 triangles in 11 named parts and eight material slots. The saya contains 19,968 triangles in four named parts and three slots. They share iron and cloth, giving nine unique materials in total. Source parts remain separate for editing; the prepared Unreal importer combines each attachment into one static mesh while retaining semantic material sections.

## Reproduction and import

In the dedicated 3D Jutsu scene, run `source/01_author_katana.py`, then `source/03_refine_normals.py`, then `source/05_wrap_finish.py` in that order, using the provider's inspected revision/scene-sequence guards. The two other Blender scripts are read-only audit/render queries. They never need to run inside Unreal.

After resolving and downloading the scene's committed `.blend` and `.glb`, run `python3 source/prepare_exports.py` locally. It verifies the GLB and creates the two attachment files while preserving the original binary geometry and texture data. Unreferenced binary data remains in the split files, avoiding a lossy model conversion.

The project script `scripts/import_katana.py` imports only `/Game/Weapons/Katana`, creating `SM_Katana` and `SM_Saya`, nine materials, and nine textures. It builds standard opaque PBR materials from the extracted maps and exported metallic/roughness values, retains imported normals, creates tangents from UV0, and disables generated collision. `--verify` reopens existing assets and checks bounds, triangle counts, slots, full-precision UV0, normal/tangent build flags, texture bindings, and PBR values. The verified import used Unreal 5.7.4 with NullRHI and two cores; the verification preserved all 20 asset file hashes. The native fighter now attaches the katana to the samurai right hand and the saya to the pelvis. `--sockets-only` adds blade-trail endpoints at (0,0,18) and (3.3,0,90) cm without reimporting materials; fresh-load verification checks the saved endpoint vectors.

## Provenance and limits

Scene: [Ronin — Original Katana and Scabbard](https://higgsfield.ai/3d-jutsu/1bd6c6a6-bfce-4ee8-a546-c1890270d2d8), committed revision 3. All mesh coordinates and texture pixels are authored in the included source. The scene name is a working label, not the character or game title.

[Higgsfield Terms of Use](https://higgsfield.ai/terms-of-use-agreement), sections 4.2 and 4.4, state that Higgsfield does not claim ownership of inputs/outputs or restrict their commercial use, and that output rights survive account cancellation and can be transferred or sublicensed. No externally licensed asset is embedded here.

This is one authored LOD0 mesh set, without a custom collision hull, lower LODs, baked wear normal maps, animation, or individual finger animation. Blender area lights remain in the editable source and are not supported by this GLB export. In-game reflections, shader compilation, attachment alignment, and performance must be checked in Unreal; the preview alone cannot establish them.
