"""Torii form study for the dedicated Higgsfield 3D Jutsu scene; Blender bpy."""
import bpy
import math
from mathutils import Vector

scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = 960
scene.render.resolution_y = 720
scene.render.resolution_percentage = 100
scene.render.film_transparent = False
if scene.world is None:
    scene.world = bpy.data.worlds.new('World_ToriiReview')
scene.world.color = (0.16, 0.18, 0.21)
scene.world.use_nodes = True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.16, 0.18, 0.21, 1)
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.35

root = bpy.data.objects.new('TORII_Root', None)
scene.collection.objects.link(root)

def material(name, color, roughness):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (*color, 1)
    bsdf.inputs['Roughness'].default_value = roughness
    return mat

wood = material('Cedar_Weathered', (0.25, 0.10, 0.045), .72)
cap = material('Cap_BlackenedCedar', (.025, .03, .035), .58)
stone = material('Footing_Stone', (.22, .24, .23), .86)

def finish(obj, name, mat):
    obj.name = name
    obj.parent = root
    obj.data.materials.append(mat)
    return obj

def box(name, center, size, mat):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    obj = finish(bpy.context.object, name, mat)
    obj.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return obj

def curved_beam(name, half_width, bottom, rise, height, depth, mat):
    verts, faces = [], []
    segments = 32
    for i in range(segments + 1):
        x = -half_width + 2 * half_width * i / segments
        z = bottom + rise * (x / half_width) ** 2
        verts.extend([(x, -depth/2, z), (x, depth/2, z),
                      (x, depth/2, z+height), (x, -depth/2, z+height)])
    for i in range(segments):
        for k in range(4):
            faces.append((i*4+k, i*4+(k+1)%4, (i+1)*4+(k+1)%4, (i+1)*4+k))
    faces.extend([(3, 2, 1, 0), tuple(segments*4+k for k in range(4))])
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    finish(obj, name, mat)
    return obj

curved_beam('TORII_Kasagi', 5.45, 5.57, .55, .36, .95, wood)
curved_beam('TORII_KasagiCap', 5.45, 5.93, .55, .09, 1.1, cap)
curved_beam('TORII_Shimaki', 5.1, 5.35, .48, .22, .68, wood)
box('TORII_Nuki', (0, 0, 4.45), (8.7, .46, .32), wood)
box('TORII_Gakuzuka', (0, 0, 5.0), (.28, .44, .78), wood)
for sign, side in [(-1, 'L'), (1, 'R')]:
    bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=.55, depth=.64,
                                       location=(sign*3.3, 0, .32))
    finish(bpy.context.object, 'TORII_Footing_'+side, stone)
    bottom, top = .58, 5.57
    x_bottom = sign * 3.3
    x_top = x_bottom - sign * math.tan(math.radians(2)) * (top-bottom)
    start, end = Vector((x_bottom, 0, bottom)), Vector((x_top, 0, top))
    bpy.ops.mesh.primitive_cone_add(vertices=48, radius1=.35, radius2=.285,
                                   depth=(end-start).length, location=(start+end)/2)
    obj = finish(bpy.context.object, 'TORII_Hashira_'+side, wood)
    obj.rotation_euler = (end-start).to_track_quat('Z', 'Y').to_euler()
    for p in obj.data.polygons:
        p.use_smooth = len(p.vertices) == 4

def aim(obj, target):
    obj.rotation_euler = (Vector(target)-obj.location).to_track_quat('-Z', 'Y').to_euler()

cam_data = bpy.data.cameras.new('CAM_main')
cam = bpy.data.objects.new('CAM_main', cam_data)
scene.collection.objects.link(cam)
cam.location = (12, -22, 9)
cam_data.lens = 48
aim(cam, (0, 0, 3.15))
scene.camera = cam
for name, kind, location, energy, color in [
    ('LGT_key', 'SUN', (-5, -8, 10), 2.3, (1, .91, .79)),
    ('LGT_fill', 'POINT', (5, -4, 8), 1100, (.72, .83, 1))]:
    data = bpy.data.lights.new(name, kind)
    data.energy = energy
    data.color = color
    if kind == 'SUN': data.angle = math.radians(12)
    else: data.shadow_soft_size = 4
    obj = bpy.data.objects.new(name, data)
    scene.collection.objects.link(obj)
    obj.location = location
    aim(obj, (0, 0, 3))

result = {'stage': 'blockout', 'mesh_names': [o.name for o in root.children],
          'source_axes': 'X width, Y depth; front faces -Y; Z up; origin at base center',
          'target_dimensions_m': [10.9, 1.1, 6.57]}
