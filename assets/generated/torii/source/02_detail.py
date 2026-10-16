"""Authored myojin torii, metre units; Blender 5.2 / Higgsfield 3D Jutsu.

Run after 01_blockout.py. All geometry is created here, without imported models.
UV0: cedar U crosses the grain and V follows it; one UV unit is 1.1 m.
The procedural review shaders are not the Unreal material delivery.
"""
import bpy
import math
from mathutils import Vector

scene = bpy.context.scene
root = bpy.data.objects['TORII_Root']
for obj in list(root.children):
    bpy.data.objects.remove(obj, do_unlink=True)
wood = bpy.data.materials['Cedar_Weathered']
cap = bpy.data.materials['Cap_BlackenedCedar']
stone = bpy.data.materials['Footing_Stone']


def review_shader(mat, base, kind):
    mat.use_nodes = True
    mat.diffuse_color = (*base, 1)
    tree = mat.node_tree
    tree.nodes.clear()
    output = tree.nodes.new('ShaderNodeOutputMaterial')
    bsdf = tree.nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.inputs['Base Color'].default_value = (*base, 1)
    bsdf.inputs['Roughness'].default_value = .82 if kind == 'stone' else .73
    tree.links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])
    tex = tree.nodes.new('ShaderNodeTexCoord')
    scale = tree.nodes.new('ShaderNodeVectorMath')
    scale.operation = 'MULTIPLY'
    scale.inputs[1].default_value = (16, 16, 16) if kind == 'stone' else (65, 1.7, 1)
    tree.links.new(tex.outputs['Generated' if kind == 'stone' else 'UV'], scale.inputs[0])
    noise = tree.nodes.new('ShaderNodeTexNoise')
    noise.inputs['Scale'].default_value = 1
    noise.inputs['Detail'].default_value = 3
    noise.inputs['Roughness'].default_value = .72
    tree.links.new(scale.outputs['Vector'], noise.inputs['Vector'])
    ramp = tree.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = .18
    ramp.color_ramp.elements[0].color = (*(c * .48 for c in base), 1)
    ramp.color_ramp.elements[1].position = .84
    ramp.color_ramp.elements[1].color = (*(min(c * 1.65, 1) for c in base), 1)
    tree.links.new(noise.outputs['Fac'], ramp.inputs['Fac'])
    tree.links.new(ramp.outputs['Color'], bsdf.inputs['Base Color'])
    bump = tree.nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = .28
    bump.inputs['Distance'].default_value = .009 if kind == 'stone' else .004
    tree.links.new(noise.outputs['Fac'], bump.inputs['Height'])
    tree.links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])
    mat['Unreal_material'] = 'M_StoneEdge' if kind == 'stone' else 'M_Cedar'
    mat['review_shader_only'] = True


review_shader(wood, (.22, .15, .095), 'wood')
review_shader(cap, (.027, .028, .024), 'wood')
review_shader(stone, (.23, .25, .23), 'stone')


def mesh_object(name, vertices, faces, face_uvs, material, normals=None, side_faces=0):
    mesh = bpy.data.meshes.new(name + '_Mesh')
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    uv = mesh.uv_layers.new(name='UVMap')
    for poly, coords in zip(mesh.polygons, face_uvs):
        for loop_index, coord in zip(poly.loop_indices, coords):
            uv.data[loop_index].uv = coord
        poly.use_smooth = poly.index < side_faces
    if normals is not None:
        loop_normals = []
        for poly in mesh.polygons:
            for vi in poly.vertices:
                loop_normals.append(normals[vi] if poly.index < side_faces else poly.normal)
        mesh.normals_split_custom_set(loop_normals)
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    obj.parent = root
    obj.data.materials.append(material)
    obj['uv_scale_m'] = 1.1
    obj['source_units'] = 'metres'
    return obj


def loft(name, sections, material, lengths, normals=None):
    """Closed, indexed loft with a UV seam and explicitly flat end caps."""
    rings, count = len(sections), len(sections[0])
    vertices = [tuple(p) for section in sections for p in section]
    perimeter = [0.0]
    first = [Vector(p) for p in sections[0]]
    for j in range(count):
        perimeter.append(perimeter[-1] + (first[(j+1) % count] - first[j]).length)
    faces, uvs = [], []
    for i in range(rings - 1):
        for j in range(count):
            k = (j + 1) % count
            faces.append((i*count+j, i*count+k, (i+1)*count+k, (i+1)*count+j))
            u0, u1 = perimeter[j]/1.1, perimeter[j+1]/1.1
            v0, v1 = lengths[i]/1.1, lengths[i+1]/1.1
            uvs.append(((u0, v0), (u1, v0), (u1, v1), (u0, v1)))
    side_faces = len(faces)
    for i, reverse in [(0, True), (rings-1, False)]:
        indices = list(range(count))
        if reverse: indices.reverse()
        faces.append(tuple(i*count+j for j in indices))
        # Planar caps; dominant axis is discarded to prevent a collapsed island.
        extents = [max(p[a] for p in sections[i])-min(p[a] for p in sections[i]) for a in range(3)]
        axes = sorted(range(3), key=lambda a: extents[a], reverse=True)[:2]
        uvs.append(tuple((sections[i][j][axes[0]]/1.1, sections[i][j][axes[1]]/1.1) for j in indices))
    if normals is None:
        normals = []
        for i in range(rings):
            for j in range(count):
                around = Vector(sections[i][(j+1) % count])-Vector(sections[i][(j-1) % count])
                along = Vector(sections[min(i+1, rings-1)][j])-Vector(sections[max(i-1, 0)][j])
                normals.append(around.cross(along).normalized())
    return mesh_object(name, vertices, faces, uvs, material, normals, side_faces)


