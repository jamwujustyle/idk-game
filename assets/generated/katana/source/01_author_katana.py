"""Original 1.02 m katana and separate saya, authored in Blender metres.

Run in the dedicated 3D Jutsu project. All geometry and texture pixels are
authored mathematically; no third-party models or paid image generation.
"""
import bpy
import math
import numpy as np
from mathutils import Vector

scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x, scene.render.resolution_y = 1000, 1000
scene.render.resolution_percentage = 100
scene.render.film_transparent = False
scene.world = bpy.data.worlds.new('World_KatanaReview')
scene.world.use_nodes = True
bg = scene.world.node_tree.nodes['Background']
bg.inputs['Color'].default_value = (.18, .21, .26, 1)
bg.inputs['Strength'].default_value = .45

def root(name, location=(0, 0, 0)):
    o = bpy.data.objects.new(name, None)
    scene.collection.objects.link(o)
    o.location = location
    return o

sword = root('KATANA_Root')
saya = root('SAYA_Root', (.19, 0, .18))
sword['pivot'] = 'Grip center; +Z points to blade tip; cutting edge is -X'
saya['pivot'] = 'Mouth center; +Z points toward closed tip; remove presentation translation for attachment'

def material(name, color, roughness, metallic=0, kind='plain', seed=1):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (*color, 1)
    bsdf.inputs['Roughness'].default_value = roughness
    bsdf.inputs['Metallic'].default_value = metallic
    # Explicit embedded color map keeps portable GLB independent of procedural nodes.
    n = 512
    rng = np.random.default_rng(seed)
    u, v = np.meshgrid(np.arange(n)/n, np.arange(n)/n)
    grain = rng.random((n, n))
    if kind == 'cloth':
        warp = .5+.5*np.sin(u*math.tau*90)
        weft = .5+.5*np.sin(v*math.tau*140)
        value = .78+.12*warp+.10*weft+.10*grain
    elif kind == 'skin':
        du = (u*48+.25*np.sin(v*math.tau*40)) % 1-.5
        dv = (v*110) % 1-.5
        bumps = np.maximum(0, 1-(du*du+dv*dv)*5)**2
        value = .62+.45*bumps+.1*grain
    elif kind == 'lacquer':
        value = .92+.045*np.sin(u*math.tau*64+np.sin(v*math.tau*3))+.06*grain
    elif kind == 'steel':
        value = .94+.04*np.sin(v*math.tau*180)+.025*grain
    else:
        value = .88+.16*grain
    pixels = np.ones((n, n, 4), dtype=np.float32)
    linear = np.clip(value[:, :, None]*np.array(color)[None, None, :], 0, 1)
    pixels[:, :, :3] = np.where(linear <= .0031308, linear*12.92, 1.055*linear**(1/2.4)-.055)
    im = bpy.data.images.new('T_'+name, width=n, height=n, alpha=True)
    im.colorspace_settings.name = 'sRGB'
    im.pixels.foreach_set(pixels.ravel())
    im.pack()
    im['provenance'] = 'Original mathematical texture; source/01_author_katana.py'
    tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
    tex.image = im
    tex.extension = 'REPEAT'
    mat.node_tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    mat.use_backface_culling = True
    return mat

steel = material('Blade_SatinSteel', (.48, .53, .58), .27, 1, 'steel')
edge = material('Blade_PolishedEdge', (.59, .63, .67), .19, 1, 'steel', 2)
spine = material('Blade_SpineSteel', (.36, .40, .44), .34, 1, 'steel', 3)
iron = material('Fittings_BlackIron', (.045, .052, .061), .53, .85, seed=4)
copper = material('Habaki_WornCopper', (.43, .19, .075), .38, .88, seed=5)
cloth = material('Wrap_WornIndigo', (.023, .052, .078), .88, kind='cloth', seed=6)
skin = material('Grip_DarkSamegawa', (.12, .105, .075), .86, kind='skin', seed=7)
lacquer = material('Saya_CharcoalLacquer', (.038, .028, .022), .36, kind='lacquer', seed=8)

