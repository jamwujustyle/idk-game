"""Read-only hilt and mouth inspection from a temporary camera."""
import bpy
from mathutils import Vector
scene = bpy.context.scene
scene.render.resolution_x, scene.render.resolution_y = 1000, 850
cam = scene.camera
cam.location = (.32,-1,.43)
target = Vector((.035,0,.065))
cam.rotation_euler = (target-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.ortho_scale = .45
scene.render.image_settings.media_type = 'IMAGE'
scene.render.image_settings.file_format = 'PNG'
target = artifacts.file(name='katana-hilt-detail.png',media_type='image/png')
scene.render.filepath = target.path
bpy.ops.render.render(write_still=True)
target.publish()
result = {'view':'Hilt construction, woven geometry, thin tsuba, habaki, and saya mouth',
          'temporary_camera':True,'model_changes':False}
