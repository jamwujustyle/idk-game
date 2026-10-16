#!/usr/bin/env python3
"""Verify/download prepared medium-fir sources, or explicitly import their assets.

Default --verify is ordinary Python and never imports Unreal. --download restores
the manifest's eight verified source files. --import-assets is for a separately
authorized idle UnrealEditor-Cmd -nullrhi -corelimit=2 pass; it creates the listed
medium-fir assets only and never places actors or edits the existing saplings.
--material-only updates just M_FirMediumNeedles from its existing textures,
without importing textures or rebuilding any mesh.
"""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

# The Unreal Python commandlet does not add the executed script's folder to
# sys.path, unlike ordinary Python. Resolve the two local helper modules there.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from import_ground import checksum, request, require


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets" / "forest-manifest.json"
CACHE = ROOT / ".cache" / "forest" / "fir_sapling_medium"
MAX_BYTES = 60_000_000
SUBSURFACE_STRENGTH = 0.35


def save_manifest(data):
    temporary = MANIFEST.with_suffix(".json.partial")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(MANIFEST)


def sources(download=False):
    data = json.loads(MANIFEST.read_text())
    require(data["asset"]["id"] == "fir_sapling_medium", "Unexpected forest source")
    records = data["asset"]["files"]
    require(len(records) == 8 and sum(item["size_bytes"] for item in records) <= MAX_BYTES,
            "Expected the prepared FBX plus seven small PBR/alpha maps")
    for record in records:
        path = (ROOT / record["local_path"]).resolve()
        require(path.is_relative_to(CACHE.resolve()), "Forest source must remain inside its cache directory")
        valid = path.is_file() and path.stat().st_size == record["size_bytes"] and checksum(path) == record["md5"]
        if not valid and download:
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary = path.with_suffix(path.suffix + ".partial")
            with request(record["url"]) as response, temporary.open("wb") as output:
                while block := response.read(1024 * 1024):
                    output.write(block)
                    require(output.tell() <= record["size_bytes"], f"Unexpected download size: {path.name}")
            require(temporary.stat().st_size == record["size_bytes"] and checksum(temporary) == record["md5"],
                    f"Provider integrity check failed: {path.name}")
            temporary.replace(path)
            valid = True
        require(valid, f"Missing or invalid source; run --download: {path}")
        require(checksum(path, "sha256") == record["sha256"], f"Local SHA-256 mismatch: {path}")
    print(f"FOREST_SOURCES_VERIFIED files={len(records)} bytes={data['download_bytes']}")
    return data