def mesh_object(name, verts, faces, uvs, mats, root_obj, indices=None, smooth=False):
    mesh = bpy.data.meshes.new(name+'_Mesh')
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    uv = mesh.uv_layers.new(name='UVMap')
    for poly, coords in zip(mesh.polygons, uvs):
        poly.use_smooth = smooth
        for loop, co in zip(poly.loop_indices, coords):
            uv.data[loop].uv = co
    for mat in mats:
        mesh.materials.append(mat)
    if indices:
        for p, i in zip(mesh.polygons, indices): p.material_index = i
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    obj.parent = root_obj
    obj['asset_role'] = 'export'
    return obj

def loft(name, rings, mats, root_obj, mat_edges=None, smooth=False):
    n, count = len(rings[0]), len(rings)
    verts = [p for ring in rings for p in ring]
    faces, uv, indices = [], [], []
    for r in range(count-1):
        for j in range(n):
            faces.append((r*n+j, r*n+(j+1)%n, (r+1)*n+(j+1)%n, (r+1)*n+j))
            uv.append([(j/n, r/(count-1)), ((j+1)/n, r/(count-1)),
                       ((j+1)/n, (r+1)/(count-1)), (j/n, (r+1)/(count-1))])
            indices.append(mat_edges[j] if mat_edges else 0)
    faces.extend([tuple(reversed(range(n))), tuple((count-1)*n+j for j in range(n))])
    for r in (0, count-1):
        ring = rings[r]
        lo = [min(p[k] for p in ring) for k in (0, 1)]
        hi = [max(p[k] for p in ring) for k in (0, 1)]
        coords = [((p[0]-lo[0])/max(hi[0]-lo[0], 1e-6), (p[1]-lo[1])/max(hi[1]-lo[1], 1e-6)) for p in ring]
        uv.append(list(reversed(coords)) if r == 0 else coords)
        indices.append(0)
    obj = mesh_object(name, verts, faces, uv, mats, root_obj, indices, smooth)
    for p in obj.data.polygons[-2:]: p.use_smooth = False
    return obj

def ellipse_ring(z, rx, ry, cx=0, n=48):
    return [(cx+rx*math.cos(j*math.tau/n), ry*math.sin(j*math.tau/n), z) for j in range(n)]

def bevel(obj, width, segments=2):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    modifier = obj.modifiers.new('Manufactured edge radius', 'BEVEL')
    modifier.width, modifier.segments = width, segments
    modifier.limit_method = 'ANGLE'
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    obj.select_set(False)

# Seven-sided shinogi cross section. Small waviness moves the bevel/hamon boundary,
# not the razor edge. The 45 mm kissaki converges toward the spine side.
blade_rings = []
for i in range(97):
    t = i/96
    z = .18+.72*t
    width = .032-.008*t
    thick = .0066-.0026*t
    tip = max(0, (t-(1-.045/.72))/(.045/.72))
    width *= max(.006, 1-tip)
    thick *= max(.035, 1-tip)
    cx = .021*t*t + .012*tip
    wave = .018*math.sin(t*math.tau*19)
    profile = [(-.5, 0), (-.31+wave, -.28), (.09, -.5), (.5, -.38),
               (.5, .38), (.09, .5), (-.31+wave, .28)]
    blade_rings.append([(cx+x*width, y*thick, z) for x, y in profile])
blade = loft('KATANA_Blade', blade_rings, [steel, edge, spine], sword,
             [1, 0, 2, 2, 2, 0, 1])
blade['blade_length_m'] = .72
blade['section'] = 'Shinogi-zukuri inspired original profile; sharp edge -X, spine +X'

# Slight waist and curvature in a 24 cm oval grip, with solid woven ribbon geometry.
grip_rings = []
for i in range(25):
    t = i/24
    factor = 1-.085*math.sin(math.pi*t)
    grip_rings.append(ellipse_ring(-.118+.238*t, .0145*factor, .0102*factor))
