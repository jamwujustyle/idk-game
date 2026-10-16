# Samurai Sentinel rig analysis

Read-only GLB analysis, 2026-10-16. The original source is preserved as
`meshy-6-lite-rigged-v1.glb`; SHA-256
`737a9ab1c49c7bf685aa9f2ce1db6753145859e7e0654fed87296d8803515efd`.
Exact matrices, bone weights, sampling times, and numeric comparisons are in
`rig-analysis.json`. These findings come from CPU skinning and skeleton math;
no Unreal import, rendered appearance, collisions, or performance were tested.

## Geometry, axes, and feet

The file contains one skinned mesh, 166,055 vertices, 288,176 triangles, one
material, and 32 joints. It is 170 cm tall, 92.26 cm wide, and 38.15 cm deep in
the bind pose. All vertices have weights, with at most four influences; the
largest weight-sum error is 0.000000142. Reconstructing the default pose from
the inverse bind matrices reproduces the mesh within 0.000039 cm.

glTF is **+Y up, +Z forward, +X anatomical left**. Forward is independently
supported by the headfront marker and the ankles-to-toes direction. The local
UE GLTFCore importer converts `(X,Y,Z)` to `(X,Z,Y)` and metres to centimetres.
That yields imported +Y forward and +Z up; a mesh yaw of −90° aligns forward
with actor +X, provided the import applies no additional rotation.

The bind-pose sole is at Y ≈ 0. For an 88 cm capsule half-height, mesh Z = −88
cm is therefore a nominal bind-pose floor alignment at scale 1, subject to
the actual imported root basis. The lowest skinned surface in Walking is
1.16–3.13 cm above the bind floor at the sampled keys. Running reaches
1.28–12.65 cm, including flight phases. Inspect planted-foot frames before
choosing an offset or foot IK; lowering the mesh to remove the running
flight gap would be incorrect.

## Hierarchy and mapping

The deforming root is `mixamorig:Hips`; the non-joint `target_character`
container has identity transform. There is no separate root-motion joint.
The exact hierarchy and bind transforms are in the JSON.

| Meshy joint or chain | Existing Kwang anatomy equivalent | Qualification |
| --- | --- | --- |
| Hips | pelvis | Root translation needs explicit treatment. |
| Spine → Spine1 → Spine2 | spine_01 → spine_02 → spine_03 | Retarget bind bases, not just names. |
| Neck → Head | neck_01 → neck_02 → head | Two Kwang neck transforms must be collapsed or distributed. |
| Left/RightShoulder → Arm → ForeArm → Hand | clavicle → upperarm → lowerarm → hand, matching side | No twist-bone equivalents. |
| Left/RightUpLeg → Leg → Foot → ToeBase | thigh → calf → foot → ball, matching side | Preserve target lengths and foot orientation. |
| Bone_020 → Bone_019 | Right weighted wrist helper chain | No safe named-finger mapping. |
| Bone_025 → Bone_024 | Left weighted wrist helper chain | No safe named-finger mapping. |

All Meshy names above carry the `mixamorig:` prefix except the `Bone_` helpers.
The wrist helper chains are **siblings of Hand under ForeArm**, not Hand
children. Their first joint shares the wrist origin, and the next lies about
7.42 cm down/outward. `Bone_030` and `Bone_029` are zero-weight Hand endpoints
about 23.14 cm laterally from the wrist, outside the hand; they are unsuitable
grip markers. No individual thumb/index/middle/ring/pinky chains, weapon bone,
armor plate bones, cloth bones, or authored attachment sockets are supplied.
Kwang's finger, armor, cloth, twist, weapon, and IK bones therefore cannot be
mapped one-to-one. Named Hand alone does not control all hand geometry.

## Supplied animation and deformation

Only Running and Walking are supplied. There are no idle, combat, guard,
parry, dodge, hit, or death clips. Each clip animates translation and rotation
on all 32 joints. Running lasts 0.666667 s and Walking 1.041667 s. Only Hips
translation reaches those endpoints; the other 63 channels end at 0.633333 s
and 1.033333 s respectively and hold to the clip end. Both loops return nearly
exactly to their start and are in place, with roughly 7 cm horizontal hip
range rather than locomotion travel.

Spatial regions are proxies rather than semantic armor segmentation; their
exact bind-space bounds are recorded in the JSON. Mean skin weights show:

- Shoulder-height regions follow the upper arm by 78–80% and the clavicle by
  15–17%. There is no independent rigid pauldron control.
