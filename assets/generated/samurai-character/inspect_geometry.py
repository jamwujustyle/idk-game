"""Audit the received, uncompressed static samurai GLBs without altering them.

Run from this directory: python3 inspect_geometry.py
This does not certify visual quality, deformation, or gameplay readiness.
"""
from collections import Counter
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'meshy-6-lite-geometry-v1.glb'


def inspect(source=SOURCE):
    raw = source.read_bytes()
    assert struct.unpack_from('<III', raw) == (0x46546C67, 2, len(raw)), 'Invalid GLB header'
    chunks = {}
    offset = 12
    while offset < len(raw):
        length, kind = struct.unpack_from('<II', raw, offset)
        assert offset + 8 + length <= len(raw), 'Truncated chunk'
        assert kind not in chunks, 'Duplicate chunk'
        chunks[kind] = raw[offset + 8:offset + 8 + length]
        offset += 8 + length
    doc = json.loads(chunks[0x4E4F534A])
    binary = chunks[0x004E4942]
    assert not doc.get('extensionsRequired'), 'Required extensions need a separate audit'

    def values(index, kind, component, fmt):
        a = doc['accessors'][index]
        assert a['type'] == kind and a['componentType'] == component
        assert 'sparse' not in a and not a.get('normalized'), 'Unsupported accessor'
        view = doc['bufferViews'][a['bufferView']]
        assert view.get('buffer', 0) == 0
        base = view.get('byteOffset', 0)
        start = base + a.get('byteOffset', 0)
        size = struct.calcsize(fmt)
        stride = view.get('byteStride', size)
        assert stride >= size and a['count'] > 0
        assert start + (a['count'] - 1) * stride + size <= base + view['byteLength'] <= len(binary)
        return [struct.unpack_from(fmt, binary, start + i * stride) for i in range(a['count'])]

    assert len(doc['meshes']) == 1 and len(doc['meshes'][0]['primitives']) == 1
    primitive = doc['meshes'][0]['primitives'][0]
    assert primitive.get('mode', 4) == 4
    points = values(primitive['attributes']['POSITION'], 'VEC3', 5126, '<fff')
    indices = [x[0] for x in values(primitive['indices'], 'SCALAR', 5125, '<I')]
    assert len(indices) % 3 == 0 and all(0 <= i < len(points) for i in indices)
    assert all(math.isfinite(v) for p in points for v in p)
    attributes = primitive['attributes']
    surface = {}
    if 'NORMAL' in attributes:
        normals = values(attributes['NORMAL'], 'VEC3', 5126, '<fff')
        assert len(normals) == len(points) and all(math.isfinite(v) for n in normals for v in n)
        surface['max_normal_length_error'] = max(abs(math.sqrt(sum(v*v for v in n))-1) for n in normals)
    if 'TEXCOORD_0' in attributes:
        uv = values(attributes['TEXCOORD_0'], 'VEC2', 5126, '<ff')
        assert len(uv) == len(points) and all(math.isfinite(v) for pair in uv for v in pair)
        surface['uv0_bounds'] = [[min(pair[j] for pair in uv) for j in range(2)],
                                 [max(pair[j] for pair in uv) for j in range(2)]]
    embedded_images = []
    for image in doc.get('images', []):
        assert 'bufferView' in image and 'uri' not in image, 'External image requires separate inspection'
        view = doc['bufferViews'][image['bufferView']]
        start = view.get('byteOffset', 0)
        assert view.get('buffer', 0) == 0 and start + view['byteLength'] <= len(binary)
        payload = binary[start:start+view['byteLength']]
        embedded_images.append({'mime_type':image['mimeType'], 'bytes':len(payload),
                                'sha256':hashlib.sha256(payload).hexdigest()})
    edges = Counter()
    degenerate = 0
    for i in range(0, len(indices), 3):
        a, b, c = indices[i:i + 3]
        u = [points[b][j] - points[a][j] for j in range(3)]
        v = [points[c][j] - points[a][j] for j in range(3)]
        cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
        degenerate += sum(x*x for x in cross) <= 1e-24
        edges.update(tuple(sorted(pair)) for pair in ((a,b), (b,c), (c,a)))
    lo = [min(p[j] for p in points) for j in range(3)]
    hi = [max(p[j] for p in points) for j in range(3)]
    return {
        'source': source.name, 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
        'generator': doc.get('asset', {}).get('generator'),
        'vertices': len(points), 'triangles': len(indices)//3,
        'position_bounds_gltf': {'min': lo, 'max': hi, 'extent': [hi[j]-lo[j] for j in range(3)]},
        'finite_positions': True, 'indices_in_range': True,
        'near_zero_area_triangles': degenerate, 'squared_cross_threshold': 1e-24,
        'boundary_edges': sum(n == 1 for n in edges.values()),
        'edges_with_more_than_two_faces': sum(n > 2 for n in edges.values()),
        'attributes': sorted(primitive['attributes']),
        'materials': len(doc.get('materials', [])), 'images': len(doc.get('images', [])),
        'textures': len(doc.get('textures', [])), 'skins': len(doc.get('skins', [])),
        'animations': len(doc.get('animations', [])),
        'surface': surface, 'embedded_images': embedded_images,
        'limitations': 'Edge counts use exported vertex indices; UV/normal seams can appear as boundaries. Counts do not detect self-intersections or certify manifold vertices. No visual, deformation, rig or engine validation.',
        'ready_for_character_integration': False,
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, nargs='?', default=SOURCE)
    parser.add_argument('--output', type=Path, default=ROOT / 'geometry-audit.json')
    args = parser.parse_args()
    report = inspect(args.source)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
