"""Verify original GLB, extract portable maps, and split attachment-ready GLBs.

Pure stdlib. No geometry or image pixels are generated or changed by this step.
Unused binary data is retained verbatim, keeping indices and original precision.
"""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def read_glb(path):
    data = path.read_bytes()
    magic, version, size = struct.unpack_from('<4sII', data)
    assert magic == b'glTF' and version == 2 and size == len(data)
    cursor = 12
    chunks = {}
    while cursor < len(data):
        length, kind = struct.unpack_from('<I4s', data, cursor)
        cursor += 8
        chunks[kind] = data[cursor:cursor+length]
        cursor += length
    return json.loads(chunks[b'JSON']), chunks[b'BIN\0']


def accessor(doc, binary, index):
    a = doc['accessors'][index]
    assert 'sparse' not in a
    view = doc['bufferViews'][a['bufferView']]
    char = {5120:'b',5121:'B',5122:'h',5123:'H',5125:'I',5126:'f'}[a['componentType']]
    width = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
    fmt = '<'+char*width
    stride = view.get('byteStride', struct.calcsize(fmt))
    start = view.get('byteOffset',0)+a.get('byteOffset',0)
    return [struct.unpack_from(fmt,binary,start+i*stride) for i in range(a['count'])]


def save_glb(doc, binary, path):
    payload = json.dumps(doc,separators=(',',':')).encode()
    payload += b' '*(-len(payload)%4)
    binary += b'\0'*(-len(binary)%4)
    path.write_bytes(struct.pack('<4sII',b'glTF',2,28+len(payload)+len(binary))+
                     struct.pack('<I4s',len(payload),b'JSON')+payload+
                     struct.pack('<I4s',len(binary),b'BIN\0')+binary)


def split(doc, binary, name, output):
    base = next(i for i,n in enumerate(doc['nodes']) if n.get('name') == name)
    keep = []
    def visit(i):
        keep.append(i)
        for c in doc['nodes'][i].get('children',[]): visit(c)
    visit(base)
    nodes = {old:new for new,old in enumerate(keep)}
    mesh_ids = sorted({doc['nodes'][i]['mesh'] for i in keep if 'mesh' in doc['nodes'][i]})
    mesh_map = {old:new for new,old in enumerate(mesh_ids)}
    result = deepcopy(doc)
    result['nodes'] = [deepcopy(doc['nodes'][i]) for i in keep]
    result['meshes'] = [deepcopy(doc['meshes'][i]) for i in mesh_ids]
    result['scenes'] = [{'name':name,'nodes':[0]}]
    result['scene'] = 0
    result.pop('cameras',None)
    result.pop('animations',None)
    for n in result['nodes']:
        if 'children' in n: n['children'] = [nodes[i] for i in n['children']]
        if 'mesh' in n: n['mesh'] = mesh_map[n['mesh']]
    # The full review file offsets the saya for display. Its export origin is mouth center.
    result['nodes'][0].pop('translation',None)
    save_glb(result,binary,output)


def main():
    doc, binary = read_glb(ROOT/'KatanaAndSaya.glb')
    records = []
    for mesh in doc['meshes']:
        triangles = 0
        slots = []
        for p in mesh['primitives']:
            assert p.get('mode',4) == 4
            attrs = p['attributes']
            assert {'POSITION','NORMAL','TEXCOORD_0'} <= set(attrs)
            positions = accessor(doc,binary,attrs['POSITION'])
            normals = accessor(doc,binary,attrs['NORMAL'])
            uv = accessor(doc,binary,attrs['TEXCOORD_0'])
            indices = [x[0] for x in accessor(doc,binary,p['indices'])]
            assert len(indices)%3 == 0 and max(indices)<len(positions)
            assert len(normals)==len(positions)==len(uv)
            assert all(abs(sum(c*c for c in n)-1)<.002 for n in normals)
            triangles += len(indices)//3
            slots.append(doc['materials'][p['material']]['name'])
        records.append({'name':mesh['name'],'triangles':triangles,'slots':slots})
    textures = ROOT/'textures'
    textures.mkdir(exist_ok=True)
    materials = {}
    for mat in doc['materials']:
        pbr = mat['pbrMetallicRoughness']
        texture = doc['textures'][pbr['baseColorTexture']['index']]
        image = doc['images'][texture['source']]
        assert image['mimeType'] == 'image/png'
        view = doc['bufferViews'][image['bufferView']]
        data = binary[view.get('byteOffset',0):view.get('byteOffset',0)+view['byteLength']]
        assert data.startswith(b'\x89PNG\r\n\x1a\n')
        path = textures/('T_'+mat['name']+'.png')
        path.write_bytes(data)
        materials[mat['name']] = {'texture':str(path.relative_to(ROOT)),
            'base_color_factor':pbr.get('baseColorFactor',[1,1,1,1]),
            'metallic':pbr.get('metallicFactor',1),'roughness':pbr.get('roughnessFactor',1),
            'double_sided':mat.get('doubleSided',False)}
    split(doc,binary,'KATANA_Root',ROOT/'SM_Katana.glb')
    split(doc,binary,'SAYA_Root',ROOT/'SM_Saya.glb')
    report = {'meshes':records,'total_triangles':sum(x['triangles'] for x in records),
              'materials':materials,'embedded_images':len(doc['images']),
              'validation':'All indexed triangles, unit normals, and UV0 exported; exact semantic part names retained.',
              'exports':{p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
                         for p in [ROOT/'KatanaAndSaya.glb',ROOT/'KatanaAndSaya.blend',ROOT/'SM_Katana.glb',ROOT/'SM_Saya.glb']}}
    (ROOT/'export-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'triangles':report['total_triangles'],'meshes':len(records),'materials':list(materials),'exports':report['exports']},indent=2))


if __name__ == '__main__':
    main()
