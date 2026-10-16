#!/usr/bin/env python3
"""Prepare or import the small CC0 forest-ground set without changing the scene.

Ordinary Python: --download fetches three 2K maps; --verify (the default) checks
cached bytes. Unreal Python: --import-assets creates only the listed textures
and M_ForestGround. Run the latter explicitly in an idle NullRHI commandlet with
-corelimit=2; it does not launch Unreal or assign materials to any actor.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import urllib.parse
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets" / "ground-manifest.json"
SLUG = "forrest_ground_01"  # Provider's canonical identifier includes two r's.
CACHE = ROOT / ".cache" / "ground" / SLUG
MATERIAL_PATH = "/Game/Environment/Materials/M_ForestGround"
TEXTURE_FOLDER = "/Game/Environment/Textures"
TEXTURE_NAMES = {"Diffuse": "T_ForestGround_Diffuse", "nor_dx": "T_ForestGround_Normal", "arm": "T_ForestGround_ARM"}
WORLD_FUNCTIONS = {
    "texture": "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture.WorldAlignedTexture",
    "normal": "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedNormal.WorldAlignedNormal",
}
MAX_BYTES = 20_000_000


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def checksum(path, algorithm="md5"):
    result = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def request(url):
    parsed = urllib.parse.urlparse(url)
    require(parsed.scheme == "https" and parsed.hostname in {"api.polyhaven.com", "dl.polyhaven.org"},
            f"Unexpected source URL: {url}")
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "RoninAssetResearch/1.0"}), timeout=120)


def write_manifest(data):
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    temporary = MANIFEST.with_suffix(".json.partial")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(MANIFEST)


def verify_record(record):
    path = (ROOT / record["local_path"]).resolve()
    require(path.is_relative_to(CACHE.resolve()), "Ground source must remain inside its cache directory")
    require(path.is_file() and path.stat().st_size == record["size_bytes"], f"Missing or wrong-size source: {path}")
    require(checksum(path) == record["md5"], f"Provider MD5 mismatch: {path}")
    if record.get("sha256"):
        require(checksum(path, "sha256") == record["sha256"], f"Local SHA-256 mismatch: {path}")
    return path


def download():
    with request(f"https://api.polyhaven.com/info/{SLUG}") as response:
        info = json.load(response)
    with request(f"https://api.polyhaven.com/files/{SLUG}") as response:
        files = json.load(response)
    records = []
    for role in TEXTURE_NAMES:
        item = files[role]["2k"]["jpg"]
        name = Path(urllib.parse.urlparse(item["url"]).path).name
        records.append({"role": role, "format": "jpg", "resolution": "2k", "url": item["url"],
                        "size_bytes": item["size"], "md5": item["md5"],
                        "local_path": str((CACHE / name).relative_to(ROOT))})
    total = sum(item["size_bytes"] for item in records)
    require(total <= MAX_BYTES, f"Ground downloads exceed the {MAX_BYTES}-byte limit: {total}")
    CACHE.mkdir(parents=True, exist_ok=True)
    for record in records:
        destination = ROOT / record["local_path"]
        if not (destination.is_file() and destination.stat().st_size == record["size_bytes"]
                and checksum(destination) == record["md5"]):
            temporary = destination.with_suffix(".jpg.partial")
            with request(record["url"]) as response, temporary.open("wb") as output:
                while block := response.read(1024 * 1024):
                    output.write(block)
                    require(output.tell() <= record["size_bytes"], f"Unexpected download size: {destination.name}")
            require(temporary.stat().st_size == record["size_bytes"] and checksum(temporary) == record["md5"],
                    f"Downloaded asset failed provider integrity check: {destination.name}")
            temporary.replace(destination)
        record["sha256"] = checksum(destination, "sha256")
        verify_record(record)
        print(f"Verified {destination.name}: {record['size_bytes']:,} bytes", flush=True)
    result = {
        "provider": "Poly Haven", "license": "CC0-1.0", "license_url": "https://polyhaven.com/license",
        "retrieved_at_utc": datetime.now(timezone.utc).isoformat(), "download_bytes": total,
        "verification": "All three file sizes and MD5 checksums match official API metadata; SHA-256 records local bytes.",
        "asset": {"id": SLUG, "name": info["name"], "authors": info["authors"],
                  "source": f"https://polyhaven.com/a/{SLUG}",
                  "metadata_source": f"https://api.polyhaven.com/files/{SLUG}",
                  "info_source": f"https://api.polyhaven.com/info/{SLUG}",
                  "physical_size_cm": [value / 10 for value in info["dimensions"]],
                  "selection_reason": "Natural earthy forest floor with leaf litter, twigs, subdued moss and dry grass; no paving pattern.",
                  "files": records},
        "prepared_import": {"script": "scripts/import_ground.py", "status": "prepared, not imported",
                            "material_path": MATERIAL_PATH,
                            "texture_paths": {role: f"{TEXTURE_FOLDER}/{name}" for role, name in TEXTURE_NAMES.items()},
                            "texture_scale_cm": 200, "mapping": "world aligned",
                            "normal_convention": "DirectX source; WorldAlignedNormal tangent-space output",
                            "arm_channels": {"R": "ambient occlusion", "G": "roughness", "B": "metallic"}},
    }
    if MANIFEST.exists():
        previous = json.loads(MANIFEST.read_text())
        if "unreal_import" in previous:
            result["unreal_import"] = previous["unreal_import"]
            result["prepared_import"]["status"] = previous["prepared_import"]["status"]
        if "scene_assignment" in previous:
            result["scene_assignment"] = previous["scene_assignment"]
    write_manifest(result)
    print(f"GROUND_SOURCES_VERIFIED files=3 bytes={total} manifest={MANIFEST.relative_to(ROOT)}")
    return result


def verify():
    data = json.loads(MANIFEST.read_text())
    require(data["asset"]["id"] == SLUG, "Unexpected ground source selection")
    records = data["asset"]["files"]
    require(len(records) == 3 and {item["role"] for item in records} == set(TEXTURE_NAMES), "Expected exactly three PBR maps")
    require(sum(item["size_bytes"] for item in records) <= MAX_BYTES, "Ground source budget exceeded")
    for record in records:
        require(record["resolution"] == "2k" and record["format"] == "jpg", "Expected selected 2K JPG sources")
        verify_record(record)
    print(f"GROUND_SOURCES_VERIFIED files={len(records)} bytes={data['download_bytes']}")
    return data


def import_assets(data):
    import unreal

    require(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve() == ROOT, "Wrong project")
    unreal.load_module("BinkAudioDecoder")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    for setting in ("Interchange.FeatureFlags.Import.FBX 0", "Interchange.FeatureFlags.Import.Enable 0"):
        unreal.SystemLibrary.execute_console_command(world, setting)
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    edit = unreal.MaterialEditingLibrary

    def save(asset):
        require(assets.save_loaded_asset(asset), f"Could not save {asset.get_path_name()}")

    def node(kind, **properties):
        result = require(edit.create_material_expression(material, kind), f"Could not create {kind}")
        for key, value in properties.items():
            result.set_editor_property(key, value)
        return result

    def connect(source, pin, target, input_name):
        require(edit.connect_material_expressions(source, pin, target, input_name), f"Could not connect {input_name}")

    textures = {}
    for record in data["asset"]["files"]:
        role = record["role"]
        name = TEXTURE_NAMES[role]
        path = f"{TEXTURE_FOLDER}/{name}"
        if not assets.does_asset_exist(path):
            task = unreal.AssetImportTask()
            for key, value in {"filename": str(verify_record(record)), "destination_path": TEXTURE_FOLDER,
                               "destination_name": name, "automated": True, "replace_existing": False,
                               "save": False, "factory": unreal.TextureFactory()}.items():
                task.set_editor_property(key, value)
            tools.import_asset_tasks([task])
        texture = require(assets.load_asset(path), f"Texture import failed: {path}")
        require(isinstance(texture, unreal.Texture2D), f"Expected Texture2D: {path}")
        texture.set_editor_property("max_texture_size", 2048)
        texture.set_editor_property("srgb", role == "Diffuse")
        texture.set_editor_property("compression_settings", {
            "Diffuse": unreal.TextureCompressionSettings.TC_DEFAULT,
            "nor_dx": unreal.TextureCompressionSettings.TC_NORMALMAP,
            "arm": unreal.TextureCompressionSettings.TC_MASKS}[role])
        if role == "nor_dx":
            texture.set_editor_property("flip_green_channel", False)
        save(texture)
        textures[role] = texture

    material = assets.load_asset(MATERIAL_PATH) if assets.does_asset_exist(MATERIAL_PATH) else tools.create_asset(
        "M_ForestGround", "/Game/Environment/Materials", unreal.Material, unreal.MaterialFactoryNew())
    require(isinstance(material, unreal.Material), f"Expected Material: {MATERIAL_PATH}")
    edit.delete_all_material_expressions(material)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("tangent_space_normal", True)
    scale = node(unreal.MaterialExpressionScalarParameter, parameter_name="TextureScale", default_value=200.0)
    samples = {}
    for role, texture in textures.items():
        sample = node(unreal.MaterialExpressionTextureObjectParameter, parameter_name=role, texture=texture,
                      sampler_type={"Diffuse": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                                    "nor_dx": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                                    "arm": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}[role])
        function = require(unreal.load_object(None, WORLD_FUNCTIONS["normal" if role == "nor_dx" else "texture"]),
                           "Missing engine world-aligned material function")
        call = node(unreal.MaterialExpressionMaterialFunctionCall)
        require(call.set_material_function(function), "Could not assign world-aligned material function")
        connect(sample, "", call, "TextureObject")
        connect(scale, "", call, "TextureSize")
        if role == "nor_dx":
            # Match the verified environment materials: the function converts its
            # world-aligned normal into tangent space when WorldSpace is false.
            tangent = node(unreal.MaterialExpressionStaticBool, value=False)
            connect(tangent, "", call, "WorldSpace")
        samples[role] = call
    tint = node(unreal.MaterialExpressionVectorParameter, parameter_name="ColorTint", default_value=unreal.LinearColor(1, 1, 1, 1))
    color = node(unreal.MaterialExpressionMultiply)
    connect(samples["Diffuse"], "XYZ Texture", color, "A")
    connect(tint, "RGB", color, "B")
    require(edit.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR), "Could not connect base color")
    require(edit.connect_material_property(samples["nor_dx"], "XYZ Texture", unreal.MaterialProperty.MP_NORMAL), "Could not connect normal")
    for channel, prop in (("r", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION),
                          ("g", unreal.MaterialProperty.MP_ROUGHNESS), ("b", unreal.MaterialProperty.MP_METALLIC)):
        # MaterialEditingLibrary creates masks with R and G already enabled.
        # Explicitly clear every other channel: scalar inputs must not receive RG/RGB.
        mask = node(unreal.MaterialExpressionComponentMask,
                    **{candidate: candidate == channel for candidate in "rgba"})
        connect(samples["arm"], "XYZ Texture", mask, "")
        require(edit.connect_material_property(mask, "", prop), f"Could not connect ARM channel {channel}")
        require(edit.get_material_property_input_node(material, prop) == mask and
                [candidate for candidate in "rgba" if mask.get_editor_property(candidate)] == [channel],
                f"Expected exactly one connected ARM channel: {channel}")
    edit.layout_material_expressions(material)
    edit.recompile_material(material)
    save(material)
    data["prepared_import"]["status"] = "imported"
    data["unreal_import"] = {"imported_at_utc": datetime.now(timezone.utc).isoformat(),
                             "material": material.get_path_name(),
                             "textures": {role: texture.get_path_name() for role, texture in textures.items()},
                             "asset_count": 4}
    write_manifest(data)
    unreal.log("GROUND_IMPORT_COMPLETE assets=4 scene_assignment_unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--download", action="store_true")
    modes.add_argument("--verify", action="store_true")
    modes.add_argument("--import-assets", action="store_true")
    args = parser.parse_args()
    manifest = download() if args.download else verify()
    if args.import_assets:
        import_assets(manifest)
