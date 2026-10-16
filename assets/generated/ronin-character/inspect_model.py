"""Inspect a delivered GLB character before Unreal import; never creates a model.

Usage: python3 inspect_model.py model.glb --output inspection.json
Requires a real GLB with skinning, geometry, normals and UV0. Reports the actual
bone names so retarget mapping can be built from evidence rather than guessed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect(path):
    raw = path.read_bytes()
    magic, version, declared_size = struct.unpack_from('<III', raw)
    assert magic == 0x46546C67 and version == 2 and declared_size == len(raw), 'Invalid GLB header'
    chunks = {}
    offset = 12
    while offset < len(raw):
        size, kind = struct.unpack_from('<II', raw, offset)
        chunks[kind] = raw[offset+8:offset+8+size]
        offset += 8+size
    doc = json.loads(chunks[0x4E4F534A])
    binary = chunks[0x004E4942]
    formats = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2),
               5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
    widths = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}

    def accessor(index):
        a = doc['accessors'][index]
        assert 'sparse' not in a, 'Sparse accessors require a separate audit'
        view = doc['bufferViews'][a['bufferView']]
        assert view.get('buffer', 0) == 0, 'External buffers are not audited'
        code, item_size = formats[a['componentType']]
        width = widths[a['type']]
        start = view.get('byteOffset', 0)+a.get('byteOffset', 0)
        stride = view.get('byteStride', item_size*width)
        values = [struct.unpack_from('<'+code*width, binary, start+i*stride) for i in range(a['count'])]
        if a.get('normalized'):
            limits = {5120: 127, 5121: 255, 5122: 32767, 5123: 65535}
            factor = limits[a['componentType']]
            values = [tuple(max(-1, v/factor) for v in row) for row in values]
        return values

    skins = doc.get('skins', [])
    assert skins, 'No skeleton/skin found; this is not a rigged character'
    meshes = []
    for node_index, node in enumerate(doc.get('nodes', [])):
        if 'mesh' not in node:
            continue
        mesh = doc['meshes'][node['mesh']]
        primitives = []
        for primitive in mesh['primitives']:
            assert primitive.get('mode', 4) == 4, 'Non-triangle geometry requires inspection'
            attrs = primitive['attributes']
            assert {'POSITION', 'NORMAL', 'TEXCOORD_0'} <= set(attrs), 'Geometry, normals or UV0 missing'
            positions = accessor(attrs['POSITION'])
            count = doc['accessors'][primitive['indices']]['count'] if 'indices' in primitive else len(positions)
            skinned = 'skin' in node
            details = {'vertices': len(positions), 'triangles': count//3,
                       'material_index': primitive.get('material'), 'skinned': skinned,
                       'local_position_min': [min(v[a] for v in positions) for a in range(3)],
                       'local_position_max': [max(v[a] for v in positions) for a in range(3)]}
            if skinned:
                assert {'JOINTS_0', 'WEIGHTS_0'} <= set(attrs), 'Skin attribute data missing'
                weights = accessor(attrs['WEIGHTS_0'])
                joints = accessor(attrs['JOINTS_0'])
                sums = [sum(w) for w in weights]
                details['zero_weight_vertices'] = sum(total <= 1e-6 for total in sums)
                details['max_weight_sum_error'] = max(abs(total-1) for total in sums)
                details['max_joint_index'] = max(max(j) for j in joints)
                assert not details['zero_weight_vertices'], 'Unweighted vertices found'
                assert details['max_joint_index'] < len(skins[node['skin']]['joints']), 'Joint index out of range'
            primitives.append(details)
        meshes.append({'node_index': node_index, 'name': node.get('name', mesh.get('name')),
                       'skin_index': node.get('skin'), 'primitives': primitives})
    assert any(p['skinned'] for m in meshes for p in m['primitives']), 'No mesh is bound to a skin'
    return {'source': str(path), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
            'triangles': sum(p['triangles'] for m in meshes for p in m['primitives']),
            'mesh_nodes': meshes,
            'skins': [{'name': s.get('name'), 'joint_count': len(s['joints']),
                       'joint_names': [doc['nodes'][j].get('name', f'node_{j}') for j in s['joints']],
                       'inverse_bind_matrices': 'inverseBindMatrices' in s} for s in skins],
            'materials': [m.get('name') for m in doc.get('materials', [])],
            'images': [{'name': image.get('name'), 'embedded': 'bufferView' in image,
                        'mime_type': image.get('mimeType')} for image in doc.get('images', [])],
            'animation_names': [a.get('name') for a in doc.get('animations', [])],
            'verification': 'Structural GLB audit only; silhouette, skin deformation and Unreal retargeting require visual/runtime checks.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.model)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(f'Rigged GLB inspected: {result["triangles"]:,} triangles, {len(result["skins"])} skins')
