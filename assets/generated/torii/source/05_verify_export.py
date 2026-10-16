"""Verify the saved GLB and write a compact, reproducible export audit."""
import hashlib
import json
from pathlib import Path
import struct

BASE = Path(__file__).resolve().parents[1]
source = BASE / 'ToriiGate.glb'
data = source.read_bytes()
magic, version, length = struct.unpack_from('<III', data)
assert magic == 0x46546C67 and version == 2 and length == len(data)
json_length, json_type = struct.unpack_from('<II', data, 12)
assert json_type == 0x4E4F534A
gltf = json.loads(data[20:20+json_length])
parts = []
for mesh in gltf['meshes']:
    triangles = 0
    for primitive in mesh['primitives']:
        assert primitive.get('mode', 4) == 4
        assert {'POSITION', 'NORMAL', 'TEXCOORD_0'} <= set(primitive['attributes'])
        triangles += gltf['accessors'][primitive['indices']]['count']//3
    assert mesh['name'].startswith('TORII_')
    parts.append({'name': mesh['name'], 'triangles': triangles,
                  'attributes': list(mesh['primitives'][0]['attributes'])})
assert len(parts) == 22 and sum(p['triangles'] for p in parts) == 37064
expected_slots = {'Cedar_Weathered', 'Cap_BlackenedCedar', 'Footing_Stone'}
assert {m['name'] for m in gltf['materials']} == expected_slots
assert len(gltf['images']) == 3
assert all('bufferView' in image and image['mimeType'] == 'image/png' for image in gltf['images'])
assert all('baseColorTexture' in m['pbrMetallicRoughness'] for m in gltf['materials'])
audit = {'path': 'assets/generated/torii/ToriiGate.glb', 'revision': 5,
         'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'glb_version': version,
         'triangles': sum(p['triangles'] for p in parts), 'mesh_count': len(parts),
         'mesh_parts': parts, 'materials': gltf['materials'], 'embedded_images': gltf['images'],
         'nodes': [{k: node[k] for k in ('name', 'mesh', 'children', 'translation', 'rotation', 'scale') if k in node}
                   for node in gltf['nodes']], 'asset': gltf['asset']}
(BASE / 'export-audit.json').write_text(json.dumps(audit, indent=2)+'\n')
print(f'GLB verified: {len(parts)} semantic parts, {audit["triangles"]:,} triangles, 3 textured materials')
