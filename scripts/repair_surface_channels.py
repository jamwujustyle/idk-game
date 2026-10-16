"""Inspect or repair only the packed ARM output masks of authored surfaces.

Run with UnrealEditor-Cmd -run=pythonscript -script="... --apply" -nullrhi
-corelimit=2 -unattended -nop4 -nosplash. Without --apply this is read-only;
--verify additionally fails if any channel is incorrect. No imports or graph
reconstruction occur. Reports include before/after graphs and SHA-256 hashes of
all Environment files, so unrelated material, texture and mesh writes are caught.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re

import unreal

ROOT = Path(__file__).resolve().parents[1]
BASE = "/Game/Environment/Materials"
SURFACES = (
    "M_StoneFloor", "M_StoneEdge", "M_Cedar", "M_Rock", "M_FirBark", "M_FirNeedles",
    "M_ForestGround", "M_FirMediumBark", "M_FirMediumNeedles", "M_ToriiCedar",
)
CHANNELS = (
    ("ambient_occlusion", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION, "r"),
    ("roughness", unreal.MaterialProperty.MP_ROUGHNESS, "g"),
    ("metallic", unreal.MaterialProperty.MP_METALLIC, "b"),
)
EDIT = unreal.MaterialEditingLibrary
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def file_hashes():
    return {str(path.relative_to(ROOT)): sha256(path)
            for path in sorted((ROOT / "Content/Environment").rglob("*")) if path.is_file()}


def snapshot(material):
    result = {}
    masks = []
    for name, prop, expected in CHANNELS:
        mask = EDIT.get_material_property_input_node(material, prop)
        require(isinstance(mask, unreal.MaterialExpressionComponentMask),
                f"Unexpected {name} input on {material.get_path_name()}; refusing to rewire it")
        upstream = EDIT.get_inputs_for_material_expression(material, mask)
        require(len(upstream) == 1 and upstream[0], f"Expected one packed-map input for {mask.get_path_name()}")
        result[name] = {"node": mask.get_path_name(), "expected": expected,
                        "enabled": [channel for channel in "rgba" if mask.get_editor_property(channel)],
                        "source": upstream[0].get_path_name(),
                        "property_output": EDIT.get_material_property_input_node_output_name(material, prop)}
        masks.append(mask)
    require(len(set(row["node"] for row in result.values())) == 3,
            f"Expected three distinct ARM masks on {material.get_path_name()}")
    require(len(set(row["source"] for row in result.values())) == 1,
            f"Expected the three masks to share their packed-map input on {material.get_path_name()}")
    return result, masks


def export_graph(material, filename):
    task = unreal.AssetExportTask()
    task.object = material
    task.filename = str(filename)
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.exporter = unreal.ObjectExporterT3D()
    require(unreal.Exporter.run_asset_export_task(task), f"Could not export {material.get_path_name()}")
    return filename.read_text()


def unchanged_graph(text, masks):
    """Ignore only the repaired booleans and engine-generated material cache IDs."""
    mask_names = {mask.get_name() for mask in masks}
    current = None
    lines = []
    for line in text.splitlines():
        match = re.search(r'Begin Object (?:Class=\S+ )?Name="([^"]+)"', line)
        if match:
            current = match.group(1)
        if current in mask_names and re.fullmatch(r"\s*[RGBA]=(True|False)\s*", line):
            continue
        if re.fullmatch(r"\s*(StateId|LightingGuid)=[A-Fa-f0-9]+\s*", line):
            continue
        lines.append(line)
        if line.strip() == "End Object":
            current = None
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--apply", action="store_true")
    modes.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    require(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve() == ROOT,
            "Wrong project")
    unreal.load_module("BinkAudioDecoder")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder = ROOT / "reports/assets" / ("surface-channels-" + stamp)
    folder.mkdir(parents=True)
    result = {"mode": "apply" if args.apply else "verify" if args.verify else "inspect",
              "materials": {}, "files_before": file_hashes(), "saved": []}
    pending = []
    try:
        # Validate the complete target set before making any in-memory edits.
        for name in SURFACES:
            material = ASSETS.load_asset(f"{BASE}/{name}")
            require(isinstance(material, unreal.Material), f"Missing authored surface: {name}")
            before, masks = snapshot(material)
            text = export_graph(material, folder / (name + "-before.t3d"))
            result["materials"][name] = {"before": before}
            pending.append((name, material, masks, text))
        for name, material, masks, text in pending:
            changed = any(row["enabled"] != [row["expected"]]
                          for row in result["materials"][name]["before"].values())
            if args.apply and changed:
                for (_, _, selected), mask in zip(CHANNELS, masks):
                    for channel in "rgba":
                        mask.set_editor_property(channel, channel == selected)
                EDIT.recompile_material(material)
            after, _ = snapshot(material)
            after_text = export_graph(material, folder / (name + "-after.t3d"))
            intact = unchanged_graph(text, masks) == unchanged_graph(after_text, masks)
            result["materials"][name].update({"after": after, "other_graph_data_unchanged": intact,
                                               "needs_repair_before": changed})
            require(intact, f"Unexpected graph change on {name}; refusing to save")
            if args.apply or args.verify:
                require(all(row["enabled"] == [row["expected"]] for row in after.values()),
                        f"Incorrect packed channel on {name}")
        # All graphs passed structural validation. Save only changed materials.
        if args.apply:
            for name, material, _, _ in pending:
                if result["materials"][name]["needs_repair_before"]:
                    require(ASSETS.save_loaded_asset(material), f"Could not save {name}")
                    result["saved"].append(name)
        result["files_after"] = file_hashes()
        changed_files = sorted(path for path in set(result["files_before"]) | set(result["files_after"])
                               if result["files_before"].get(path) != result["files_after"].get(path))
        result["changed_files"] = changed_files
        expected_files = {f"Content/Environment/Materials/{name}.uasset" for name in result["saved"]}
        require(set(changed_files) == expected_files, f"Unexpected asset-file changes: {changed_files}")
        result["success"] = True
        unreal.log(f"SURFACE_CHANNELS_COMPLETE mode={result['mode']} saved={len(result['saved'])} "
                   f"unchanged_other_files={len(result['files_before']) - len(changed_files)} report={folder}")
    except Exception as error:
        result["success"] = False
        result["error"] = str(error)
        raise
    finally:
        (folder / "report.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
