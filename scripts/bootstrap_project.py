"""Run inside UnrealEditor-Cmd through scripts/unreal.sh bootstrap."""

from pathlib import Path
import json

import unreal

ROOT = Path(__file__).resolve().parents[1]
ARENA = "/Game/Maps/Arena"
MATERIAL = "/Game/Materials/M_Combat"
SOUNDS = ("Slash", "Clash", "Hit", "Dodge", "Execute")


def ensure_material(assets, tools):
    if assets.does_asset_exist(MATERIAL):
        material = assets.load_asset(MATERIAL)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"Expected a Material at {MATERIAL}")
        # Runtime-created courtyard instances cannot enable missing shader usage in a game build.
        if not material.get_editor_property("used_with_instanced_static_meshes"):
            material.set_editor_property("used_with_instanced_static_meshes", True)
            unreal.MaterialEditingLibrary.recompile_material(material)
            if not assets.save_loaded_asset(material):
                raise RuntimeError(f"Could not save instanced-mesh usage for {MATERIAL}")
        unreal.log(f"Preserving existing {MATERIAL}")
        return
    material = tools.create_asset("M_Combat", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError(f"Could not create {MATERIAL}")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("two_sided", False)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    editing = unreal.MaterialEditingLibrary
    color = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -400, -120)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.3, 0.34, 0.38, 1))
    if not editing.connect_material_property(color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR):
        raise RuntimeError("Could not connect Color to Base Color")
    for y, name, value, property_name in (
        (80, "Roughness", 0.55, unreal.MaterialProperty.MP_ROUGHNESS),
        (260, "Metallic", 0.15, unreal.MaterialProperty.MP_METALLIC),
    ):
        node = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -400, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        if not editing.connect_material_property(node, "", property_name):
            raise RuntimeError(f"Could not connect {name}")
    editing.recompile_material(material)
    if not assets.save_loaded_asset(material):
        raise RuntimeError(f"Could not save {MATERIAL}")


def ensure_audio(assets, tools):
    tasks = []
    for name in SOUNDS:
        destination = f"/Game/Audio/{name}"
        if assets.does_asset_exist(destination):
            unreal.log(f"Preserving existing {destination}")
            continue
        filename = ROOT / ".cache" / "audio" / f"{name}.wav"
        if not filename.is_file():
            raise RuntimeError(f"Missing {filename}; run scripts/generate_audio.py first")
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(filename))
        task.set_editor_property("destination_path", "/Game/Audio")
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", False)
        task.set_editor_property("save", True)
        tasks.append(task)
    if tasks:
        tools.import_asset_tasks(tasks)
    for name in SOUNDS:
        asset_path = f"/Game/Audio/{name}"
        sound = assets.load_asset(asset_path)
        if not isinstance(sound, unreal.SoundWave):
            raise RuntimeError(f"Expected SoundWave at {asset_path}; inspect the import log")


def validate_material(assets):
    material = assets.load_asset(MATERIAL)
    if not isinstance(material, unreal.Material):
        raise RuntimeError(f"Expected a Material at {MATERIAL}")
    editing = unreal.MaterialEditingLibrary
    vectors = {str(name) for name in editing.get_vector_parameter_names(material)}
    scalars = {str(name) for name in editing.get_scalar_parameter_names(material)}
    if "Color" not in vectors or not {"Roughness", "Metallic"}.issubset(scalars):
        raise RuntimeError(f"{MATERIAL} lacks required parameters; repair it in the editor or remove it deliberately and rerun bootstrap")
    if material.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_OPAQUE:
        raise RuntimeError(f"{MATERIAL} must use opaque blending")
    if not material.get_editor_property("used_with_instanced_static_meshes"):
        raise RuntimeError(f"{MATERIAL} must support the courtyard's instanced static meshes")


def main():
    active_project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
    if active_project != ROOT:
        raise RuntimeError(f"Wrong project open: {active_project}; expected {ROOT}")
    # Commandlets skip the audio device manager, which normally registers decoders.
    # SoundFactory and SoundWave loading still need the project-default BINKA factory.
    # Loading the decoder module registers it without opening an audio output device.
    unreal.load_module("BinkAudioDecoder")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    for directory in ("/Game/Maps", "/Game/Materials", "/Game/Audio"):
        if not assets.does_directory_exist(directory):
            if not assets.make_directory(directory):
                raise RuntimeError(f"Could not create {directory}")
    # Python commandlets do not load the startup map automatically.
    if assets.does_asset_exist(ARENA):
        if not levels.load_level(ARENA):
            raise RuntimeError(f"Could not load existing {ARENA}")
    elif not levels.new_level(ARENA, is_partitioned_world=False):
        raise RuntimeError(f"Could not create {ARENA}")
    ensure_material(assets, tools)
    validate_material(assets)
    ensure_audio(assets, tools)
    expected = [ARENA, MATERIAL] + [f"/Game/Audio/{name}" for name in SOUNDS]
    for asset_path in expected:
        if not assets.does_asset_exist(asset_path):
            raise RuntimeError(f"Bootstrap did not produce {asset_path}")
    marker = ROOT / ".cache" / "bootstrap.complete"
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(json.dumps({"engine": unreal.SystemLibrary.get_engine_version(), "assets": expected}, indent=2) + "\n")
    unreal.log("RONIN bootstrap complete. Runtime game mode constructs the arena and fighters.")


if __name__ == "__main__":
    main()
