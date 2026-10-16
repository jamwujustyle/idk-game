# Original samurai model intake

The selected source is `meshy-6-lite-textured-v1.glb`, received from the user's Meshy download after generating from the [original armored samurai reference](../ronin-character/reference-samurai-front-v2.png). The preceding `meshy-6-lite-geometry-v1.glb` preserves the geometry-only stage. Both files are unmodified; [the manifest](manifest.json) records their hashes, provenance, attribution, and selection. The user identified the generation as Meshy 6 Lite; the GLB metadata itself does not encode that model version.

The textured export contains 288,176 triangles, 166,055 vertices after UV/normal splitting, one material, UV0, unit-length normals, and three embedded 2048 × 2048 JPEGs: base color, packed metallic/roughness, and normal. All images decode successfully. There is no skeleton, skin-weight data, or animation in either download. These assets have not replaced the current playable character.

The geometry audit checks GLB/accessor bounds, finite positions, index ranges, near-zero-area triangles and edge incidence. The texture pass also checks UV/normal counts and values and hashes embedded images. The raw mesh has no boundary edges or edges incident to more than two triangles by vertex index. The textured export has split vertices at UV/normal seams; its indexed boundaries alone do not establish holes. Neither audit establishes absence of self-intersections, useful deformation topology, visual quality, or game performance.

Reproduce the structural reports from this directory:

```bash
python3 inspect_geometry.py
python3 inspect_geometry.py meshy-6-lite-textured-v1.glb --output textured-audit.json
```

Before gameplay integration, obtain or build a humanoid rig and skin weights, inspect armor and joint deformation, establish scale/orientation and hand sockets, retarget combat and locomotion, and check the result in Unreal. The existing strict rig inspector is `../ronin-character/inspect_model.py`; it intentionally rejects these static exports. Preserve the embedded PBR data during subsequent rigging/export. Meshy's [textured export guide](https://help.meshy.ai/en/articles/15724161-how-to-export-meshy-models-with-colors-and-textures) describes compatible formats; its [workflow guide](https://help.meshy.ai/en/articles/9991793-how-to-use-meshy-complete-beginner-s-tutorial) describes the Animation panel and rigging points.

Attribution: **Samurai Sentinel — generated with Meshy from this project's original armored samurai reference. [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).** The selected license was visible in the supplied generation/download UI. No raw Paragon assets are included in this folder.