loft('KATANA_GripCore', grip_rings, [skin], sword, smooth=True)

def ribbon(name, start, length, turns, sign=1, rx=.015, ry=.0107, width=.009, depth=.0008, root_obj=sword):
    rings = []
    steps = int(turns*72)
    for i in range(steps+1):
        t = i/steps
        angle = sign*turns*math.tau*t
        waist = 1-.08*math.sin(math.pi*t) if root_obj == sword else 1
        normal = Vector((math.cos(angle)/rx, math.sin(angle)/ry, 0)).normalized()
        # Alternate radial stacking at front/back crossings to read as woven.
        radial = .00045*(.5+.5*math.cos(angle*2+ (0 if sign == 1 else math.pi)))
        base = Vector((rx*waist*math.cos(angle), ry*waist*math.sin(angle), start+length*t))+normal*radial
        # Cross section CCW when viewed along strip tangent, four watertight faces.
        verts = [base + normal*d + Vector((0, 0, dz)) for d, dz in
                 [(-depth/2, -width/2), (depth/2, -width/2), (depth/2, width/2), (-depth/2, width/2)]]
        if sign < 0: verts.reverse()
        rings.append([tuple(v) for v in verts])
    return loft(name, rings, [cloth], root_obj, smooth=False)

ribbon('KATANA_ItoWrap_A', -.102, .208, 9.5, 1)
ribbon('KATANA_ItoWrap_B', -.102, .208, 9.5, -1)

# Annular fittings use real inner walls and planar annulus UVs.
def collar(name, z0, z1, rx, ry, inner_rx, inner_ry, mat, root_obj, n=48, cx=0):
    rings = [ellipse_ring(z0, rx, ry, cx, n), ellipse_ring(z1, rx, ry, cx, n),
             ellipse_ring(z1, inner_rx, inner_ry, cx, n), ellipse_ring(z0, inner_rx, inner_ry, cx, n)]
    verts = [p for ring in rings for p in ring]
    faces, uvs = [], []
    for r in range(4):
        nxt = (r+1)%4
        for j in range(n):
            ids = (r*n+j, r*n+(j+1)%n, nxt*n+(j+1)%n, nxt*n+j)
            faces.append(ids)
            if r in (1, 3):
                uvs.append([(.5+(verts[q][0]-cx)/(2.1*rx), .5+verts[q][1]/(2.1*ry)) for q in ids])
            else:
                uvs.append([(j/n, 0), ((j+1)/n, 0), ((j+1)/n, 1), (j/n, 1)])
    return mesh_object(name, verts, faces, uvs, [mat], root_obj, smooth=False)

tsuba = collar('KATANA_Tsuba', .125, .130, .038, .032, .016, .006, iron, sword, n=64)
bevel(tsuba, .0009, 3)
fuchi = collar('KATANA_Fuchi', .108, .122, .0153, .011, .0138, .0095, iron, sword)
bevel(fuchi, .00035)
kashira = loft('KATANA_Kashira', [ellipse_ring(-.12, .0128, .0088), ellipse_ring(-.117, .015, .0105),
               ellipse_ring(-.106, .015, .0105)], [iron], sword, smooth=True)
bevel(kashira, .0004)
for z, suffix in [(.122, 'Grip'), (.13, 'Blade')]:
    washer = collar('KATANA_Seppa_'+suffix, z, z+.0015, .019, .0125, .013, .004, copper, sword)
    bevel(washer, .0003)
habaki = loft('KATANA_Habaki', [ellipse_ring(.132, .0178, .0055), ellipse_ring(.143, .0175, .0054),
                               ellipse_ring(.18, .0163, .0044)], [copper], sword)
bevel(habaki, .00035)

# A small bamboo peg through the grip is visible inside a diamond opening.
bamboo = material('Peg_SmokedBamboo', (.22, .14, .061), .74, seed=9)
bpy.ops.mesh.primitive_cylinder_add(vertices=20, radius=.0023, depth=.0228, location=(0, 0, -.006))
peg = bpy.context.object
peg.name = 'KATANA_Mekugi'
peg.data.name = peg.name+'_Mesh'
peg.rotation_euler.x = math.pi/2
peg.parent = sword
peg.data.materials.append(bamboo)
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
bevel(peg, .0003)