def rounded_rectangle(depth, height, radius, corner_steps=4):
    """CCW YZ profile plus exact profile normals; straight faces remain flat."""
    points = []
    for cy, cz, start in [(depth/2-radius, radius, -90),
                          (depth/2-radius, height-radius, 0),
                          (-depth/2+radius, height-radius, 90),
                          (-depth/2+radius, radius, 180)]:
        for k in range(corner_steps+1):
            angle = math.radians(start + 90*k/corner_steps)
            ny, nz = math.cos(angle), math.sin(angle)
            points.append((cy+radius*ny, cz+radius*nz, ny, nz))
    return points


def beam(name, half_width, bottom, rise, height, depth, material, segments, bevel, wear=.0015):
    profile = rounded_rectangle(depth, height, bevel)
    sections, normals, lengths = [], [], []
    last_center = None
    distance = 0
    for i in range(segments+1):
        x = -half_width + 2*half_width*i/segments
        t = x/half_width
        zbase = bottom + rise*t*t
        slope = 2*rise*x/(half_width*half_width)
        center = Vector((x, 0, zbase))
        if last_center is not None: distance += (center-last_center).length
        lengths.append(distance)
        last_center = center
        section = []
        for j, (y, z, ny, nz) in enumerate(profile):
            # Restrained waviness stays under 2 mm and fades at the ends.
            erosion = wear * math.sin(math.pi*i/segments) * math.sin(7*x + j*.63)
            section.append((x, y-erosion*ny, zbase+z-abs(erosion)*nz))
            normals.append(Vector((-slope*nz, ny, nz)).normalized())
        sections.append(section)
    obj = loft(name, sections, material, lengths, normals)
    obj['grain_direction'] = 'longitudinal V'
    return obj


beam('TORII_Kasagi', 5.45, 5.57, .55, .36, .95, wood, 112, .018)
beam('TORII_KasagiCap', 5.45, 5.93, .55, .09, 1.1, cap, 112, .012, .0006)
beam('TORII_Shimaki', 5.1, 5.35, .48, .22, .68, wood, 96, .013)
beam('TORII_Nuki', 4.35, 4.29, 0, .32, .46, wood, 80, .016)


def upright(name, x, bottom, top, rbottom, rtop, lean, phase):
    sections, lengths = [], []
    radial, levels = 96, 42
    # The taper is continuous, with a soft shoulder and foot rather than razor rims.
    for i in range(levels+1):
        t = i/levels
        z = bottom + (top-bottom)*t
        xc = x + lean*(z-bottom)
        r = rbottom + (rtop-rbottom)*t
        if i == 0 or i == levels: r -= .008
        section = []
        for j in range(radial):
            a = 2*math.pi*j/radial
            weather = .0016*math.sin(13*a + .5*math.sin(3*t+phase)) + .0011*math.sin(29*a + t)
            fissure = 0.0
            for center, length, shift in [(1.63, .66, .13), (3.76, .43, .49), (5.31, .54, .21)]:
                d = abs(math.atan2(math.sin(a-center-phase*.16), math.cos(a-center-phase*.16)))
                envelope = max(0, math.sin(math.pi*min(max((t-shift)/length, 0), 1)))
                fissure += .006 * math.exp(-(d/.04)**2) * envelope
            rr = r + weather - fissure
            section.append((xc+rr*math.cos(a), rr*math.sin(a), z))
        sections.append(section)
        lengths.append((z-bottom)*math.sqrt(1+lean*lean))
    obj = loft(name, sections, wood, lengths)
    obj['inward_lean_degrees'] = 2
    obj['grain_direction'] = 'vertical V'
    return obj


for sign, side in [(-1, 'L'), (1, 'R')]:
    upright('TORII_Hashira_'+side, sign*3.3, .58, 5.57, .35, .285,
            -sign*math.tan(math.radians(2)), 1 if sign < 0 else 2.4)


