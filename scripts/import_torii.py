"""Import the authored GLB torii as one mesh with three CC0 material sections.

Run in UnrealEditor-Cmd with -run=pythonscript -nullrhi -corelimit=2.
Use --verify to inspect existing assets without recreating materials or importing.
This script owns only the torii mesh and its two new material assets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import import_environment as env

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'assets/generated/torii/manifest.json'
DESTINATION = '/Game/Environment/Torii'
MESH_PATH = DESTINATION + '/SM_ToriiGate'
CEDAR_PATH = '/Game/Environment/Materials/M_ToriiCedar'
CAP_PATH = '/Game/Environment/Materials/MI_ToriiCedarCap'
STONE_PATH = '/Game/Environment/Materials/M_StoneEdge'
SLOTS = {'Cedar_Weathered': CEDAR_PATH,
         'Cap_BlackenedCedar': CAP_PATH,
         'Footing_Stone': STONE_PATH}


def create_materials():
    textures = {role: env.require(env.ASSETS.load_asset(path), f'Missing CC0 texture: {path}')
                for role, path in {
                    'Diffuse': '/Game/Environment/Textures/T_Cedar_Diffuse',
                    'Normal': '/Game/Environment/Textures/T_Cedar_nor_dx',
                    'ARM': '/Game/Environment/Textures/T_Cedar_arm'}.items()}
    # Default texture sampling uses UV0. The torii has 1.1 m per UV unit and
    # longitudinal V coordinates, including across curved beams.
    material = env.new_material('M_ToriiCedar')
    diffuse = env.texture_sample(material, textures['Diffuse'], 'Diffuse')
    normal = env.texture_sample(material, textures['Normal'], 'Normal', normal=True)
    arm = env.texture_sample(material, textures['ARM'], 'ARM', masks=True)
    desaturate = env.node(material, unreal.MaterialExpressionDesaturation)
    fraction = env.node(material, unreal.MaterialExpressionScalarParameter,
                        parameter_name='Weathering', default_value=.35)
    env.connect(diffuse, 'RGB', desaturate, '')
    env.connect(fraction, '', desaturate, 'Fraction')
    tint = env.node(material, unreal.MaterialExpressionVectorParameter,
                   parameter_name='ColorTint', default_value=unreal.LinearColor(.72, .72, .72, 1))
    multiply = env.node(material, unreal.MaterialExpressionMultiply)
    env.connect(desaturate, '', multiply, 'A')
    env.connect(tint, 'RGB', multiply, 'B')
    env.output(multiply, '', unreal.MaterialProperty.MP_BASE_COLOR)
    env.output(normal, 'RGB', unreal.MaterialProperty.MP_NORMAL)
    for channel, prop in [('r', unreal.MaterialProperty.MP_AMBIENT_OCCLUSION),
                          ('g', unreal.MaterialProperty.MP_ROUGHNESS),
                          ('b', unreal.MaterialProperty.MP_METALLIC)]:
        mask = env.node(material, unreal.MaterialExpressionComponentMask, **{channel: True})
        env.connect(arm, 'RGB', mask, '')
        env.output(mask, '', prop)
    env.finish_material(material)
    cap = env.ASSETS.load_asset(CAP_PATH) if env.ASSETS.does_asset_exist(CAP_PATH) else env.TOOLS.create_asset(
        'MI_ToriiCedarCap', '/Game/Environment/Materials', unreal.MaterialInstanceConstant,
        unreal.MaterialInstanceConstantFactoryNew())
    env.require(isinstance(cap, unreal.MaterialInstanceConstant), 'Could not create torii cap material')
    env.EDIT.set_material_instance_parent(cap, material)
    # UE 5.7's setter returns false even after assigning the parameter. Verify
    # the stored value instead (MaterialEditingLibrary.cpp).
    env.EDIT.set_material_instance_vector_parameter_value(
        cap, 'ColorTint', unreal.LinearColor(.07, .08, .09, 1))
    env.EDIT.update_material_instance(cap)
    actual = env.EDIT.get_material_instance_vector_parameter_value(cap, 'ColorTint')
    env.require(abs(actual.r-.07) < .001 and abs(actual.g-.08) < .001 and abs(actual.b-.09) < .001,
                'Could not verify torii cap tint')
    env.save(cap)
    return {name: env.require(env.ASSETS.load_asset(path), f'Missing material: {path}')
            for name, path in SLOTS.items()}


def import_mesh(source):
    if env.ASSETS.does_asset_exist(MESH_PATH):
        return env.ASSETS.load_asset(MESH_PATH)
    # Interchange supports GLB. FbxImportUI is deliberately not used for this file.
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property('asset_name', 'SM_ToriiGate')
    pipeline.set_editor_property('use_source_name_for_asset', False)
    pipeline.set_editor_property('scene_name_sub_folder', False)
    pipeline.set_editor_property('asset_type_sub_folders', False)
    pipeline.set_editor_property('import_offset_uniform_scale', 1.0)
    common = pipeline.get_editor_property('common_meshes_properties')
    for key, value in {'bake_meshes': True, 'bake_pivot_meshes': False,
                       'keep_sections_separate': False, 'recompute_normals': False,
                       'recompute_tangents': True, 'use_mikk_t_space': True,
                       'use_full_precision_u_vs': True, 'remove_degenerates': False,
                       'import_lods': False, 'auto_detect_mesh_type': False}.items():
        common.set_editor_property(key, value)
    meshes = pipeline.get_editor_property('mesh_pipeline')
    for key, value in {'import_static_meshes': True, 'combine_static_meshes': True,
                       'import_skeletal_meshes': False, 'collision': False,
                       'build_nanite': False, 'generate_lightmap_u_vs': False}.items():
        meshes.set_editor_property(key, value)
    pipeline.get_editor_property('animation_pipeline').set_editor_property('import_animations', False)
    materials = pipeline.get_editor_property('material_pipeline')
    materials.set_editor_property('import_materials', False)
    materials.get_editor_property('texture_pipeline').set_editor_property('import_textures', False)
    parameters = unreal.ImportAssetParameters()
    parameters.set_editor_property('is_automated', True)
    parameters.set_editor_property('replace_existing', False)
    parameters.set_editor_property('destination_name', 'SM_ToriiGate')
    parameters.set_editor_property('override_pipelines', [unreal.SoftObjectPath(pipeline.get_path_name())])
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    source_data = unreal.InterchangeManager.create_source_data(str(source))
    env.require(manager.import_asset(DESTINATION, source_data, parameters), 'GLB import failed')
    mesh = env.ASSETS.load_asset(MESH_PATH)
    return env.require(mesh, f'Import did not create the requested mesh: {MESH_PATH}')


def inspect(mesh, materials=None):
    env.require(isinstance(mesh, unreal.StaticMesh), 'Torii asset is not a StaticMesh')
    slots = mesh.get_editor_property('static_materials')
    names = []
    for index, slot in enumerate(slots):
        imported = str(slot.get_editor_property('imported_material_slot_name'))
        name = str(slot.get_editor_property('material_slot_name'))
        token = imported if imported in SLOTS else name
        env.require(token in SLOTS, f'Unexpected torii slot: {name} / {imported}')
        names.append(token)
        if materials:
            mesh.set_material(index, materials[token])
        assigned = mesh.get_material(index)
        env.require(assigned and assigned.get_path_name().split('.')[0] == SLOTS[token],
                    f'Incorrect material assigned to {token}')
    env.require(set(names) == set(SLOTS) and len(names) == 3, 'Expected three semantic material slots')
    bounds = mesh.get_bounding_box()
    extent = bounds.max-bounds.min
    # GLB metres are converted to centimetres by Unreal's GLTF translator.
    # Blender (X,Y,Z) -> glTF (X,Z,-Y) -> Unreal (X,-Y,Z).
    for actual, expected in zip([extent.x, extent.y, extent.z], [1090, 110.1184, 657]):
        env.require(abs(actual-expected) < 1, f'Unexpected imported dimensions: {extent}')
    env.require(abs(bounds.min.z) < .1, f'Base pivot moved above/below zero: {bounds.min.z}')
    env.require(mesh.get_num_triangles(0) == 37064, 'Import changed LOD0 triangle count')
    env.require(mesh.get_num_sections(0) == 3, 'Expected three merged material sections')
    editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.StaticMeshEditorSubsystem()
    uv_channels = editor.get_num_uv_channels(mesh, 0)
    env.require(uv_channels >= 1, 'UV0 was not imported')
    if materials:
        env.save(mesh)
    return {'object_path': mesh.get_path_name(), 'bounds_min_cm': env.vector_list(bounds.min),
            'bounds_max_cm': env.vector_list(bounds.max), 'dimensions_cm': env.vector_list(extent),
            'triangles_lod0': mesh.get_num_triangles(0), 'sections_lod0': mesh.get_num_sections(0),
            'uv_channels_lod0': uv_channels, 'material_slots': names, 'materials': SLOTS,
            'source_front_unreal': '+Y', 'arena_yaw_degrees': 90,
            'arena_placement': 'Unit scale; base pivot at the desired paving height; yaw 90 faces -X'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    env.require(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve() == ROOT,
                'Wrong Unreal project')
    manifest = json.loads(MANIFEST.read_text())
    record = manifest['exports']['glb']
    source = ROOT / record['path']
    env.require(source.is_file(), 'Missing torii GLB')
    env.require(hashlib.sha256(source.read_bytes()).hexdigest() == record['sha256'], 'Torii source checksum mismatch')
    if args.verify:
        result = inspect(env.ASSETS.load_asset(MESH_PATH))
    else:
        unreal.load_module('BinkAudioDecoder')
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.Enable 1')
        env.ASSETS.make_directory(DESTINATION)
        materials = create_materials()
        result = inspect(import_mesh(source), materials)
        manifest['unreal_import'] = {'engine': unreal.SystemLibrary.get_engine_version(), **result,
                                    'verification': 'Static asset metadata verified; in-game GPU appearance is separate.'}
        manifest['verification']['unreal'] = 'NullRHI import metadata verified; in-game GPU appearance is separate.'
        MANIFEST.write_text(json.dumps(manifest, indent=2)+'\n')
    unreal.log('RONIN_TORII_VERIFIED '+json.dumps(result))


if __name__ == '__main__':
    main()