# Saya is hollow at the mouth. The internal tube closes before the rounded tip.
# Presentation placement is separate from its mouth-centred local geometry.
n, steps = 48, 80
outer, inner = [], []
for i in range(steps+1):
    t = i/steps
    taper = .65+.35*min(1, (1-t)*18)
    x = .021*t*t
    outer.append(ellipse_ring(.754*t, (.0202-.003*t)*taper, (.009-.0017*t)*taper, x, n))
for i in range(steps+1):
    t = i/steps
    inner.append(ellipse_ring(.741*t, .0177-.004*t, .0064-.0013*t, .021*t*t, n))
verts = [p for ring in outer+list(reversed(inner)) for p in ring]
rings = len(outer)+len(inner)
faces, uv = [], []
for r in range(rings-1):
    for j in range(n):
        faces.append((r*n+j, r*n+(j+1)%n, (r+1)*n+(j+1)%n, (r+1)*n+j))
        uv.append([(j/n,r/(rings-1)),((j+1)/n,r/(rings-1)),((j+1)/n,(r+1)/(rings-1)),(j/n,(r+1)/(rings-1))])
for j in range(n):
    ids = ((rings-1)*n+j,(rings-1)*n+(j+1)%n,(j+1)%n,j)
    faces.append(ids)
    uv.append([(.5+verts[q][0]/.05,.5+verts[q][1]/.025) for q in ids])
saya_body = mesh_object('SAYA_LacquerShell', verts, faces, uv, [lacquer], saya, smooth=True)
mouth = collar('SAYA_Koiguchi', -.003, .012, .0209, .0097, .0177, .0064, iron, saya)
bevel(mouth, .0004)
tip = loft('SAYA_Kojiri', [ellipse_ring(.738, .0158, .007, .0202),
                          ellipse_ring(.75, .0137, .006, .021),
                          ellipse_ring(.758, .008, .004, .0215)], [iron], saya, smooth=True)
bevel(tip, .0005)
ribbon('SAYA_SageoWrap', .075, .063, 4.5, 1, .0203, .0097, .008, .001, saya)

# All authoring transforms are baked except the explicit saya presentation offset.
for obj in list(sword.children)+list(saya.children):
    if obj.type == 'MESH':
        obj.data.name = obj.name+'_Mesh'
        obj['asset_role'] = 'export'
        obj.data.validate(verbose=False)
        obj.data.update()

def aim(obj, target):
    obj.rotation_euler = (Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()

camdata = bpy.data.cameras.new('CAM_KatanaReview')
cam = bpy.data.objects.new('CAM_KatanaReview', camdata)
scene.collection.objects.link(cam)
cam.location = (.65, -3, 1.02)
camdata.type, camdata.ortho_scale = 'ORTHO', 1.25
aim(cam, (.09, 0, .41))
scene.camera = cam
for name, loc, energy, size, color in [
    ('LGT_Key',(-1.2,-1.6,1.8),130,1.5,(.9,.95,1)),
    ('LGT_Strip',(.5,.8,1.4),180,1.2,(1,.85,.68)),
    ('LGT_Fill',(1.1,-.5,.2),55,1.1,(.68,.79,1))]:
    data = bpy.data.lights.new(name,'AREA')
    data.energy, data.shape, data.size, data.color = energy, 'DISK', size, color
    light = bpy.data.objects.new(name,data)
    scene.collection.objects.link(light)
    light.location = loc
    aim(light,(.05,0,.4))

result = {'stage':'authored geometry and portable PBR', 'sword_meshes':[o.name for o in sword.children],
          'saya_meshes':[o.name for o in saya.children], 'length_m':1.02,'blade_length_m':.72,
          'orientation':'Z tip, -X cutting edge, Y thickness; sword pivot grip center; saya pivot mouth center'}
