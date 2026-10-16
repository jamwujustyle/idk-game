"""Import original attachment-ready katana and saya; no actor/source edits.

Run through UnrealEditor-Cmd -run=pythonscript -nullrhi -corelimit=2.
--verify only reads existing asset metadata. Run source/prepare_exports.py first.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import unreal

sys.path.insert(0,str(Path(__file__).resolve().parent))
import import_environment as env

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'assets/generated/katana'
DESTINATION = '/Game/Weapons/Katana'


def content_hashes():
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted((ROOT/'Content/Weapons/Katana').rglob('*.uasset'))}


def inspect_materials(records):
    result = {}
    for name,record in records.items():
        mat = env.require(env.ASSETS.load_asset(DESTINATION+'/Materials/M_'+name),
                          'Missing material '+name)
        texture = env.EDIT.get_material_default_texture_parameter_value(mat,'BaseColor')
        expected = DESTINATION+'/Textures/T_'+name
        env.require(isinstance(texture,unreal.Texture2D) and
                    texture.get_path_name().split('.')[0]==expected,'Incorrect texture '+name)
        size = [texture.blueprint_get_size_x(),texture.blueprint_get_size_y()]
        env.require(size==[512,512] and texture.get_editor_property('srgb'),
                    'Wrong color texture dimensions/space '+name)
        values = {}
        for label,prop in [('metallic',unreal.MaterialProperty.MP_METALLIC),
                           ('roughness',unreal.MaterialProperty.MP_ROUGHNESS)]:
            expression = env.EDIT.get_material_property_input_node(mat,prop)
            env.require(isinstance(expression,unreal.MaterialExpressionScalarParameter),
                        'Expected direct scalar '+label+' on '+name)
            env.require(str(expression.get_editor_property('parameter_name'))==label.title(),
                        'Wrong scalar input '+label+' on '+name)
            value = env.EDIT.get_material_default_scalar_parameter_value(mat,label.title())
            env.require(abs(value-record[label])<1e-6,'Incorrect '+label+' on '+name)
            values[label] = value
        base = env.EDIT.get_material_property_input_node(mat,unreal.MaterialProperty.MP_BASE_COLOR)
        env.require(isinstance(base,unreal.MaterialExpressionMultiply),'Expected textured base color '+name)
        tint = env.EDIT.get_material_default_vector_parameter_value(mat,'Tint')
        actual_tint = [tint.r,tint.g,tint.b,tint.a]
        env.require(all(abs(a-b)<1e-6 for a,b in zip(actual_tint,record['base_color_factor'])),
                    'Incorrect base color factor '+name)
        result[name] = {'material':mat.get_path_name(),'texture':texture.get_path_name(),
                        'texture_size':size,'srgb':True,'base_color_factor':actual_tint,**values}
    return result


def materials(records):
    result = {}
    for name, record in records.items():
        texture_name = 'T_'+name
        texture_path = DESTINATION+'/Textures/'+texture_name
        if not env.ASSETS.does_asset_exist(texture_path):
            task = unreal.AssetImportTask()
            for key,value in {'filename':str(SOURCE/record['texture']),
                'destination_path':DESTINATION+'/Textures','destination_name':texture_name,
                'automated':True,'replace_existing':False,'save':True,
                'factory':unreal.TextureFactory()}.items():
                task.set_editor_property(key,value)
            env.TOOLS.import_asset_tasks([task])
        tex = env.require(env.ASSETS.load_asset(texture_path), 'Missing authored texture '+name)
        env.require(isinstance(tex,unreal.Texture2D),'Expected Texture2D '+name)
        tex.set_editor_property('srgb',True)
        tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_DEFAULT)
        env.save(tex)
        mat_name = 'M_'+name
        path = DESTINATION+'/Materials/'+mat_name
        mat = env.ASSETS.load_asset(path) if env.ASSETS.does_asset_exist(path) else env.TOOLS.create_asset(
            mat_name,DESTINATION+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
        env.require(isinstance(mat,unreal.Material),'Expected Material '+name)
        env.EDIT.delete_all_material_expressions(mat)
        mat.set_editor_property('two_sided',False)
        sample = env.texture_sample(mat,tex,'BaseColor')
        factor = record['base_color_factor']
        tint = env.node(mat,unreal.MaterialExpressionVectorParameter,parameter_name='Tint',
                        default_value=unreal.LinearColor(*factor))
        multiply = env.node(mat,unreal.MaterialExpressionMultiply)
        env.connect(sample,'RGB',multiply,'A')
        env.connect(tint,'RGB',multiply,'B')
        env.output(multiply,'',unreal.MaterialProperty.MP_BASE_COLOR)
        for label,prop in [('metallic',unreal.MaterialProperty.MP_METALLIC),
                           ('roughness',unreal.MaterialProperty.MP_ROUGHNESS)]:
            scalar = env.node(mat,unreal.MaterialExpressionScalarParameter,
                              parameter_name=label.title(),default_value=record[label])
            env.output(scalar,'',prop)
        result[name] = env.finish_material(mat)
    return result


def import_mesh(name):
    path = DESTINATION+'/'+name
    if env.ASSETS.does_asset_exist(path):
        return env.ASSETS.load_asset(path)
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    for key,value in {'asset_name':name,'use_source_name_for_asset':False,
                      'scene_name_sub_folder':False,'asset_type_sub_folders':False,
                      'import_offset_uniform_scale':1.0}.items():
        pipeline.set_editor_property(key,value)
    common = pipeline.get_editor_property('common_meshes_properties')
    for key,value in {'bake_meshes':True,'bake_pivot_meshes':False,'keep_sections_separate':False,
                      'recompute_normals':False,'recompute_tangents':True,'use_mikk_t_space':True,
                      'use_full_precision_u_vs':True,'remove_degenerates':False,'import_lods':False,
                      'auto_detect_mesh_type':False}.items():
        common.set_editor_property(key,value)
    meshes = pipeline.get_editor_property('mesh_pipeline')
    for key,value in {'import_static_meshes':True,'combine_static_meshes':True,
                      'import_skeletal_meshes':False,'collision':False,'build_nanite':False,
                      'generate_lightmap_u_vs':False}.items():
        meshes.set_editor_property(key,value)
    pipeline.get_editor_property('animation_pipeline').set_editor_property('import_animations',False)
    material_pipeline = pipeline.get_editor_property('material_pipeline')
    material_pipeline.set_editor_property('import_materials',False)
    material_pipeline.get_editor_property('texture_pipeline').set_editor_property('import_textures',False)
    parameters = unreal.ImportAssetParameters()
    for key,value in {'is_automated':True,'replace_existing':False,'destination_name':name,
                      'override_pipelines':[unreal.SoftObjectPath(pipeline.get_path_name())]}.items():
        parameters.set_editor_property(key,value)
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    data = unreal.InterchangeManager.create_source_data(str(SOURCE/(name+'.glb')))
    env.require(manager.import_asset(DESTINATION,data,parameters),'GLB import failed '+name)
    return env.require(env.ASSETS.load_asset(path),'Expected imported asset '+path)


def inspect(mesh, name, audit, assigned=None):
    env.require(isinstance(mesh,unreal.StaticMesh),'Expected StaticMesh '+name)
    prefix = 'KATANA_' if name == 'SM_Katana' else 'SAYA_'
    parts = [p for p in audit['meshes'] if p['name'].startswith(prefix)]
    expected_slots = {s for p in parts for s in p['slots']}
    expected_triangles = sum(p['triangles'] for p in parts)
    slots = []
    for i,slot in enumerate(mesh.get_editor_property('static_materials')):
        imported = str(slot.get_editor_property('imported_material_slot_name'))
        label = str(slot.get_editor_property('material_slot_name'))
        token = imported if imported in expected_slots else label
        env.require(token in expected_slots,'Unexpected slot '+label+' / '+imported)
        slots.append(token)
        if assigned: mesh.set_material(i,assigned[token])
        actual = mesh.get_material(i)
        env.require(actual and actual.get_path_name().split('.')[0] == DESTINATION+'/Materials/M_'+token,
                    'Incorrect assigned material '+token)
    env.require(len(slots)==len(expected_slots) and set(slots)==expected_slots,'Wrong material sections '+name)
    env.require(mesh.get_num_triangles(0)==expected_triangles,'Triangle count changed '+name)
    box = mesh.get_bounding_box()
    expected_z = (-12,90) if name == 'SM_Katana' else (-.3,75.8)
    env.require(abs(box.min.z-expected_z[0])<.05 and abs(box.max.z-expected_z[1])<.05,
                'Attachment pivot or scale changed '+name)
    # NullRHI commandlets do not instantiate this editor subsystem. UE 5.7's
    # constructible fallback exposes the same mesh inspection methods.
    editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.StaticMeshEditorSubsystem()
    channels = editor.get_num_uv_channels(mesh,0)
    env.require(channels>=1,'Missing UV0 '+name)
    build = editor.get_lod_build_settings(mesh,0)
    build_settings = {key:build.get_editor_property(key) for key in
                      ['recompute_normals','recompute_tangents','use_mikk_t_space','use_full_precision_u_vs']}
    env.require(not build_settings['recompute_normals'],'Imported source normals were replaced '+name)
    env.require(build_settings['recompute_tangents'] and build_settings['use_mikk_t_space'],
                'Expected UV-derived MikkTSpace tangents '+name)
    env.require(build_settings['use_full_precision_u_vs'],'Expected full precision UV0 '+name)
    if assigned: env.save(mesh)
    return {'path':mesh.get_path_name(),'triangles':expected_triangles,'slots':slots,
            'bounds_min_cm':env.vector_list(box.min),'bounds_max_cm':env.vector_list(box.max),
            'uv_channels':channels,'build_settings':build_settings,
            'normal_verification':'Source GLB unit normals verified; Unreal preserves imported normals',
            'source_forward':'+Z toward tip','cutting_edge':'-X',
            'pivot':'Grip center' if name=='SM_Katana' else 'Scabbard mouth center'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify',action='store_true')
    args = parser.parse_args()
    hashes_before = content_hashes() if args.verify else None
    env.require(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()==ROOT,
                'Wrong Unreal project')
    audit = json.loads((SOURCE/'export-audit.json').read_text())
    for name in ['SM_Katana','SM_Saya']:
        p = SOURCE/(name+'.glb')
        env.require(hashlib.sha256(p.read_bytes()).hexdigest()==audit['exports'][p.name]['sha256'],
                    'Source checksum mismatch '+name)
    assigned = None
    if not args.verify:
        unreal.load_module('BinkAudioDecoder')
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.Enable 1')
        env.ASSETS.make_directory(DESTINATION)
        assigned = materials(audit['materials'])
    results = []
    for name in ['SM_Katana','SM_Saya']:
        mesh = env.ASSETS.load_asset(DESTINATION+'/'+name) if args.verify else import_mesh(name)
        results.append(inspect(mesh,name,audit,assigned))
    material_report = inspect_materials(audit['materials'])
    hashes_after = content_hashes()
    if args.verify:
        env.require(hashes_before==hashes_after,'Read-only verification changed asset files')
    report = {'engine':unreal.SystemLibrary.get_engine_version(),'read_only_verification':args.verify,
              'meshes':results,'materials':material_report,'content_sha256':hashes_after}
    (SOURCE/'unreal-import-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    manifest_path = SOURCE/'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    stage = 'Fresh-load verification' if args.verify else 'Import verification'
    manifest['verification']['unreal'] = (stage+' passed under NullRHI; '
        'imported normals retained, one full-precision UV channel, expected bounds/triangles/slots. '
        'In-game GPU appearance and attachment alignment remain unverified.')
    manifest['unreal_import'] = report
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
    unreal.log('RONIN_KATANA_VERIFIED '+json.dumps(report))


if __name__ == '__main__':
    main()
