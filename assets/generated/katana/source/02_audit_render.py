"""Read-only mesh audit and actual Blender review render; run as a query."""
import bpy
import bmesh
from mathutils import Vector

scene = bpy.context.scene
meshes = [o for o in scene.objects if o.type == 'MESH']
audit = []
for obj in meshes:
    mesh = obj.data
    mesh.calc_loop_triangles()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    uv = mesh.uv_layers.active
    zero_uv = 0
    for tri in mesh.loop_triangles:
        a, b, c = [uv.data[q].uv for q in tri.loops]
        if abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)) < 1e-12:
            zero_uv += 1
    audit.append({'name':obj.name,'vertices':len(mesh.vertices),'triangles':len(mesh.loop_triangles),
        'dimensions_m':list(obj.dimensions),'origin_m':list(obj.location),
        'parent':obj.parent.name if obj.parent else None,
        'materials':[m.name for m in mesh.materials],
        'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),
        'zero_faces':sum(f.calc_area() < 1e-14 for f in bm.faces),
        'signed_volume_m3':bm.calc_volume(signed=True),'zero_uv_triangles':zero_uv})
    bm.free()

points = [o.matrix_world@Vector(c) for o in meshes if o.name.startswith('KATANA_') for c in o.bound_box]
low = [min(v[k] for v in points) for k in range(3)]
high = [max(v[k] for v in points) for k in range(3)]
scene.render.image_settings.media_type = 'IMAGE'
scene.render.image_settings.file_format = 'PNG'
target = artifacts.file(name='katana-full-review.png',media_type='image/png')
scene.render.filepath = target.path
bpy.ops.render.render(write_still=True)
target.publish()
result = {'meshes':audit,'total_triangles':sum(m['triangles'] for m in audit),
          'sword_bounds_m':[low,high],'sword_dimensions_m':[high[k]-low[k] for k in range(3)],
          'source_units':'metres','render':'actual Blender EEVEE scene, no generated image'}
