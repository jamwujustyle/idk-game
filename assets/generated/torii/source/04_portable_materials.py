"""Small authored preview maps so the exported GLB retains its material colors.

These mathematical textures are generated here, not sampled from photographs.
Unreal integration uses the project's CC0 cedar and stone textures instead.
"""
import bpy
import numpy as np
import math


def field(size, fu, fv, seed):
    rng = np.random.default_rng(seed)
    grid = rng.random((fv, fu))
    u, v = np.meshgrid(np.arange(size)/size*fu, np.arange(size)/size*fv)
    iu, iv = np.floor(u).astype(int), np.floor(v).astype(int)
    x, y = u-iu, v-iv
    x, y = x*x*(3-2*x), y*y*(3-2*y)
    a, b = grid[iv % fv, iu % fu], grid[iv % fv, (iu+1) % fu]
    c, d = grid[(iv+1) % fv, iu % fu], grid[(iv+1) % fv, (iu+1) % fu]
    return (a*(1-x)+b*x)*(1-y)+(c*(1-x)+d*x)*y


def image(name, pixels):
    height, width, _ = pixels.shape
    if name in bpy.data.images:
        bpy.data.images.remove(bpy.data.images[name], do_unlink=True)
    im = bpy.data.images.new(name, width=width, height=height, alpha=True)
    im.colorspace_settings.name = 'sRGB'
    # Packed PNG and glTF base-color textures contain sRGB-encoded values.
    encoded = pixels.copy()
    linear = pixels[:, :, :3]
    encoded[:, :, :3] = np.where(linear <= .0031308, linear*12.92,
                                 1.055*np.power(linear, 1/2.4)-.055)
    im.pixels.foreach_set(encoded.astype(np.float32).ravel())
    im.pack()
    im['provenance'] = 'Mathematical preview texture authored in 04_portable_materials.py'
    return im


def timber(size, dark=False):
    u, v = np.meshgrid(np.arange(size)/size, np.arange(size)/size)
    coarse = field(size, 9, 3, 13)
    fiber = field(size, 220, 5, 73)
    warp = field(size, 13, 3, 98)
    grain = (0.5+0.5*np.sin(2*math.pi*(u*89+.7*warp+.12*np.sin(v*2*math.pi))))**10
    value = .67+.34*coarse+.22*fiber-.19*grain
    base = np.array([.027, .028, .024] if dark else [.22, .15, .095])
    rgb = value[:, :, None]*base[None, None, :]
    if not dark:
        patina = np.clip((field(size, 5, 4, 27)-.5)*.38, 0, .15)
        rgb = rgb*(1-patina[:, :, None])+np.array([.24, .23, .20])*patina[:, :, None]
    rgba = np.ones((size, size, 4))
    rgba[:, :, :3] = np.clip(rgb, 0, 1)
    return rgba


def rock(size):
    coarse = field(size, 7, 7, 42)
    fine = field(size, 80, 80, 51)
    micro = field(size, 200, 200, 32)
    value = .65+.48*coarse+.14*fine+.07*micro
    rgba = np.ones((size, size, 4))
    rgba[:, :, :3] = value[:, :, None]*np.array([.23, .25, .23])
    return rgba


for mat_name, texture_name, pixels, roughness in [
    ('Cedar_Weathered', 'Torii_Cedar_AuthoredPreview', timber(1024), .73),
    ('Cap_BlackenedCedar', 'Torii_BlackCap_AuthoredPreview', timber(512, True), .73),
    ('Footing_Stone', 'Torii_Stone_AuthoredPreview', rock(512), .82)]:
    mat = bpy.data.materials[mat_name]
    tree = mat.node_tree
    tree.nodes.clear()
    output = tree.nodes.new('ShaderNodeOutputMaterial')
    bsdf = tree.nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.inputs['Roughness'].default_value = roughness
    tree.links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])
    tex = tree.nodes.new('ShaderNodeTexImage')
    tex.image = image(texture_name, pixels)
    tex.extension = 'REPEAT'
    tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    mat.use_backface_culling = True
    mat['review_shader_only'] = False
    mat['Unreal_override'] = 'Use UV cedar variant or M_StoneEdge; preview images are not CC0 source textures'

result = {'portable_materials': ['Cedar_Weathered', 'Cap_BlackenedCedar', 'Footing_Stone'],
          'images': [{'name': im.name, 'dimensions': list(im.size)} for im in bpy.data.images if im.name.startswith('Torii_')],
          'geometry_unchanged': True}
