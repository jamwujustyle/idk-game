# Samurai skeletal import

The preserved `meshy-6-lite-rigged-v1.glb` is imported as a real Unreal skeletal mesh. The source GLB is unchanged: SHA-256 `737a9ab1c49c7bf685aa9f2ce1db6753145859e7e0654fed87296d8803515efd`. Source ownership and attribution remain documented in this folder's [README](README.md) and manifest; importing does not change the source license.

## Saved assets

| Asset | Unreal package |
|---|---|
| Skeletal mesh | `/Game/Characters/Samurai/SK_Samurai` |
| Skeleton | `/Game/Characters/Samurai/SK_Samurai_Skeleton` |
| Running | `/Game/Characters/Samurai/A_Samurai_Running` |
| Walking | `/Game/Characters/Samurai/A_Samurai_Walking` |
| Material | `/Game/Characters/Samurai/Materials/M_Samurai` |
| Textures | `/Game/Characters/Samurai/Textures/T_Samurai_{BaseColor,Normal,MetallicRoughness}` |

`M_Samurai` multiplies the sRGB base texture by `ColorTint`, a vector parameter that defaults to white. The packed linear texture uses G for roughness and B for metallic. It has no occlusion input because the source does not specify one. The OpenGL normal texture has its green channel flipped for Unreal, matching Unreal's glTF translator. All three textures retain their original embedded 2,048 × 2,048 JPEG pixels; unchanged extracts are in `textures/`. The material preserves the source's two-sided setting and enables skeletal-mesh usage.

## Verification

Fresh-load verification passed under Unreal **5.7.4**, NullRHI, with two cores. See [unreal-import-audit.json](unreal-import-audit.json) for exact paths, hierarchy, build flags, channels, and hashes. All eight `.uasset` file hashes were unchanged by verification.

- 288,176 triangles; 166,055 saved source vertices and 166,060 render vertices after Unreal's render-data splits.
- One full-precision UV channel. Imported normals retained; MikkTSpace tangents rebuilt. A sample spanning 1,026 render triangles had finite UVs and unit normals.
- Every saved source vertex has normalized skin weights; at most four influences, bone indices 0–31, maximum sum error about `4.47e-8`. Skin weights use 16-bit precision.
- Bounds in centimeters: `(-46.1291, -19.0742, 0)` to `(46.1291, 19.0742, 170)`.
- Running: 16 frames at 24 Hz, duration `0.6666666865` seconds. Walking: 25 frames at 24 Hz, duration `1.0416666269` seconds. Both retain all 32 bone tracks, matching source duration within floating-point precision.

Weights are read from the saved skeletal MeshDescription. Unreal's render-data adapter requires an initialized RHI skin-weight resource, so it omits that profile under NullRHI. Render topology, UVs, and normals are checked separately. This verification does not demonstrate GPU skin deformation.

## Integration constraints

The mesh faces Unreal **+Y**; a **−90° yaw** aligns it to an actor facing +X. glTF `(x, y, z)` meters becomes Unreal `(x, z, y)` centimeters. All Mixamo colons become underscores, for example `mixamorig_RightHand` and `mixamorig_LeftHand`.

Bone 0 is **`mixamorig_Hips`**, with local bind translation `(0.2601, -1.5882, 95.8464)` cm. There is no separate ground root. Zeroing this bone's full translation would remove the pelvis height. Both imported clips retain their original pelvis animation; root-motion extraction and forced root locking are disabled. No attachment sockets were added.

Saved mesh, skeleton, material, textures, and compressed clips load successfully. The project must reference them or include their directory when cooking. Final packaged cooking, GPU appearance, animation deformation, attachments, combat animation coverage, and LOD/performance tuning remain separate integration checks. This import does not include an animation blueprint or gameplay changes.

## Repeatable import

From the project root:

```bash
.tools/unreal/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/Ronin.uproject" \
  -run=pythonscript "-script=$PWD/scripts/import_samurai.py" \
  -nullrhi -corelimit=2 -EnablePlugins=GeometryScripting \
  -unattended -nop4 -nosplash
```

For fresh-load read-only verification, change the script argument to `"-script=$PWD/scripts/import_samurai.py --verify"`. GeometryScripting is enabled only for the inspection process; these game assets do not depend on that plugin.

The importer selects each original clip into a temporary GLB under `.cache/samurai-import`, keeping its source binary buffers unchanged. Each clip receives its exact 24 Hz frame range because Unreal's glTF translator otherwise rounds animation ends to 30 Hz. Use this script to regenerate the temporary clip inputs when reimporting. The original rigged GLB remains the maintained source.
