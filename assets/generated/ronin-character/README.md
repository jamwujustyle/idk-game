# Original armored samurai reference

Use [`reference-samurai-front-v2.png`](reference-samurai-front-v2.png) for the selected design: practical dark lacquer lamellar cuirass and kusazuri, broad layered shoulder guards, forearm and shin armor, muted oxblood lacing, and worn indigo cloth. The front view shows the full body in an A-pose with open hands and separate limbs. [`reference-front-v1.png`](reference-front-v1.png) is the superseded unarmored ronin design; do not use it for modeling or generation.

This is a **2D reference only**. No character mesh, skeleton, skin weights, animation clips, or Unreal character integration has been delivered from either image. The folder name is a technical working path, not the selected character identity or game title. Both PNGs are 1024 × 1536 pixels; their verified paths, SHA-256 hashes, and selection state are recorded in [`reference-request.json`](reference-request.json). Its `selected_prompt` is a descriptive modeling brief checked against v2, while the old prompt is explicitly superseded.

## Validate the eventual model

1. Inspect the actual exported mesh from front, side, and back. Check scale and axes, face and fingers, separated limbs, armor layering, topology, normals, UVs, and PBR texture assignments. The single front reference does not specify hidden surfaces or prove their quality.
2. Inspect the actual skeleton, bind pose, joint names, inverse bind matrices, and skin weights. Test shoulders, wrists, elbows, knees, and hanging armor through movement; a successful automatic rig is not proof that deformation is usable.
3. Build retarget mappings from the delivered bone hierarchy, then verify locomotion, attack, guard, dodge, foot contact, root motion, and weapon-hand alignment in Unreal before replacing the active character. Judge the resulting gameplay capture separately from this reference image.

[`inspect_model.py`](inspect_model.py) is a preparatory structural GLB inspector. It requires skeleton/skin data and geometry, normal, UV, joint, and weight attributes; reports actual bone names, triangle counts, materials, images, animations, and weight-sum errors; and rejects unweighted vertices or out-of-range skin joints. It does not create a model or validate appearance, topology, deformation, or retargeting. No character GLB exists here to inspect yet.

## Optional web workflow

On **October 16, 2026**, [Meshy's official pricing page](https://www.meshy.ai/pricing) lists its free plan with 100 monthly credits, 10 Meshy 6 Lite downloads per month, and no API access. This is an optional web workflow reference, not a submitted generation or a guarantee of rig quality. Recheck the current limits and the output's applicable license before choosing that route; evaluate the actual exported asset using the checks above.
