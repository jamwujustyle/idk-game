# Authored torii gate

The gate is an AI-assisted, project-authored static mesh built in Blender through
Higgsfield 3D Jutsu. The source scripts construct its curved kasagi, black cap,
shimaki, inward-leaning tapered pillars, nuki, wedges, dowels, stone footings, and
unlettered plaque. It is not a scan or a reconstruction of a named historical gate.

The [manifest](manifest.json) records provenance, the provider's terms, exact
scene revision, source hashes, bounds, and Unreal material bindings. It is the
maintained provenance record for this asset. [The actual final render](../../../reports/assets/torii-final-front.png)
shows the authored model under Blender review lighting. It is not a game capture.

The GLB and editable Blender file preserve 22 semantic parts and three named
materials. LOD0 contains 37,064 triangles. The measured source dimensions are
10.9 m wide, 1.101 m deep, and 6.57 m tall. The pivot is at the base centre.
Blender X is width, Z is up, and the front faces -Y. Cedar UV0 uses 1.1 m per
UV unit, with V following each timber's grain. UV islands intentionally tile.

To reconstruct the scene in an empty Blender 5.2 scene, run `source/01_blockout.py`,
then `source/02_detail.py`, then `source/04_portable_materials.py`. The review
query `source/03_verify_render.py` also needs the 3D Jutsu `artifacts` registry;
its temporary floor is intentionally not part of the exported asset.
`python3 assets/generated/torii/source/05_verify_export.py` verifies the saved
portable file locally without Unreal or Blender.

The embedded GLB maps are small mathematical review textures. The Unreal
importer replaces them with the existing CC0 cedar/stone textures. Its UV cedar
material follows the timbers, and a dark instance shades the cap and dowels.
It combines the source parts into one static mesh with three material sections.
The source GLB and Blender file retain the individual part names.

```bash
.tools/unreal/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/Ronin.uproject" \
  -run=pythonscript "-script=$PWD/scripts/import_torii.py" \
  -nullrhi -corelimit=2 -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput
```

The prepared destination is `/Game/Environment/Torii/SM_ToriiGate`. Unreal's
GLTF conversion makes the front face +Y; yaw 90 degrees faces the gate toward
-X in the arena. Use unit scale and place the base pivot at the paving height.
The importer asserts dimensions, 37,064 triangles, UV0, and three semantic slots.
It generates no collision or extra LOD. Unreal import and in-game appearance
remain separate checks; the saved mesh and Blender render do not establish them.
