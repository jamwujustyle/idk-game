# Environment integration — 2026-10-16

The ground, seven medium firs, and original torii are imported and assigned in
`RoninArena`. A native Unreal GPU capture shows the terrain, gate and foliage.
This report separates that view from source integrity and remaining acceptance work.

## Sources and scene assignment

| Asset | Provenance | Current runtime use |
| --- | --- | --- |
| Forest Ground 01 | Three CC0 Poly Haven maps; [manifest](../assets/ground-manifest.json) | `M_ForestGround` covers the 180 × 180 m visual terrain, using a 2 m world texture scale and linear tint (0.28, 0.32, 0.24). |
| Fir Sapling Medium | FBX plus seven CC0 Poly Haven maps; [manifest](../assets/forest-manifest.json) | Seven instances of three imported variants surround the gate and ridges, at heights of 6.8–9.3 m. Bark and masked Two Sided Foliage needle materials retain the source UVs and alpha map. |
| Myojin torii | AI-assisted project-authored geometry through Higgsfield 3D Jutsu/Blender; [manifest](../assets/generated/torii/manifest.json) | `SM_ToriiGate` at (1560, 0, −55) cm, yaw 90 degrees, unit scale. The mesh has 37,064 triangles and three sections, using CC0 cedar and stone maps in Unreal. The exported preview textures are separate authored maps. |

The cached ground/forest source files were rechecked against all 11 recorded
sizes, provider MD5s and local SHA-256s. The torii GLB and Blender file match their
recorded sizes and SHA-256s. Existing import metadata records actual bounds,
triangle counts, UVs and material slots; this documentation pass did not reimport
or modify any Unreal asset.

The arena's shaped terrain, trees, rocks and torii are decorative. The separate
courtyard floor and invisible boundary boxes provide movement collision.
`AddNaturalMesh` corrects imported pivot offsets and anchors mesh bounds to the
shared terrain height function; this is a placement rule, not evidence that every
root or rock footprint sits cleanly on the triangulated slopes.

## Material correction and observed result

The saved graphs for all ten authored surface materials had AO masks selecting
RG, roughness masks selecting RG, and metallic masks selecting RGB. Unreal's
material-expression creation helper enables R and G by default. The importers
now set every channel explicitly, and the targeted repair selects only R for AO,
G for roughness and B for metallic.

The [repair report](assets/surface-channels-20261016T100843085413Z/report.json)
contains before/after graph exports and asset hashes. Only ten material files
changed; all other 42 environment files remained byte-identical. Other graph
data was preserved apart from engine material-cache GUIDs. A separate
[fresh-load verification](assets/surface-channels-20261016T100924966245Z/report.json)
confirmed all saved masks and left all 52 environment file hashes unchanged.
The torii cap instance inherits the corrected parent without its own file changing.

![Recorded scene after material repair](visuals/parry-v1.png)

The capture is a 1920 × 1200 frame from Unreal 5.7.4 on the NVIDIA RTX 5060 Laptop
GPU with Vulkan SM6. The capture log recorded zero remaining shader jobs before
requesting the screenshot, then confirmed a 3,644,030-byte PNG and a normal exit
request. The image SHA-256 is
`da0105a16f1b66e98232f5b85d80f4f2f105e430c7af6d95d3322638484841bd`.

The ground-covered ridges, curved torii and imported foliage are visible. The
large white highlight on the right ridge in the [preceding scene](visuals/courtyard-v3.png)
is absent from this later view, and the stone court appears less reflective.
The camera and combat state differ, so these are not a matched pixel comparison.
The [combat report](2026-10-16-combat-reset.md) records the attack/parry observations.

## Remaining work

- The seven medium-tree placements contain 3,752,072 source LOD0 triangles in
  total, before view culling. Each variant has one LOD and Nanite is disabled;
  this is not a measured GPU triangle count or frame-time cost.
- The fir import's missing smoothing-group, degenerate tangent-basis and nearly
  zero-binormal warnings remain unresolved. The distant capture does not validate
  close-up tangent shading, needle backlighting or motion.
- Terrain cover remains sparse and repeated surface detail is visible. Additional
  views are needed for planting contact, gate surfaces and camera composition.
- The capture used a 30 fps cap and included shader warmup; it establishes no
  sustained-performance or 60 fps result. Packaged Linux behavior and an extended
  interactive visual review remain unverified.
