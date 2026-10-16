"""Import the verified CC0 environment in UnrealEditor-Cmd with -nullrhi.

Run download_environment.py first. The importer owns only /Game/Environment and
records actual object paths, local bounds, slots, and mesh counts in the manifest.
"""

import argparse
import hashlib
import json
from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets" / "environment-manifest.json"
BASE = "/Game/Environment"
EDIT = unreal.MaterialEditingLibrary
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
WORLD_TEXTURE = "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture"
WORLD_NORMAL = "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedNormal"
# Triangle counts from the verified FBX polygon-index arrays, in source order.
ROCK_TRIANGLES = [11000, 10996, 5000, 10589, 16548, 8994]
FIR_TRIANGLES = [157402, 150876, 124743]
MATERIAL_PROVENANCE = {
    "M_StoneFloor": {"source": "monastery_stone_floor", "mapping": "world aligned", "texture_scale_cm": 180},
    "M_StoneEdge": {"source": "monastery_stone_floor", "mapping": "world aligned", "texture_scale_cm": 250},
    "M_Cedar": {"source": "japanese_cedar_planks", "mapping": "world aligned", "texture_scale_cm": 110},
    "M_Rock": {"source": "rock_moss_set_01", "mapping": "original mesh UV atlas"},
    "M_FirBark": {"source": "fir_sapling", "maps": "branches", "mapping": "original mesh UVs"},
    "M_FirNeedles": {"source": "fir_sapling", "maps": "twigs", "mapping": "original mesh UVs", "masked": True, "two_sided": True},
    "M_Sky": {"source": "overcast_soil_puresky", "mapping": "camera direction cube", "sky_brightness": 0.35},
}


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def save(asset):
    require(ASSETS.save_loaded_asset(asset), f"Could not save {asset.get_path_name()}")


def node(material, kind, x=0, y=0, **properties):
    result = require(EDIT.create_material_expression(material, kind, x, y), f"Could not create {kind}")
    if isinstance(result, unreal.MaterialExpressionComponentMask):
        # UE's editing helper enables R and G by default. Start with no channels
        # so the supplied flags select exactly the requested packed-map component.
        for channel in "rgba":
            result.set_editor_property(channel, False)
    for name, value in properties.items():
        result.set_editor_property(name, value)
    return result


def connect(source, output, target, input_name):
    require(EDIT.connect_material_expressions(source, output, target, input_name),
            f"Could not connect {source.get_name()}.{output} to {target.get_name()}.{input_name}")


def output(material_node, pin, property_name):
    require(EDIT.connect_material_property(material_node, pin, property_name), f"Could not connect material property {property_name}")


def new_material(name, masked=False):
    path = f"{BASE}/Materials/{name}"
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else TOOLS.create_asset(name, f"{BASE}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    require(isinstance(material, unreal.Material), f"Expected Material at {path}")
    EDIT.delete_all_material_expressions(material)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("two_sided", masked)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED if masked else unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("tangent_space_normal", True)
    if masked:
        material.set_editor_property("opacity_mask_clip_value", 0.35)
    return material


def finish_material(material):
    EDIT.layout_material_expressions(material)
    EDIT.recompile_material(material)
    save(material)
    unreal.log(f"RONIN material ready {material.get_path_name()}")
    return material


def import_texture(record, name):
    path = f"{BASE}/Textures/{name}"
    if ASSETS.does_asset_exist(path):
        texture = ASSETS.load_asset(path)
    else:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(ROOT / record["local_path"]))
        task.set_editor_property("destination_path", f"{BASE}/Textures")
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", False)
        task.set_editor_property("save", True)
        factory = unreal.TextureFactory()
        # Legacy TextureFactory treats HDR input as a long-lat cube map for
        # unattended imports; the result is type-checked below.
        task.set_editor_property("factory", factory)
        TOOLS.import_asset_tasks([task])
        texture = ASSETS.load_asset(path)
    require(isinstance(texture, unreal.Texture), f"Texture import failed: {path}")
    role = record["role"]
    if role == "hdri":
        require(isinstance(texture, unreal.TextureCube), f"HDRI must import as TextureCube: {path}")
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_HDR)
    elif "nor_dx" in role:
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property("flip_green_channel", False)
    elif role.endswith("arm") or role.endswith("alpha"):
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    else:
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    save(texture)
    return texture


