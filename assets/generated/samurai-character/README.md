# Original samurai model intake

The selected source is `meshy-6-lite-rigged-v1.glb`, received from the user's Meshy download after generating from the [original armored samurai reference](../ronin-character/reference-samurai-front-v2.png). The preceding `meshy-6-lite-geometry-v1.glb` preserves the geometry-only stage. All three received GLBs are unmodified; [the manifest](manifest.json) records their hashes, provenance, attribution, and selection. The user identified the generation as Meshy 6 Lite; the GLB metadata itself does not encode that model version.

The textured export contains 288,176 triangles, 166,055 vertices after UV/normal splitting, one material, UV0, unit-length normals, and three embedded 2048 × 2048 JPEGs: base color, packed metallic/roughness, and normal. All images decode successfully. The earlier geometry and textured exports are static. The selected rigged export preserves the same textured surface and adds 32 bones, skin weights, and two in-place clips: Running (0.666667 s) and Walking (1.041667 s). No sword, guard, dodge or death animations came in the download.

The geometry audit checks GLB/accessor bounds, finite positions, index ranges, near-zero-area triangles and edge incidence. The texture pass also checks UV/normal counts and values and hashes embedded images. The raw mesh has no boundary edges or edges incident to more than two triangles by vertex index. The textured export has split vertices at UV/normal seams; its indexed boundaries alone do not establish holes. Neither audit establishes absence of self-intersections, useful deformation topology, visual quality, or game performance.

Reproduce the structural reports from this directory:

```bash
python3 inspect_geometry.py
python3 inspect_geometry.py meshy-6-lite-textured-v1.glb --output textured-audit.json
```

`scripts/import_samurai.py` imports the selected GLB as `/Game/Characters/Samurai/SK_Samurai`, its skeleton, two animation clips, material and textures. Fresh-load checks passed for all saved weights, clip durations, render geometry, PBR bindings and unchanged file hashes; see `unreal-import-audit.json`. Use a two-core NullRHI Python commandlet with `-EnablePlugins=GeometryScripting`; `--verify` performs read-only asset verification.

The rig has no individual finger or armor bones. The [rig analysis](rig-analysis.md) records measured wrist-helper deformation and a deterministic correction, along with attachment transforms. Structural checks do not establish convincing hand grips, rigid armor deformation or visual quality. Retargeting and gameplay integration are a separate validation step. The strict rig inspector is `../ronin-character/inspect_model.py`; it accepts this rigged export and intentionally rejects the earlier static exports. Preserve the embedded PBR data during later editing. Meshy's [textured export guide](https://help.meshy.ai/en/articles/15724161-how-to-export-meshy-models-with-colors-and-textures) describes compatible formats; its [workflow guide](https://help.meshy.ai/en/articles/9991793-how-to-use-meshy-complete-beginner-s-tutorial) describes the Animation panel and rigging points.

Attribution: **Samurai Sentinel — generated with Meshy from this project's original armored samurai reference. [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).** The selected license was visible in the supplied generation/download UI. No raw Paragon assets are included in this folder.
