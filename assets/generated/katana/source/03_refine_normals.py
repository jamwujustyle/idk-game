"""Correct winding/UV end caps and smooth blade longitudinally, retaining ridges."""
import bpy
import bmesh
from mathutils import Vector

fixed = []
for name in ['KATANA_ItoWrap_A','KATANA_ItoWrap_B','SAYA_SageoWrap']:
    mesh = bpy.data.objects[name].data
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(mesh)
    bm.free()
    uv = mesh.uv_layers.active
    for poly in mesh.polygons[-2:]:
        for loop, co in zip(poly.loop_indices, [(0,0),(1,0),(1,1),(0,1)]):
            uv.data[loop].uv = co
    mesh.update()
    fixed.append(name)

mesh = bpy.data.objects['KATANA_Blade'].data
normals = []
segments, profile_size = 96, 7
for poly in mesh.polygons:
    poly.use_smooth = True
    if poly.index >= segments*profile_size:
        normals.extend([tuple(poly.normal)]*len(poly.loop_indices))
        continue
    ring, side = divmod(poly.index, profile_size)
    before = mesh.polygons[max(0,ring-1)*profile_size+side].normal
    after = mesh.polygons[min(segments-1,ring+1)*profile_size+side].normal
    low = (poly.normal+before).normalized()
    high = (poly.normal+after).normalized()
    normals.extend([tuple(low),tuple(low),tuple(high),tuple(high)])
mesh.normals_split_custom_set(normals)
mesh.update()

# A thin fuchi-like horn collar on the saya and the wrap already have real bevels.
# Keep studio lights out of the portable model; Blender retains them for review.
result = {'corrected_outward_winding':fixed,'ribbon_uv_endcaps':'nondegenerate',
          'blade_normals':'smooth along curvature; split at edge, shinogi, and spine'}
