"""Import the preserved Meshy GLB as a skeletal character, then audit saved data.

UnrealEditor-Cmd Ronin.uproject -run=pythonscript -script=<this-file>
    -nullrhi -corelimit=2 -EnablePlugins=GeometryScripting -unattended
Add --verify inside the -script argument for a fresh, read-only asset check.
GeometryScripting is enabled for inspection by this command only; game assets
do not depend on it. No gameplay, skeleton sockets, or source GLB are changed.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import import_environment as env

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/generated/samurai-character'
SOURCE_GLB = SOURCE / 'meshy-6-lite-rigged-v1.glb'
SOURCE_HASH = '737a9ab1c49c7bf685aa9f2ce1db6753145859e7e0654fed87296d8803515efd'
BASE = '/Game/Characters/Samurai'
MESH = BASE + '/SK_Samurai'
SKELETON = BASE + '/SK_Samurai_Skeleton'
MATERIAL = BASE + '/Materials/M_Samurai'
CLIPS = {name: BASE + '/A_Samurai_' + name for name in ('Running', 'Walking')}


def properties(obj, **values):
    for key, value in values.items():
        obj.set_editor_property(key, value)


def source_data(write_images):
    raw = SOURCE_GLB.read_bytes()
    env.require(hashlib.sha256(raw).hexdigest() == SOURCE_HASH, 'Source GLB checksum mismatch')
    json_size, json_type = struct.unpack_from('<II', raw, 12)
    env.require(json_type == 0x4e4f534a, 'Expected GLB JSON chunk')
    doc = json.loads(raw[20:20 + json_size])
    bin_start = 20 + json_size + 8
    binary = raw[bin_start:]
    mat = doc['materials'][0]
    pbr = mat['pbrMetallicRoughness']
    env.require(len(doc['materials']) == 1 and 'occlusionTexture' not in mat,
                'Unexpected source material layout; update importer deliberately')
    textures = {}
    for name, index in [('BaseColor', pbr['baseColorTexture']['index']),
                        ('Normal', mat['normalTexture']['index']),
                        ('MetallicRoughness', pbr['metallicRoughnessTexture']['index'])]:
        source_image = doc['images'][doc['textures'][index]['source']]
        env.require(source_image['mimeType'] == 'image/jpeg', 'Expected embedded source JPEG')
        view = doc['bufferViews'][source_image['bufferView']]
        start = view.get('byteOffset', 0)
        image = binary[start:start + view['byteLength']]
        filename = SOURCE / 'textures' / (name + '.jpg')
        if write_images:
            filename.parent.mkdir(parents=True, exist_ok=True)
            if not filename.exists():
                filename.write_bytes(image)
        env.require(filename.exists() and filename.read_bytes() == image,
                    'Extracted image differs from embedded source: ' + name)
        textures[name] = {'source': str(filename.relative_to(ROOT)),
                          'sha256': hashlib.sha256(image).hexdigest()}
    durations = {}
    for animation in doc['animations']:
        times = []
        for index in {s['input'] for s in animation['samplers']}:
            accessor = doc['accessors'][index]
            view = doc['bufferViews'][accessor['bufferView']]
            env.require(accessor['componentType'] == 5126 and accessor['type'] == 'SCALAR',
                        'Unexpected animation time accessor')
            start = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
            stride = view.get('byteStride', 4)
            times.extend(struct.unpack_from('<f', binary, start + i * stride)[0]
                         for i in range(accessor['count']))
        durations[animation['name']] = max(times) - min(times)
    env.require(set(durations) == set(CLIPS), 'Unexpected source animation list')
    return doc, textures, durations


def build_material(textures):
    loaded = {}
    for name, record in textures.items():
        path = BASE + '/Textures/T_Samurai_' + name
        if not env.ASSETS.does_asset_exist(path):
            task = unreal.AssetImportTask()
            properties(task, filename=str(ROOT / record['source']),
                       destination_path=BASE + '/Textures', destination_name=path.rsplit('/', 1)[1],
                       automated=True, replace_existing=False, save=True, factory=unreal.TextureFactory())
            env.TOOLS.import_asset_tasks([task])
        tex = env.require(env.ASSETS.load_asset(path), 'Missing texture ' + name)
        compression = {'BaseColor': unreal.TextureCompressionSettings.TC_DEFAULT,
                       'Normal': unreal.TextureCompressionSettings.TC_NORMALMAP,
                       'MetallicRoughness': unreal.TextureCompressionSettings.TC_MASKS}[name]
        # Matches UE's own InterchangeGltfTranslator normal-map conversion.
        properties(tex, srgb=name == 'BaseColor', compression_settings=compression,
                   flip_green_channel=name == 'Normal', max_texture_size=2048)
        env.save(tex)
        loaded[name] = tex
    mat = env.ASSETS.load_asset(MATERIAL) if env.ASSETS.does_asset_exist(MATERIAL) else env.TOOLS.create_asset(
        'M_Samurai', BASE + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    env.EDIT.delete_all_material_expressions(mat)
    properties(mat, two_sided=True, used_with_skeletal_mesh=True)
    color = env.texture_sample(mat, loaded['BaseColor'], 'BaseColor')
    tint = env.node(mat, unreal.MaterialExpressionVectorParameter, parameter_name='ColorTint',
                    default_value=unreal.LinearColor(1, 1, 1, 1))
    multiply = env.node(mat, unreal.MaterialExpressionMultiply)
    env.connect(color, 'RGB', multiply, 'A')
    env.connect(tint, 'RGB', multiply, 'B')
    env.output(multiply, '', unreal.MaterialProperty.MP_BASE_COLOR)
    normal = env.texture_sample(mat, loaded['Normal'], 'Normal', normal=True)
    env.output(normal, 'RGB', unreal.MaterialProperty.MP_NORMAL)
    mr = env.texture_sample(mat, loaded['MetallicRoughness'], 'MetallicRoughness', masks=True)
    for channel, prop in [('g', unreal.MaterialProperty.MP_ROUGHNESS),
                          ('b', unreal.MaterialProperty.MP_METALLIC)]:
        mask = env.node(mat, unreal.MaterialExpressionComponentMask,
                        r=False, g=channel == 'g', b=channel == 'b', a=False)
        env.connect(mr, 'RGB', mask, '')
        env.output(mask, '', prop)
    return env.finish_material(mat)


def clip_source(name):
    """Keep source buffers intact, selecting one clip for an exact import range."""
    raw = SOURCE_GLB.read_bytes()
    json_size = struct.unpack_from('<I', raw, 12)[0]
    document = json.loads(raw[20:20 + json_size])
    document['animations'] = [clip for clip in document['animations'] if clip['name'] == name]
    env.require(len(document['animations']) == 1, 'Missing source clip ' + name)
    encoded = json.dumps(document, separators=(',', ':')).encode()
    encoded += b' ' * (-len(encoded) % 4)
    binary_chunk = raw[20 + json_size:]
    derived = struct.pack('<III', 0x46546c67, 2, 20 + len(encoded) + len(binary_chunk))
    derived += struct.pack('<II', len(encoded), 0x4e4f534a) + encoded + binary_chunk
    path = ROOT / '.cache/samurai-import' / (name + '.glb')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(derived)
    return path


def import_skeletal(clip=None, duration=None):
    animation_only = clip is not None
    if not animation_only and env.ASSETS.does_asset_exist(MESH):
        return env.require(env.ASSETS.load_asset(MESH), 'Missing existing skeletal mesh')
    asset_name = 'A_Samurai_' + clip if animation_only else 'SK_Samurai'
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    properties(pipeline, asset_name=asset_name, use_source_name_for_asset=False,
               scene_name_sub_folder=False, asset_type_sub_folders=False, import_offset_uniform_scale=1.0)
    properties(pipeline.get_editor_property('common_meshes_properties'),
               bake_meshes=True, bake_pivot_meshes=False, keep_sections_separate=False,
               recompute_normals=False, recompute_tangents=True, use_mikk_t_space=True,
               use_full_precision_u_vs=True, use_high_precision_tangent_basis=True,
               remove_degenerates=False, import_lods=False, auto_detect_mesh_type=False)
    properties(pipeline.get_editor_property('common_skeletal_meshes_and_animations_properties'),
               import_only_animations=animation_only, use_t0_as_ref_pose=False,
               skeleton=env.ASSETS.load_asset(SKELETON) if animation_only else None)
    properties(pipeline.get_editor_property('mesh_pipeline'),
               import_static_meshes=False, import_skeletal_meshes=not animation_only,
               import_morph_targets=False, create_physics_asset=False,
               update_skeleton_reference_pose=False, use_high_precision_skin_weights=True,
               bone_influence_limit=4)
    animation = pipeline.get_editor_property('animation_pipeline')
    properties(animation,
               import_animations=animation_only, import_bone_tracks=True, import_custom_attribute=False,
               custom_bone_animation_sample_rate=24, snap_to_closest_frame_boundary=False)
    if animation_only:
        frames = round(duration * 24)
        env.require(abs(frames / 24 - duration) < .0001, 'Source clip is not aligned to 24 Hz')
        properties(animation, animation_range=unreal.InterchangeAnimationRange.SET_RANGE,
                   frame_import_range=unreal.Int32Interval(min=0, max=frames))
    materials = pipeline.get_editor_property('material_pipeline')
    properties(materials, import_materials=False)
    properties(materials.get_editor_property('texture_pipeline'), import_textures=False)
    params = unreal.ImportAssetParameters()
    properties(params, is_automated=True, replace_existing=animation_only, destination_name=asset_name,
               override_pipelines=[unreal.SoftObjectPath(pipeline.get_path_name())])
    data = unreal.InterchangeManager.create_source_data(str(clip_source(clip) if animation_only else SOURCE_GLB))
    imported = unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(BASE, data, params)
    env.require(imported, 'Interchange skeletal import failed')
    unreal.log('SAMURAI_IMPORTED_OBJECTS ' + json.dumps([o.get_path_name() for o in imported]))
    if animation_only:
        for asset in imported:
            env.require(isinstance(asset, unreal.AnimSequence), 'Animation-only import created unexpected asset')
            env.save(asset)
        return None
    meshes = [o for o in imported if isinstance(o, unreal.SkeletalMesh)]
    env.require(len(meshes) == 1, 'Expected exactly one skeletal mesh')
    if meshes[0].get_path_name().split('.')[0] != MESH:
        env.require(env.ASSETS.rename_loaded_asset(meshes[0], MESH), 'Could not normalize mesh name')
    return meshes[0]


def normalize_names(mesh):
    skeleton = mesh.get_editor_property('skeleton')
    env.require(skeleton, 'Mesh has no skeleton')
    if skeleton.get_path_name().split('.')[0] != SKELETON:
        env.require(env.ASSETS.rename_loaded_asset(skeleton, SKELETON), 'Could not normalize skeleton name')
    for path in env.ASSETS.list_assets(BASE, recursive=True, include_folder=False):
        obj = env.ASSETS.load_asset(path)
        if not isinstance(obj, unreal.AnimSequence):
            continue
        matched = [key for key in CLIPS if key.lower() in obj.get_name().lower()]
        env.require(len(matched) == 1, 'Cannot identify original animation ' + obj.get_name())
        target = CLIPS[matched[0]]
        if obj.get_path_name().split('.')[0] != target:
            env.require(env.ASSETS.rename_loaded_asset(obj, target), 'Could not normalize clip name')
        properties(obj, enable_root_motion=False, force_root_lock=False)
        env.save(obj)
    env.save(skeleton)


def inspect_material(textures):
    mat = env.require(env.ASSETS.load_asset(MATERIAL), 'Missing samurai material')
    env.require(mat.get_editor_property('used_with_skeletal_mesh') and mat.get_editor_property('two_sided'),
                'Missing skeletal/two-sided material flags')
    base = env.EDIT.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)
    env.require(isinstance(base, unreal.MaterialExpressionMultiply), 'Missing ColorTint multiplication')
    tint = env.EDIT.get_material_default_vector_parameter_value(mat, 'ColorTint')
    env.require([tint.r, tint.g, tint.b] == [1, 1, 1], 'ColorTint must default to white')
    env.require(env.EDIT.get_material_property_input_node(mat, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION) is None,
                'Source has no occlusion texture; R must not be treated as AO')
    result = {}
    for name, record in textures.items():
        tex = env.EDIT.get_material_default_texture_parameter_value(mat, name)
        env.require(tex and tex.get_path_name().split('.')[0] == BASE + '/Textures/T_Samurai_' + name,
                    'Incorrect material texture ' + name)
        size = [tex.blueprint_get_size_x(), tex.blueprint_get_size_y()]
        srgb, flip = tex.get_editor_property('srgb'), tex.get_editor_property('flip_green_channel')
        env.require(size == [2048, 2048] and srgb == (name == 'BaseColor') and flip == (name == 'Normal'),
                    'Wrong texture dimensions or color/normal conversion ' + name)
        result[name] = {**record, 'asset': tex.get_path_name(), 'size': size, 'srgb': srgb,
                        'flip_green_channel': flip, 'compression': str(tex.get_editor_property('compression_settings'))}
    for channel, prop in [('g', unreal.MaterialProperty.MP_ROUGHNESS), ('b', unreal.MaterialProperty.MP_METALLIC)]:
        expression = env.EDIT.get_material_property_input_node(mat, prop)
        env.require(isinstance(expression, unreal.MaterialExpressionComponentMask), 'Missing packed-map mask')
        env.require([expression.get_editor_property(c) for c in 'rgba'] == [c == channel for c in 'rgba'],
                    'Incorrect packed-map channel ' + channel)
    return {'path': mat.get_path_name(), 'ColorTint': [1, 1, 1, 1], 'textures': result,
            'roughness_channel': 'G', 'metallic_channel': 'B', 'occlusion_input': None,
            'two_sided': True, 'used_with_skeletal_mesh': True}


def inspect_geometry(mesh):
    env.require(hasattr(unreal, 'GeometryScript_AssetUtils'),
                'Run with -EnablePlugins=GeometryScripting for actual geometry/weight inspection')
    dynamic = unreal.DynamicMesh()
    result = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
        mesh, dynamic, unreal.GeometryScriptCopyMeshFromAssetOptions(),
        unreal.GeometryScriptMeshReadLOD(lod_type=unreal.GeometryScriptLODType.RENDER_DATA, lod_index=0))
    env.require(result[-1] == unreal.GeometryScriptOutcomePins.SUCCESS, 'Could not read render geometry')
    queries = unreal.GeometryScript_MeshQueries
    triangles = dynamic.get_triangle_count()
    count = queries.get_vertex_count(dynamic)
    env.require(triangles == 288176, 'Imported triangle count changed')
    env.require(queries.get_num_uv_sets(dynamic) == 1, 'Expected source UV0')
    env.require(queries.get_has_triangle_normals(dynamic), 'Imported mesh lacks normals')
    bounds = queries.get_mesh_bounding_box(dynamic)
    env.require(abs(bounds.max.z - 170) < .05 and abs(bounds.min.z) < .05, 'Incorrect source scale/basis')
    env.require(not queries.get_has_vertex_id_gaps(dynamic), 'Unexpected sparse render vertex IDs')
    # NullRHI never initializes the RHI skin-weight resource. UE's render-data
    # adapter consequently omits that profile even when saved weights exist.
    # Read weights from the saved skeletal MeshDescription instead; geometry,
    # UVs and normals above/below still come from LOD0 render data.
    weighted = unreal.DynamicMesh()
    copied, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
        mesh, weighted, unreal.GeometryScriptCopyMeshFromAssetOptions(),
        unreal.GeometryScriptMeshReadLOD(lod_type=unreal.GeometryScriptLODType.SOURCE_MODEL, lod_index=0))
    env.require(outcome == unreal.GeometryScriptOutcomePins.SUCCESS, 'Could not read skeletal MeshDescription')
    weighted_count = queries.get_vertex_count(weighted)
    env.require(not queries.get_has_vertex_id_gaps(weighted), 'Unexpected sparse source vertex IDs')
    max_error, max_influences, max_index = 0, 0, -1
    bone_indices = set()
    profile = unreal.GeometryScriptBoneWeightProfile(profile_name='Default')
    for vertex in range(weighted_count):
        _, weights, valid = unreal.GeometryScript_BoneWeights.get_vertex_bone_weights(weighted, vertex, profile)
        env.require(valid and len(weights) > 0, 'Unweighted saved source vertex ' + str(vertex))
        # UE 5.7's query pre-sizes its output then appends real weights, leaving
        # zero-initialized entries in front; exclude those empty entries.
        values = [(w.bone_index, w.weight) for w in weights if w.weight != 0]
        env.require(values, 'Zero-sum saved source vertex ' + str(vertex))
        env.require(all(0 <= index < 32 and math.isfinite(weight) and weight >= 0 for index, weight in values),
                    'Invalid saved skin weight at ' + str(vertex))
        max_error = max(max_error, abs(sum(weight for _, weight in values) - 1))
        max_influences = max(max_influences, len(values))
        max_index = max(max_index, max(index for index, _ in values))
        bone_indices.update(index for index, weight in values if weight > 0)
    env.require(max_error < .0001 and max_influences <= 4, 'Unexpected skin-weight normalization/influence count')
    # Sample across every region, checking saved UV and normal attributes on the
    # actual render triangles; full weight coverage above is checked separately.
    sampled = 0
    for triangle in range(0, triangles, max(1, triangles // 1024)):
        uv1, uv2, uv3, valid_uv = queries.get_triangle_u_vs(dynamic, 0, triangle)
        _, n1, n2, n3, valid_normal = queries.get_triangle_normals(dynamic, triangle)
        env.require(valid_uv and valid_normal, 'Missing triangle UVs/normals')
        env.require(all(math.isfinite(v) for uv in (uv1, uv2, uv3) for v in (uv.x, uv.y)), 'Non-finite UV')
        env.require(all(abs(n.length() - 1) < .002 for n in (n1, n2, n3)), 'Invalid render normal')
        sampled += 1
    return {'triangles': triangles, 'render_vertices': count, 'uv_channels': 1,
            'bounds_min_cm': env.vector_list(bounds.min), 'bounds_max_cm': env.vector_list(bounds.max),
            'weight_check_source': 'Saved LOD0 skeletal MeshDescription; NullRHI render resource is not initialized',
            'saved_source_vertices': weighted_count, 'all_saved_source_vertices_weighted': True,
            'max_weight_sum_error': max_error,
            'max_influences': max_influences, 'max_joint_index': max_index,
            'weighted_bone_indices': sorted(bone_indices), 'uv_normal_triangles_sampled': sampled}


def inspect_mesh(mesh, durations):
    env.require(isinstance(mesh, unreal.SkeletalMesh), 'Expected real SkeletalMesh')
    skeleton = mesh.get_editor_property('skeleton')
    env.require(skeleton.get_path_name().split('.')[0] == SKELETON, 'Incorrect skeleton asset')
    component = unreal.SkeletalMeshComponent()
    component.set_skeletal_mesh_asset(mesh)
    bones = [{'index': i, 'name': str(component.get_bone_name(i)),
              'parent': str(component.get_parent_bone(component.get_bone_name(i))),
              'local_bind_translation_cm': env.vector_list(component.get_ref_pose_transform(i).translation)}
             for i in range(component.get_num_bones())]
    unreal.log('SAMURAI_IMPORTED_BONES ' + json.dumps(bones))
    env.require(32 <= len(bones) <= 33, 'Unexpected skeleton size')
    slots = mesh.get_editor_property('materials')
    env.require(len(slots) == 1 and slots[0].get_editor_property('material_interface').get_path_name().split('.')[0] == MATERIAL,
                'Incorrect skeletal material assignment')
    editor = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem) or unreal.SkeletalMeshEditorSubsystem()
    build = editor.get_lod_build_settings(mesh, 0)
    settings = {key: build.get_editor_property(key) for key in (
        'recompute_normals', 'recompute_tangents', 'use_mikk_t_space',
        'use_full_precision_u_vs', 'use_high_precision_tangent_basis', 'use_high_precision_skin_weights')}
    env.require(not settings['recompute_normals'] and all(v for k, v in settings.items() if k != 'recompute_normals'),
                'Source normals/UV/tangent/weight build settings changed')
    clips = {}
    for name, path in CLIPS.items():
        clip = env.require(env.ASSETS.load_asset(path), 'Missing original animation ' + name)
        env.require(isinstance(clip, unreal.AnimSequence) and clip.get_editor_property('skeleton') == skeleton,
                    'Animation does not use imported skeleton')
        duration = unreal.AnimationLibrary.get_sequence_length(clip)
        env.require(abs(duration - durations[name]) < .0001,
                    f'Animation duration changed {name}: {duration} != {durations[name]}')
        tracks = [str(n) for n in unreal.AnimationLibrary.get_animation_track_names(clip)]
        frames = unreal.AnimationLibrary.get_num_frames(clip)
        env.require(frames == round(durations[name] * 24), 'Source 24 Hz frame count changed ' + name)
        env.require(len(tracks) >= 32, 'Animation bone tracks lost ' + name)
        env.require(not clip.get_editor_property('enable_root_motion') and not clip.get_editor_property('force_root_lock'),
                    'Source root animation must remain unmodified')
        clips[name] = {'path': clip.get_path_name(), 'duration_seconds': duration,
                       'source_duration_seconds': durations[name],
                       'frames': frames, 'sample_rate_hz': 24, 'track_names': tracks,
                       'root_motion_enabled': False, 'force_root_lock': False}
    return {'path': mesh.get_path_name(), 'skeleton': skeleton.get_path_name(), 'bones': bones,
            'material_slots': [str(slot.get_editor_property('material_slot_name')) for slot in slots],
            'build_settings': settings, 'geometry': inspect_geometry(mesh), 'animations': clips,
            'coordinate_conversion': 'glTF (x,y,z) meters to UE (x,z,y) centimeters',
            'source_forward': '+Y in Unreal, confirmed by source headfront/toe offsets; actor +X needs yaw -90 degrees',
            'root_behavior': 'Imported source pelvis animation is retained; no root-motion extraction or force-root-lock.'}


def content_hashes():
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted((ROOT / 'Content/Characters/Samurai').rglob('*.uasset'))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    before = content_hashes() if args.verify else None
    unreal.load_module('AnimationBlueprintLibrary')
    env.require(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve() == ROOT,
                'Wrong Unreal project')
    doc, textures, durations = source_data(not args.verify)
    if args.verify:
        mesh = env.ASSETS.load_asset(MESH)
    else:
        unreal.load_module('BinkAudioDecoder')
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.Enable 1')
        env.ASSETS.make_directory(BASE)
        material = build_material(textures)
        mesh = import_skeletal()
        # This source is 24 Hz. The GLTF translator otherwise rounds its end
        # times to 30 Hz before the animation pipeline sees the tracks.
        # Import each unchanged source clip with its exact frame range.
        for name, duration in durations.items():
            existing = env.ASSETS.load_asset(CLIPS[name]) if env.ASSETS.does_asset_exist(CLIPS[name]) else None
            if (existing is None or abs(unreal.AnimationLibrary.get_sequence_length(existing) - duration) > .0001
                    or unreal.AnimationLibrary.get_num_frames(existing) != round(duration * 24)):
                import_skeletal(clip=name, duration=duration)
        normalize_names(mesh)
        slots = mesh.get_editor_property('materials')
        env.require(len(slots) == 1, 'Expected one source material slot')
        slot = slots[0]
        slot.set_editor_property('material_interface', material)
        slots[0] = slot
        mesh.set_editor_property('materials', slots)
        # SkeletalMesh.Materials is a custom-serialized transient property;
        # setting it through Python does not reliably dirty an existing package.
        env.require(env.ASSETS.save_loaded_asset(mesh, only_if_is_dirty=False),
                    'Could not persist skeletal material assignment')
    result = inspect_mesh(mesh, durations)
    material_report = inspect_material(textures)
    after = content_hashes()
    if args.verify:
        env.require(before == after, 'Read-only verification changed asset files')
    report = {'engine': unreal.SystemLibrary.get_engine_version(), 'read_only_verification': args.verify,
              'source': str(SOURCE_GLB.relative_to(ROOT)), 'source_sha256': SOURCE_HASH,
              'mesh': result, 'material': material_report, 'content_sha256': after,
              'limits': ['GPU appearance and skinned motion not yet reviewed.',
                         'No combat clips, animation blueprint, sockets, gameplay integration, or LOD reduction added.',
                         'Saved assets can be referenced for cooking; final packaged cook remains a separate project check.']}
    (SOURCE / 'unreal-import-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    unreal.log('SAMURAI_IMPORT_VERIFIED ' + json.dumps(report))


if __name__ == '__main__':
    main()