def import_assets(data, material_only=False):
    import unreal
    # Reuse the already exercised FBX/UV-material helpers, with new asset names
    # and a separate mesh folder. Importing this module does not run its main().
    import import_environment as environment

    require(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve() == ROOT, "Wrong project")
    unreal.load_module("BinkAudioDecoder")
    if not material_only:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        for setting in ("Interchange.FeatureFlags.Import.FBX 0", "Interchange.FeatureFlags.Import.Enable 0"):
            unreal.SystemLibrary.execute_console_command(world, setting)
    records = {record["role"]: record for record in data["asset"]["files"]}
    textures = {}
    for role, record in records.items():
        if role == "fbx" or (material_only and not role.startswith("twigs_")):
            continue
        if material_only:
            texture = require(environment.ASSETS.load_asset(f"/Game/Environment/Textures/T_FirMedium_{role}"),
                              f"Material-only update requires the existing texture: {role}")
        else:
            texture = environment.import_texture(record, f"T_FirMedium_{role}")
            texture.set_editor_property("max_texture_size", 1024)
            environment.save(texture)
        textures[role] = texture

    def maps(prefix):
        return {"Diffuse": textures[prefix + "diff"], "Normal": textures[prefix + "nor_dx"], "ARM": textures[prefix + "arm"]}

    if material_only:
        require(environment.ASSETS.does_asset_exist("/Game/Environment/Materials/M_FirMediumNeedles"),
                "Material-only update requires the existing medium-fir needle material")
    needles = environment.pbr_material("M_FirMediumNeedles", maps("twigs_"), alpha=textures["twigs_alpha"])
    # UE 5.7 EMaterialShadingModel::MSM_TwoSidedFoliage and
    # EMaterialProperty::MP_SubsurfaceColor are exposed with these Python names.
    # Transmission comes from the same tinted diffuse color; it is not emissive.
    needles.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    base = require(environment.EDIT.get_material_property_input_node(needles, unreal.MaterialProperty.MP_BASE_COLOR),
                   "Needles must retain their diffuse base-color input")
    base_pin = environment.EDIT.get_material_property_input_node_output_name(needles, unreal.MaterialProperty.MP_BASE_COLOR)
    strength = environment.node(needles, unreal.MaterialExpressionScalarParameter,
                                parameter_name="SubsurfaceStrength", default_value=SUBSURFACE_STRENGTH,
                                slider_min=0.0, slider_max=1.0)
    subsurface = environment.node(needles, unreal.MaterialExpressionMultiply)
    environment.connect(base, base_pin, subsurface, "A")
    environment.connect(strength, "", subsurface, "B")
    environment.output(subsurface, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    require(needles.get_editor_property("two_sided") and needles.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_MASKED,
            "Needles must retain their two-sided masked surface")
    opacity = require(environment.EDIT.get_material_property_input_node(needles, unreal.MaterialProperty.MP_OPACITY_MASK),
                      "Needles must retain their alpha-mask input")
    require(opacity.get_editor_property("texture") == textures["twigs_alpha"], "Needle opacity must use the original alpha map")
    environment.finish_material(needles)
    metadata = {"source": "fir_sapling_medium", "mapping": "original mesh UVs", "masked": True,
                "two_sided": True, "opacity_mask_clip": needles.get_editor_property("opacity_mask_clip_value"),
                "shading_model": "Two Sided Foliage", "subsurface_color": "tinted diffuse RGB multiplied by SubsurfaceStrength",
                "subsurface_strength": SUBSURFACE_STRENGTH, "alpha_source": textures["twigs_alpha"].get_path_name(),
                "verification": "Material graph saved with NullRHI; backlighting and compiled GPU appearance require a rendered check."}
    data["prepared_import"]["needles"] = metadata
    if material_only:
        data["unreal_import"].setdefault("material_metadata", {})["M_FirMediumNeedles"] = metadata
        data["unreal_import"]["material_updated_at_utc"] = datetime.now(timezone.utc).isoformat()
        save_manifest(data)
        unreal.log("FOREST_MATERIAL_UPDATE_COMPLETE material=M_FirMediumNeedles meshes_rebuilt=0 textures_written=0")
        return
    bark = environment.pbr_material("M_FirMediumBark", maps("branches_"))
    # Preserve the FBX's 100x node transform. The source vertex coordinates are
    # meters; ignoring this transform previously collapsed small needle faces.
    meshes = environment.import_meshes(records["fbx"], "FirsMedium", "SM_FirMedium")
    require(len(meshes) == 3, "Expected three separate medium-fir variants")
    material_aliases = {"M_FirBark": bark, "M_FirNeedles": needles}
    imported = [environment.assign_and_inspect(mesh, material_aliases, foliage=True) for mesh in meshes]
    expected = data["source_inspection"]["meshes"]
    require([mesh["triangles_lod0"] for mesh in imported] == [mesh["triangles"] for mesh in expected],
            "Medium-fir import changed source triangle counts; inspect transforms before placement")
    for actual, source in zip(imported, expected):
        height = actual["bounds_max_cm"][2] - actual["bounds_min_cm"][2]
        require(abs(height - source["height_m"] * 100) < 1,
                f"Unexpected medium-fir height: {actual['object_path']} = {height} cm")
        require(actual["sections_lod0"] == 3, f"Missing source material section: {actual['object_path']}")
    data["prepared_import"]["status"] = "imported"
    data["unreal_import"] = {
        "imported_at_utc": datetime.now(timezone.utc).isoformat(), "engine": unreal.SystemLibrary.get_engine_version(),
        "textures": {role: texture.get_path_name() for role, texture in textures.items()},
        "materials": {"bark": bark.get_path_name(), "needles": needles.get_path_name()},
        "material_metadata": {"M_FirMediumNeedles": metadata},
        "meshes": imported, "asset_count": 12,
    }
    save_manifest(data)
    unreal.log("FOREST_IMPORT_COMPLETE assets=12 scene_assignment_unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--verify", action="store_true")
    modes.add_argument("--download", action="store_true")
    modes.add_argument("--import-assets", action="store_true")
    modes.add_argument("--material-only", action="store_true")
    arguments = parser.parse_args()
    manifest = sources(download=arguments.download)
    if arguments.import_assets or arguments.material_only:
        import_assets(manifest, material_only=arguments.material_only)