def texture_sample(material, texture, name, normal=False, masks=False):
    return node(material, unreal.MaterialExpressionTextureSampleParameter2D,
                parameter_name=name, texture=texture,
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal else
                unreal.MaterialSamplerType.SAMPLERTYPE_MASKS if masks else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)


def aligned_sample(material, texture, name, scale, normal=False, masks=False):
    texture_object = node(material, unreal.MaterialExpressionTextureObjectParameter,
                          parameter_name=name, texture=texture,
                          sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal else
                          unreal.MaterialSamplerType.SAMPLERTYPE_MASKS if masks else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    function_path = WORLD_NORMAL if normal else WORLD_TEXTURE
    # Engine content may not yet be scanned into a commandlet's asset registry.
    function = require(unreal.load_object(None, function_path + "." + function_path.rsplit("/", 1)[-1]), "Missing engine world-aligned function")
    call = node(material, unreal.MaterialExpressionMaterialFunctionCall)
    require(call.set_material_function(function), "Could not bind world-aligned function")
    inputs = list(EDIT.get_material_expression_input_names(call))
    unreal.log(f"RONIN function {function.get_name()} inputs: {inputs}")
    connect(texture_object, "", call, "TextureObject")
    connect(scale, "", call, "TextureSize")
    if normal:
        tangent_output = node(material, unreal.MaterialExpressionStaticBool, value=False)
        connect(tangent_output, "", call, "WorldSpace")
    return call, "XYZ Texture"


def pbr_material(name, textures, world_scale=None, alpha=None):
    material = new_material(name, masked=alpha is not None)
    samples = {}
    if world_scale:
        scale = node(material, unreal.MaterialExpressionScalarParameter, parameter_name="TextureScale", default_value=float(world_scale))
        for role, texture in textures.items():
            samples[role] = aligned_sample(material, texture, role, scale, normal=role == "Normal", masks=role == "ARM")
    else:
        for role, texture in textures.items():
            samples[role] = (texture_sample(material, texture, role, normal=role == "Normal", masks=role == "ARM"), "RGB")
    tint = node(material, unreal.MaterialExpressionVectorParameter, parameter_name="ColorTint", default_value=unreal.LinearColor(1, 1, 1, 1))
    multiply = node(material, unreal.MaterialExpressionMultiply)
    connect(*samples["Diffuse"], multiply, "A")
    connect(tint, "RGB", multiply, "B")
    output(multiply, "", unreal.MaterialProperty.MP_BASE_COLOR)
    output(*samples["Normal"], unreal.MaterialProperty.MP_NORMAL)
    arm, arm_pin = samples["ARM"]
    for channel, property_name in (("r", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION),
                                   ("g", unreal.MaterialProperty.MP_ROUGHNESS),
                                   ("b", unreal.MaterialProperty.MP_METALLIC)):
        mask = node(material, unreal.MaterialExpressionComponentMask, **{channel: True})
        connect(arm, arm_pin, mask, "")
        output(mask, "", property_name)
    if alpha:
        opacity = texture_sample(material, alpha, "Opacity", masks=True)
        output(opacity, "R", unreal.MaterialProperty.MP_OPACITY_MASK)
    return finish_material(material)


def sky_material(texture):
    material = new_material("M_Sky")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("is_sky", True)
    material.set_editor_property("two_sided", True)
    camera = node(material, unreal.MaterialExpressionCameraVectorWS)
    direction = node(material, unreal.MaterialExpressionMultiply, const_b=-1.0)
    connect(camera, "", direction, "A")
    sky = node(material, unreal.MaterialExpressionTextureSampleParameterCube, texture=texture, parameter_name="SkyTexture",
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    connect(direction, "", sky, "")
    brightness = node(material, unreal.MaterialExpressionScalarParameter, parameter_name="SkyBrightness", default_value=0.35)
    exposure = node(material, unreal.MaterialExpressionMultiply)
    connect(sky, "RGB", exposure, "A")
    connect(brightness, "", exposure, "B")
    output(exposure, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish_material(material)


def import_meshes(record, folder, base_name):
    destination = f"{BASE}/{folder}"
    existing = [ASSETS.load_asset(path) for path in ASSETS.list_assets(destination, recursive=False)] if ASSETS.does_directory_exist(destination) else []
    meshes = [asset for asset in existing if isinstance(asset, unreal.StaticMesh)]
    if meshes:
        # These FBXs carry their meter-to-centimeter conversion in the node
        # transforms. Discarding those transforms makes foliage small enough
        # that the mesh builder removes its needle triangles as degenerate.
        valid_transforms = all(mesh.get_editor_property("asset_import_data").get_editor_property("transform_vertex_to_absolute") for mesh in meshes)
        if valid_transforms:
            return sorted(meshes, key=lambda asset: asset.get_path_name())
        for mesh in meshes:
            path = mesh.get_path_name()
            require(ASSETS.delete_loaded_asset(mesh), f"Could not replace incorrectly scaled owned mesh {path}")
        meshes = []
    options = unreal.FbxImportUI()
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("automated_import_should_detect_type", False)
    mesh_options = options.get_editor_property("static_mesh_import_data")
    for key, value in {"combine_meshes": False, "auto_generate_collision": False,
                       "generate_lightmap_u_vs": False, "build_nanite": False,
                       "transform_vertex_to_absolute": True, "bake_pivot_in_vertex": False,
                       "import_uniform_scale": 1.0,
                       "convert_scene": True, "convert_scene_unit": True,
                       "normal_import_method": unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS}.items():
        mesh_options.set_editor_property(key, value)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(ROOT / record["local_path"]))
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("destination_name", base_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", False)
    task.set_editor_property("save", True)
    task.set_editor_property("factory", unreal.FbxFactory())
    task.set_editor_property("options", options)
    TOOLS.import_asset_tasks([task])
    for path in task.get_editor_property("imported_object_paths"):
        asset = ASSETS.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            meshes.append(asset)
    require(meshes, f"FBX import produced no static meshes: {record['local_path']}")
    ordered = sorted(meshes, key=lambda asset: asset.get_path_name())
    for index, mesh in enumerate(ordered, 1):
        final_path = f"{destination}/{base_name}{index:02}"
        require(ASSETS.rename_asset(mesh.get_path_name(), final_path), f"Could not rename imported mesh to {final_path}")
    return ordered


def vector_list(vector):
    return [round(vector.x, 4), round(vector.y, 4), round(vector.z, 4)]


def assign_and_inspect(mesh, materials, foliage=False):
    slots = mesh.get_editor_property("static_materials")
    for index, slot in enumerate(slots):
        imported = str(slot.get_editor_property("imported_material_slot_name"))
        name = str(slot.get_editor_property("material_slot_name"))
        if foliage:
            token = (imported + " " + name).lower()
            if "twig" in token or "leave" in token or "needle" in token:
                material = materials["M_FirNeedles"]
            elif "branch" in token or "bark" in token or "trunk" in token:
                material = materials["M_FirBark"]
            else:
                raise RuntimeError(f"Cannot identify foliage material slot {mesh.get_path_name()} #{index}: {name} / {imported}")
        else:
            material = materials["M_Rock"]
        mesh.set_material(index, material)
    save(mesh)
    bounds = mesh.get_bounding_box()
    # This stateless query helper has no registered subsystem instance in a
    # Python commandlet, so construct a transient helper when needed.
    mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.StaticMeshEditorSubsystem()
    uv_channels = mesh_editor.get_num_uv_channels(mesh, 0)
    require(uv_channels >= 1, f"UV coordinates missing from {mesh.get_path_name()}")
    result = {"object_path": mesh.get_path_name(), "bounds_min_cm": vector_list(bounds.min), "bounds_max_cm": vector_list(bounds.max),
              "triangles_lod0": mesh.get_num_triangles(0), "vertices_lod0": mesh.get_num_vertices(0), "lod_count": mesh.get_num_lods(),
              "uv_channels_lod0": uv_channels, "sections_lod0": mesh.get_num_sections(0),
              "slots": [{"name": str(slot.get_editor_property("material_slot_name")),
                         "imported_name": str(slot.get_editor_property("imported_material_slot_name")),
                         "material": mesh.get_material(index).get_path_name()} for index, slot in enumerate(slots)]}
    unreal.log("RONIN_IMPORTED_MESH " + json.dumps(result))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--material-only", choices=list(MATERIAL_PROVENANCE), help="Rebuild one generated material using existing textures; preserve all meshes")
    arguments = parser.parse_args()
    require(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve() == ROOT, "Wrong project")
    unreal.load_module("BinkAudioDecoder")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    # These tasks deliberately use the stable explicit FbxImportUI and TextureFactory APIs.
    for setting in ("Interchange.FeatureFlags.Import.FBX 0", "Interchange.FeatureFlags.Import.Enable 0"):
        unreal.SystemLibrary.execute_console_command(world, setting)
    manifest = json.loads(MANIFEST.read_text())
    lookup = {entry["id"]: {record["role"]: record for record in entry["files"]} for entry in manifest["assets"]}
    for records in lookup.values():
        for record in records.values():
            path = ROOT / record["local_path"]
            require(path.is_file() and path.stat().st_size == record["size_bytes"], f"Missing or truncated source: {path}")
            require(hashlib.md5(path.read_bytes()).hexdigest() == record["md5"], f"Source checksum mismatch: {path}")
    for folder in ("Materials", "Textures", "Meshes"):
        ASSETS.make_directory(f"{BASE}/{folder}")
    names = {"monastery_stone_floor": "Stone", "japanese_cedar_planks": "Cedar", "rock_moss_set_01": "Rock", "fir_sapling": "Fir", "overcast_soil_puresky": "OvercastSky"}
    textures = {}
    for slug, records in lookup.items():
        textures[slug] = {}
        for role, record in records.items():
            if role != "fbx":
                name = "T_OvercastSky" if role == "hdri" else f"T_{names[slug]}_{role}"
                textures[slug][role] = require(ASSETS.load_asset(f"{BASE}/Textures/{name}"), f"Missing texture {name}") if arguments.material_only else import_texture(record, name)
    def maps(slug, prefix=""):
        source = textures[slug]
        return {"Diffuse": source[prefix + "diff" if prefix else "Diffuse"], "Normal": source[prefix + "nor_dx"], "ARM": source[prefix + "arm"]}
    materials = {}
    for name, source, size in (("M_StoneFloor", maps("monastery_stone_floor"), 180),
                               ("M_Cedar", maps("japanese_cedar_planks"), 110),
                               ("M_StoneEdge", maps("monastery_stone_floor"), 250),
                               ("M_Rock", maps("rock_moss_set_01"), None),
                               ("M_FirBark", maps("fir_sapling", "branches_"), None)):
        if not arguments.material_only or arguments.material_only == name:
            materials[name] = pbr_material(name, source, world_scale=size)
    if not arguments.material_only or arguments.material_only == "M_FirNeedles":
        materials["M_FirNeedles"] = pbr_material("M_FirNeedles", maps("fir_sapling", "twigs_"), alpha=textures["fir_sapling"]["twigs_alpha"])
    if not arguments.material_only or arguments.material_only == "M_Sky":
        materials["M_Sky"] = sky_material(textures["overcast_soil_puresky"]["hdri"])
    if arguments.material_only:
        manifest["unreal_import"]["materials"].update({name: material.get_path_name() for name, material in materials.items()})
        manifest["unreal_import"]["material_provenance"] = MATERIAL_PROVENANCE
        MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n")
        unreal.log(f"RONIN environment material update complete: {arguments.material_only}")
        return
    rocks = import_meshes(lookup["rock_moss_set_01"]["fbx"], "Rocks", "SM_Rock")
    firs = import_meshes(lookup["fir_sapling"]["fbx"], "Firs", "SM_Fir")
    imported = [assign_and_inspect(mesh, materials) for mesh in rocks] + [assign_and_inspect(mesh, materials, foliage=True) for mesh in firs]
    require(len(rocks) == 6 and len(firs) == 3, "Expected six separate rocks and three fir variants")
    require([mesh.get_num_triangles(0) for mesh in rocks] == ROCK_TRIANGLES, "Rock import changed source triangle counts")
    require([mesh.get_num_triangles(0) for mesh in firs] == FIR_TRIANGLES, "Fir import lost needle geometry; inspect import scale and mesh build tolerances")
    manifest["unreal_import"] = {"engine": unreal.SystemLibrary.get_engine_version(),
                                "materials": {name: material.get_path_name() for name, material in materials.items()},
                                "textures": {slug: {role: texture.get_path_name() for role, texture in group.items()} for slug, group in textures.items()},
                                "meshes": imported,
                                "source_triangles": {"rock_moss_set_01": ROCK_TRIANGLES, "fir_sapling": FIR_TRIANGLES},
                                "material_provenance": MATERIAL_PROVENANCE,
                                "verification": "Imported with NullRHI; file integrity, asset types, mesh bounds, triangle counts and material slots verified. GPU appearance requires rendering."}
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n")
    unreal.log("RONIN environment import complete")


if __name__ == "__main__":
    main()
