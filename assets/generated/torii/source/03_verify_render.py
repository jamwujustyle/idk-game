"""Read-only scene query: audit the authored mesh and publish an actual render."""
import bpy
import bmesh
import math
from mathutils import Vector

scene = bpy.context.scene
parts = [o for o in bpy.data.objects['TORII_Root'].children if o.type == 'MESH']
points = [o.matrix_world @ Vector(p) for o in parts for p in o.bound_box]
minimum = [min(p[a] for p in points) for a in range(3)]
maximum = [max(p[a] for p in points) for a in range(3)]
audit = []
for obj in parts:
    mesh = obj.data
    mesh.calc_loop_triangles()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    uv = mesh.uv_layers.active
    zero_uv_triangles = 0
    if uv:
        for tri in mesh.loop_triangles:
            a, b, c = [uv.data[li].uv for li in tri.loops]
            area2 = abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))
            if area2 < 1e-10: zero_uv_triangles += 1
    audit.append({'name': obj.name, 'triangles': len(mesh.loop_triangles),
                  'vertices': len(mesh.vertices), 'uv_layers': [u.name for u in mesh.uv_layers],
                  'zero_uv_triangles': zero_uv_triangles,
                  'nonmanifold_edges': sum(not e.is_manifold for e in bm.edges),
                  'degenerate_faces': sum(f.calc_area() < 1e-10 for f in bm.faces),
                  'signed_volume_m3': bm.calc_volume(signed=True),
                  'materials': [m.name for m in mesh.materials],
                  'has_custom_normals': mesh.has_custom_normals})
    bm.free()

# A temporary review floor is discarded by the query and excluded from the export.
bpy.ops.mesh.primitive_plane_add(size=40, location=(0, 0, -.015))
floor = bpy.context.object
floor.name = 'TEMP_ReviewGround'
mat = bpy.data.materials.new('TEMP_ReviewGroundMat')
mat.use_nodes = True
mat.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (.12, .14, .15, 1)
mat.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value = .85
floor.data.materials.append(mat)
scene.render.image_settings.media_type = 'IMAGE'
scene.render.image_settings.file_format = 'PNG'
target = artifacts.file(name='torii-final-front.png', media_type='image/png')
scene.render.filepath = target.path
bpy.ops.render.render(write_still=True)
target.publish()
result = {'bounds_min_m': minimum, 'bounds_max_m': maximum,
          'dimensions_m': [maximum[a]-minimum[a] for a in range(3)],
          'mesh_count': len(parts), 'triangles': sum(a['triangles'] for a in audit),
          'parts': audit, 'preview': 'torii-final-front.png'}