- Front/rear skirt-height regions follow the two upper legs by 87–88%
  combined and Hips by 12–13%. This cannot enforce rigid kusazuri plates.
- Hand/wrist regions follow Hand by 41–45%, ForeArm by 31–33%, and the two
  wrist helpers by 24–27% combined.

The wrist helpers change orientation relative to Hand by up to 59–87° from
their bind relationship during the supplied locomotion. Numerical skinning
shows substantial associated edge deformation. At Walking source keys,
95th-percentile absolute edge-length change is 33–34% in shoulder proxies,
48% in the front skirt proxy, and 156% / 203% in the left/right hand proxies.
These metrics use unique triangle edges initially longer than 2 mm and pool
all key samples equally. They measure deformation, not visible quality or
intersections; render these areas closely before acceptance.

## Deterministic wrist correction, tested virtually

A derived clip or final animation node can make each helper follow Hand's
rotation change while preserving its bind relationship. With column-vector
component-space matrices, use:

```text
A_current = H_current × inverse(H_bind) × A_bind
C_current = A_current × inverse(A_bind) × C_bind
```

Here H is the named Hand, A is `Bone_020` / `Bone_025`, and C is its child
`Bone_019` / `Bone_024`. Since H and A share ForeArm as parent, the equivalent
local-space formula is:

```text
A_local_current = H_local_current × inverse(H_local_bind) × A_local_bind
C_local_current = C_local_bind
```

This was evaluated without changing the GLB, using all 35 Running and 55
Walking distinct source key times. The correction changes only four bone
transforms. On edges whose vertices have over 95% combined Hand/helper
weight, P95 length change falls from **195–310% to below 0.0011%**. Whole
hand/wrist regions still include mixed ForeArm weights:

| Region and clip | Original P95 length change | Corrected P95 | Edge samples over 10% change, before → after |
| --- | ---: | ---: | ---: |
| Left, Running | 137.46% | 39.8% | 31.77% → 14.56% |
| Right, Running | 122.23% | 32.8% | 23% → 11.23% |
| Left, Walking | 155.71% | 45.35% | 39.06% → 17.69% |
| Right, Walking | 202.96% | 47.95% | 45.7% → 20.23% |

Thus the correction removes the dominant helper mismatch but **does not
eliminate all wrist deformation** or create a closed grip. It moves some
vertices by up to 9.58 cm in Running and 11.42 cm in Walking. Loop closure is
preserved to less than 0.000062 cm in the evaluated region.

For a fixed imported animation set, bake these four tracks into derived
assets after retargeting and retain the original GLB unchanged. For a native
graph that blends or procedurally retargets poses, applying this correction
after the final pose evaluation keeps the relationship exact. Use the final
Hand component transform, then update helper parent and child in hierarchy
order. It needs no separate clock and should freeze with the evaluated pose.
UE `FTransform` multiplication uses a different composition notation than
the column-vector equations above; verify a bind-pose identity case and
component-transform reconstruction before using it. This is a proposed
implementation supported by offline measurements, not an engine-tested fix.

## Original katana attachment candidate

The existing imported `SM_Katana` has a grip-center pivot, tip along local
+Z, cutting edge toward local −X, and thickness along local Y. Its bounds
are `(-3.8,-3.2,-12)` to `(3.8,3.2,90)` cm. These axes and dimensions come from
the existing `katana/unreal-import-audit.json`.

A measured starting point is the centroid of the hand/wrist proxy, weighted
by named Hand influence. It is **not an authored or visually fitted grip
center**. To place that pivot there with the blade pointing character forward
and the cutting edge downward in bind pose, use the following RightHand
relative transform in the importer basis described above:

```text
Translation cm: (-0.246960336, -0.247584995, 9.169885795)
Quaternion XYZW: (-0.597783623, 0.197935307, 0.528157378, 0.569671957)
Scale: (1, 1, 1)
```

The JSON also contains the left-hand candidate and full matrices. Both
rotation determinants are approximately 1, orthogonality error is below
0.00000008, and `HandBind × Socket` reproduces the desired weapon transform
within 0.000000000000008 in matrix elements. This verifies the stated math,
not grip contact. A socket cannot close the absent finger chains or keep a
second hand on the hilt. Confirm the imported bone names/basis, then inspect
palm overlap, thumb clearance, edge direction, forearm twist, and a two-hand
pose before integrating combat. Trail endpoints must follow the original
katana's actual geometry rather than Kwang's longer weapon or old sockets.
