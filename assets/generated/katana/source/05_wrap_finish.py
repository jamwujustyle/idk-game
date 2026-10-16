"""Shift opposite wrap phase so diamond crossings sit on the grip faces."""
import bpy
import numpy as np
import math

mesh = bpy.data.objects['KATANA_ItoWrap_B'].data
for vertex in mesh.vertices:
    vertex.co.x *= -1
    vertex.co.y *= -1
mesh.update()

# Steel polish grain is subtle and longitudinal, avoiding visible horizontal bands.
for name, color, seed in [
    ('Blade_SatinSteel',(.48,.53,.58),1),
    ('Blade_PolishedEdge',(.59,.63,.67),2),
    ('Blade_SpineSteel',(.36,.40,.44),3)]:
    im = bpy.data.images['T_'+name]
    n = im.size[0]
    u,v = np.meshgrid(np.arange(n)/n,np.arange(n)/n)
    rng = np.random.default_rng(seed)
    value = .98+.008*np.sin(u*math.tau*91)+.012*rng.random((n,n))
    rgba = np.ones((n,n,4),dtype=np.float32)
    linear = value[:,:,None]*np.array(color)[None,None,:]
    rgba[:,:,:3] = np.where(linear<=.0031308,linear*12.92,1.055*linear**(1/2.4)-.055)
    im.pixels.foreach_set(rgba.ravel())
    im.pack()

result = {'wrap':'Opposing strips cross on front/back grip faces and reveal diamonds',
          'steel':'Subtle longitudinal polish grain; no horizontal bands',
          'dimensions_unchanged':True,'triangles_unchanged':True}