def footing(name, x, phase):
    # Octagonal dressed stone profile with rounded corners, chamfers and a socket rim.
    rings = [(0, .47), (.025, .52), (.07, .53), (.12, .525),
             (.48, .50), (.55, .49), (.60, .46), (.64, .43)]
    radial = 64
    sections = []
    for z, radius in rings:
        row = []
        for j in range(radial):
            a = 2*math.pi*j/radial
            octagonal = math.cos(math.pi/8) / math.cos((a+math.pi/8)%(math.pi/4)-math.pi/8)
            # Small scan-like irregularity, bounded away from the exterior size limit.
            rough = .0025*math.sin(7*a+phase+z*3)+.0015*math.sin(19*a+z*13)
            r = radius*octagonal + rough
            row.append((x+r*math.cos(a), r*math.sin(a), z))
        sections.append(row)
    return loft(name, sections, stone, [r[0] for r in rings])


footing('TORII_Footing_L', -3.3, 0)
footing('TORII_Footing_R', 3.3, 1.6)


def beveled_box(name, center, dimensions, material, bevel=.01, segments=3):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    obj = bpy.context.object
    obj.name = name
    obj.scale = dimensions
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.parent = root
    obj.data.materials.append(material)
    mod = obj.modifiers.new('Physical edge bevel', 'BEVEL')
    mod.width = bevel
    mod.segments = segments
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)
    # Deterministic box mapping, with grain running along the longest dimension.
    uv = obj.data.uv_layers.active or obj.data.uv_layers.new(name='UVMap')
    long_axis = max(range(3), key=lambda a: dimensions[a])
    for poly in obj.data.polygons:
        face_axis = max(range(3), key=lambda a: abs(poly.normal[a]))
        axes = [a for a in range(3) if a != face_axis]
        if axes[0] == long_axis: axes.reverse()
        for li in poly.loop_indices:
            p = obj.data.vertices[obj.data.loops[li].vertex_index].co
            uv.data[li].uv = (p[axes[0]]/1.1, p[axes[1]]/1.1)
    obj['uv_scale_m'] = 1.1
    return obj


beveled_box('TORII_Gakuzuka', (0, 0, 5.0), (.28, .44, .78), wood, .012)
# A small, unlettered weathered dedication board; no fabricated inscription.
beveled_box('TORII_Plaque', (0, -.253, 5.04), (.49, .075, .66), cap, .012)
for sign in [-1, 1]:
    beveled_box('TORII_PlaqueFrame_'+('L' if sign < 0 else 'R'),
                (sign*.231, -.301, 5.04), (.045, .035, .65), wood, .004, 2)
for z, part in [(4.727, 'Bottom'), (5.353, 'Top')]:
    beveled_box('TORII_PlaqueFrame_'+part, (0, -.301, z), (.48, .035, .04), wood, .004, 2)


def wedge(name, x, y, z, sign):
    # A solid kusabi driven across the post/nuki joint. The narrow end enters the post.
    obj = beveled_box(name, (x, y, z), (.60, .14, .16), wood, .006, 2)
    for vertex in obj.data.vertices:
        t = vertex.co.x/.60 + .5
        vertex.co.z *= .45 + .55*(t if sign > 0 else 1-t)
    obj.data.update()
    return obj


def peg(name, center):
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=.028, depth=.055,
                                       location=center, rotation=(math.pi/2, 0, 0))
    obj = bpy.context.object
    obj.name = name
    obj.parent = root
    obj.data.materials.append(cap)
    mod = obj.modifiers.new('Dowel end chamfer', 'BEVEL')
    mod.width, mod.segments = .003, 2
    bpy.ops.object.modifier_apply(modifier=mod.name)
    return obj


for sign, side in [(-1, 'L'), (1, 'R')]:
    x = sign*(3.3-math.tan(math.radians(2))*(4.45-.58))
    for front, y in [('Front', -.282), ('Back', .282)]:
        wedge('TORII_Kusabi_'+side+'_'+front, x, y, 4.59, sign)
        peg('TORII_Dowel_'+side+'_'+front, (x-sign*.11, y*1.01, 4.42))


root['asset_type'] = 'static architectural mesh; authored geometry'
root['source_axes'] = 'X width, Y depth; front -Y; Z up'
root['pivot'] = 'base centre at (0,0,0) metres'
root['target_dimensions_m'] = [10.9, 1.1, 6.57]
root['material_bindings'] = 'Cedar_Weathered=M_Cedar; Cap_BlackenedCedar=tinted M_Cedar; Footing_Stone=M_StoneEdge'
scene.render.resolution_x = 960
scene.render.resolution_y = 720
scene.camera.data.lens = 53
scene.view_settings.exposure = .5
mesh_objects = [o for o in root.children if o.type == 'MESH']
for obj in mesh_objects:
    obj.data.name = obj.name + '_Mesh'
    obj.data.calc_loop_triangles()
result = {'stage': 'detailed LOD0', 'mesh_count': len(mesh_objects),
          'triangles': sum(len(o.data.loop_triangles) for o in mesh_objects),
          'material_slots': sorted({m.name for o in mesh_objects for m in o.data.materials})}
