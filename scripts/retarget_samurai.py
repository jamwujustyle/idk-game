#!/usr/bin/env python3
"""Prepare/inspect/retarget the local Kwang clips with UE 5.7's IK Retargeter.

Ordinary Python --plan prints the contract without loading Unreal. Inspection
and verification run through UnrealEditor-Cmd -nullrhi -corelimit=2 -unattended
-nosound -run=pythonscript -script="<this file> --inspect|--verify".
Retargeting requires initialized Slate because UE 5.7's batch API always opens
a Content Browser notification. Use UnrealEditor-Cmd -nullrhi -renderoffscreen
-corelimit=2 -unattended -nosound -nowrite
-ExecutePythonScript="<this file> --retarget" instead. It quits after the script;
check the report and completion marker as well as process exit status.
--strip-source-curves repairs an earlier local batch, saving only its animation
packages. If legacy curves prevent project startup, use a module-free tools
project at .cache/samurai-retarget-tools with Content linked to this project.
Coordinate with the importer before launching that commandlet. --inspect and
--verify never save Content. --retarget creates new assets only and refuses
collisions; use a new --revision to iterate without overwriting delivered work.

Kwang-derived animations and rig assets remain local, Git-ignored derivatives.
The visible JSON report contains provenance, mappings and validation summaries,
not animation keyframes. No FBX/glTF animation export is performed.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import traceback
import uuid


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = "/Game/Retargeted/KwangSamurai"
SOURCE = "/Game/ParagonKwang/Characters/Heroes/Kwang/Meshes/Kwang_GDC"
TARGET = "/Game/Characters/Samurai/SK_Samurai"
ANIMATIONS = "/Game/ParagonKwang/Characters/Heroes/Kwang/Animations"
CLIPS = (
    "Idle", "Jog_Fwd", "Jog_Bwd", "Jog_Left", "Jog_Right",
    "PrimaryAttack_A_Slow", "PrimaryAttack_B_Slow", "PrimaryAttack_C_Slow", "PrimaryAttack_D_Slow",
    "PrimaryAttack_A_Slow_Recovery", "PrimaryAttack_B_Slow_Recovery", "PrimaryAttack_C_Slow_Recovery",
    "Stun_Start", "Stun_Loop", "Death_Bwd", "Jog_Fwd_Start", "Jog_Bwd_Start",
    "Jog_Left_Start", "Jog_Right_Start", "Ability_RMB",
)
# Chain name: source start/end, target start/end. Fingers are intentionally
# absent: the generated skeleton has unnamed coarse joints, not finger chains.
CHAINS = {
    "Spine": ("spine_01", "spine_03", "Spine", "Spine2"),
    "Neck": ("neck_01", "neck_02", "Neck", "Neck"),
    "Head": ("head", "head", "Head", "Head"),
    "LeftClavicle": ("clavicle_l", "clavicle_l", "LeftShoulder", "LeftShoulder"),
    "RightClavicle": ("clavicle_r", "clavicle_r", "RightShoulder", "RightShoulder"),
    "LeftArm": ("upperarm_l", "hand_l", "LeftArm", "LeftHand"),
    "RightArm": ("upperarm_r", "hand_r", "RightArm", "RightHand"),
    "LeftLeg": ("thigh_l", "foot_l", "LeftUpLeg", "LeftFoot"),
    "RightLeg": ("thigh_r", "foot_r", "RightUpLeg", "RightFoot"),
    "LeftToe": ("ball_l", "ball_l", "LeftToeBase", "LeftToeBase"),
    "RightToe": ("ball_r", "ball_r", "RightToeBase", "RightToeBase"),
}
LIMBS = ("LeftArm", "RightArm", "LeftLeg", "RightLeg")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def canonical(name):
    # FBX/Interchange may sanitize ':' to '_'. Do not fuzzy-match anatomy.
    result = re.sub(r"[^a-z0-9]", "", str(name).lower())
    return result.removeprefix("mixamorig")


def object_path(package):
    return package + "." + package.rsplit("/", 1)[-1]


def content_path(package):
    require(package.startswith("/Game/"), f"Expected a game package: {package}")
    return ROOT / "Content" / (package[6:].split(".", 1)[0] + ".uasset")


def digest(path):
    require(path.is_file(), f"Missing source package: {path}")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def content_snapshot():
    return {str(path.relative_to(ROOT)): (path.stat().st_size, path.stat().st_mtime_ns)
            for path in (ROOT / "Content").rglob("*.uasset")}


def assert_ignored(package):
    relative = str(content_path(package).relative_to(ROOT))
    result = subprocess.run(["git", "check-ignore", "--no-index", "--quiet", relative], cwd=ROOT, check=False)
    require(result.returncode == 0, f"Licensed derivative must be ignored before creation: {relative}")
    tracked = subprocess.run(["git", "ls-files", "--error-unmatch", relative], cwd=ROOT,
                             capture_output=True, check=False)
    require(tracked.returncode != 0, f"Refusing a tracked derivative: {relative}")


def plan(args):
    folder = OUTPUT + ("/" + args.revision if args.revision else "")
    return {
        "source_mesh": object_path(SOURCE), "target_mesh": object_path(args.target),
        "output_folder": folder,
        "rigs": {name: object_path(folder + "/Rigs/" + name)
                 for name in ("IK_Kwang", "IK_Samurai", "RTG_KwangToSamurai")},
        "animations": {name: object_path(folder + "/Animations/A_" + name) for name in args.clip or CLIPS},
        "license": "Local derivatives of Fab Paragon: Kwang; excluded from Git. No source redistribution.",
        "limitations": ["Automatic alignment needs rendered pose review before gameplay acceptance.",
                        "No finger or weapon-bone correspondence is invented.",
                        "Weighted wrist auxiliaries stay at reference locally in these clips; the native pose must synchronize them with Hand motion.",
                        "Root-locked native evaluation must preserve pelvis gait if the imported hips are bone zero."],
    }


def inspect_mesh(ue, mesh, target):
    component = ue.new_object(ue.SkeletalMeshComponent)
    component.set_skeletal_mesh_asset(mesh)
    bones = [str(component.get_bone_name(i)) for i in range(component.get_num_bones())]
    require(bones, "Mesh has no skeleton bones")
    parents = {bone: str(component.get_parent_bone(bone)) for bone in bones}
    index = {}
    for bone in bones:
        index.setdefault(canonical(bone), []).append(bone)

    def find(name):
        matches = index.get(canonical(name), [])
        require(len(matches) == 1, f"Expected one bone for {name}; found {matches}")
        return matches[0]

    def ancestry(start, end):
        branch = [end]
        while branch[-1] != start:
            parent = parents.get(branch[-1])
            require(parent in bones and parent not in branch, f"Invalid chain {start} -> {end}")
            branch.append(parent)
        return list(reversed(branch))

    offset = 2 if target else 0
    chains = {name: {"start": find(spec[offset]), "end": find(spec[offset + 1])}
              for name, spec in CHAINS.items()}
    for record in chains.values():
        record["bones"] = ancestry(record["start"], record["end"])
    for chain, middle in (("LeftArm", "LeftForeArm" if target else "lowerarm_l"),
                          ("RightArm", "RightForeArm" if target else "lowerarm_r"),
                          ("LeftLeg", "LeftLeg" if target else "calf_l"),
                          ("RightLeg", "RightLeg" if target else "calf_r")):
        require(find(middle) in chains[chain]["bones"], f"Missing elbow/knee inside {chain}")
        require(len(chains[chain]["bones"]) >= 3, f"Limb solver needs at least three bones: {chain}")
    pelvis = find("Hips" if target else "pelvis")
    mapped = {pelvis} | {bone for chain in chains.values() for bone in chain["bones"]}
    return {"mesh": mesh.get_path_name(), "skeleton": mesh.get_editor_property("skeleton").get_path_name(),
            "root": bones[0], "pelvis": pelvis, "hip_rooted": bones[0] == pelvis,
            "bones": bones, "parents": parents, "chains": chains,
            "unmapped_bones": [bone for bone in bones if bone not in mapped],
            "unmapped_policy": "Keep target reference local transforms; inherit mapped parent motion"}


def new_asset(ue, assets, path, cls, factory):
    package = path.split(".", 1)[0]
    require(not assets.does_asset_exist(package), f"Asset already exists; choose a fresh --revision: {package}")
    assert_ignored(package)
    folder, name = package.rsplit("/", 1)
    return require(ue.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, cls, factory),
                   f"Could not create {package}")


def build_rig(ue, assets, path, mesh, metadata, target):
    rig = new_asset(ue, assets, path, ue.IKRigDefinition, ue.IKRigDefinitionFactory())
    controller = ue.IKRigController.get_controller(rig)
    require(controller.set_skeletal_mesh(mesh), "Could not assign rig mesh")
    auto_ok = controller.apply_auto_generated_retarget_definition()
    metadata["automatic_characterization"] = {
        "success": auto_ok,
        "chains": [str(chain.chain_name) for chain in controller.get_retarget_chains()],
    }
    # Keep auto characterization as audit evidence, then replace its guesses with
    # checked anatomical chains. This also excludes unsupported coarse fingers.
    for chain in list(controller.get_retarget_chains()):
        require(controller.remove_retarget_chain(chain.chain_name), "Could not clear automatic chain")
    for solver in reversed(range(controller.get_num_solvers())):
        require(controller.remove_solver(solver), "Could not clear automatic solver")
    for goal in list(controller.get_all_goals()):
        require(controller.remove_goal(goal.get_editor_property("goal_name")), "Could not clear automatic goal")
    require(controller.set_retarget_root(metadata["pelvis"]), "Could not set retarget pelvis")
    for name, chain in metadata["chains"].items():
        goal = "None"
        if name in LIMBS:
            goal = name + "_Goal"
            require(str(controller.add_new_goal(goal, chain["end"])) == goal, f"Failed goal: {goal}")
            if target:
                solver = controller.add_solver("/Script/IKRig.IKRigLimbSolver")
                require(solver >= 0, "Could not add a limb solver")
                require(controller.set_start_bone(chain["start"], solver), "Could not set limb start")
                require(controller.connect_goal_to_solver(goal, solver), "Could not connect limb goal")
                require(controller.is_goal_connected_to_solver(goal, solver), "Disconnected limb goal")
        require(str(controller.add_retarget_chain(name, chain["start"], chain["end"], goal)) == name,
                f"Could not add exact chain: {name}")
    require(controller.get_num_solvers() == (4 if target else 0), "Unexpected solver count")
    return rig


def build_retargeter(ue, assets, contract, source, target, source_rig, target_rig, metadata, corrections):
    asset = new_asset(ue, assets, contract["rigs"]["RTG_KwangToSamurai"], ue.IKRetargeter, ue.IKRetargetFactory())
    controller = ue.IKRetargeterController.get_controller(asset)
    for side, mesh, rig in ((ue.RetargetSourceOrTarget.SOURCE, source, source_rig),
                            (ue.RetargetSourceOrTarget.TARGET, target, target_rig)):
        controller.set_ik_rig(side, rig)
        controller.set_preview_mesh(side, mesh)
    require(controller.remove_all_ops(), "Could not reset default retarget operations")
    controller.add_default_ops()
    controller.assign_ik_rig_to_all_ops(ue.RetargetSourceOrTarget.SOURCE, source_rig)
    controller.assign_ik_rig_to_all_ops(ue.RetargetSourceOrTarget.TARGET, target_rig)
    for index in range(controller.get_num_retarget_ops()):
        controller.run_op_initial_setup(index)
    controller.auto_map_chains(ue.AutoMapChainType.EXACT, True)
    for name in CHAINS:
        require(controller.set_source_chain(name, name), f"Cannot map chain: {name}")
        require(str(controller.get_source_chain(name)) == name, f"Chain mapping did not persist: {name}")
    operations = []
    found_root = False
    for index in range(controller.get_num_retarget_ops()):
        op = controller.get_op_controller(index)
        if isinstance(op, ue.IKRetargetRootMotionController):
            found_root = True
            op.set_source_root_bone(metadata["source"]["root"])
            op.set_target_root_bone(metadata["target"]["root"])
            op.set_target_pelvis_bone(metadata["target"]["pelvis"])
            # A hip-rooted Mixamo mesh has no independent trajectory root. The
            # Root Motion op would overwrite Pelvis Motion on that same bone.
            require(controller.set_retarget_op_enabled(index, not metadata["target"]["hip_rooted"]),
                    "Could not configure root motion operation")
        operations.append({"name": str(controller.get_op_name(index)),
                           "controller": op.get_class().get_name(),
                           "enabled": controller.get_retarget_op_enabled(index)})
    require(found_root, "UE default stack did not expose its Root Motion controller")
    side = ue.RetargetSourceOrTarget.TARGET
    pose_name = controller.create_retarget_pose("SamuraiAligned", side)
    require(controller.set_current_retarget_pose(pose_name, side), "Cannot select target alignment pose")
    controller.auto_align_all_bones(side, ue.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
    applied = {}
    for bone, values in corrections.items():
        require(bone in metadata["target"]["bones"], f"Unknown correction bone: {bone}")
        require(len(values) == 4 and all(math.isfinite(float(v)) for v in values), "Expected finite quaternion XYZW")
        require(abs(sum(float(v) ** 2 for v in values) - 1) < 0.001, "Correction quaternion must have unit length")
        # Explicit correction replaces the auto-aligned local offset. Values
        # are authored offsets, never silently added Euler angles.
        controller.set_rotation_offset_for_retarget_pose_bone(bone, ue.Quat(*values), side)
        applied[bone] = values
    metadata["retargeter"] = {"operations": operations, "pose": str(pose_name),
                              "alignment": "ChainToChain, then explicit local quaternion overrides",
                              "manual_overrides": applied,
                              "chains": {name: str(controller.get_source_chain(name)) for name in CHAINS}}
    return asset


def validate_animation(ue, sequence, original, target, metadata):
    library = ue.AnimationLibrary
    require(isinstance(sequence, ue.AnimSequence), "Retarget output is not an animation sequence")
    require(sequence.get_editor_property("skeleton") == target.get_editor_property("skeleton"), "Wrong output skeleton")
    require(sequence.get_editor_property("additive_anim_type") == ue.AdditiveAnimationType.AAT_NONE,
            "Native graph requires nonadditive clips")
    length = library.get_sequence_length(sequence)
    require(abs(length - library.get_sequence_length(original)) < 0.00001, "Retarget changed animation duration")
    require(library.get_num_frames(sequence) == library.get_num_frames(original), "Retarget changed frame count")
    tracks = {str(name) for name in library.get_animation_track_names(sequence)}
    # AnimData's FK Control Rig lowercases track names. FName lookup is case
    # insensitive; keep punctuation/names exact and compare only case-folded.
    require(len(tracks) >= 3 and {name.casefold() for name in tracks} <= {name.casefold() for name in metadata["bones"]},
            f"Missing or foreign output bone tracks in {sequence.get_name()}: {sorted(tracks)}; expected {metadata['bones']}")
    require(not library.get_animation_notify_events(sequence), "Source notifies remain in generated animation")
    require(not library.get_animation_curve_names(sequence, ue.RawCurveTrackTypes.RCT_FLOAT),
            "Unused source float/morph/material curves remain in generated animation")
    require(not library.get_animation_curve_names(sequence, ue.RawCurveTrackTypes.RCT_TRANSFORM),
            "Source transform curves remain in generated animation")
    require(not sequence.get_editor_property("enable_root_motion"), "Gameplay owns translation; root motion must be disabled")
    require(not sequence.get_editor_property("force_root_lock"), "ForceRootLock must not discard animated pelvis motion")
    options = ue.AnimPoseEvaluationOptions()
    options.set_editor_property("optional_skeletal_mesh", target)
    options.set_editor_property("extract_root_motion", False)
    sampled = [metadata["root"], metadata["pelvis"]] + [metadata["chains"][name]["end"] for name in LIMBS]
    positions = {bone: [] for bone in dict.fromkeys(sampled)}
    for step in range(13):
        pose = ue.AnimPoseExtensions.get_anim_pose_at_time(sequence, length * step / 12, options)
        require(ue.AnimPoseExtensions.is_valid(pose), f"Invalid sampled pose: {sequence.get_name()}")
        for bone, history in positions.items():
            transform = ue.AnimPoseExtensions.get_bone_pose(pose, bone, ue.AnimPoseSpaces.WORLD)
            p, q, s = transform.translation, transform.rotation, transform.scale3d
            require(all(math.isfinite(float(v)) for v in (p.x, p.y, p.z, q.x, q.y, q.z, q.w, s.x, s.y, s.z)),
                    f"Nonfinite transform in {bone}")
            require(abs(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w - 1) < 0.01, f"Invalid quaternion in {bone}")
            history.append((p.x, p.y, p.z))
    ranges = {bone: [max(p[axis] for p in history) - min(p[axis] for p in history) for axis in range(3)]
              for bone, history in positions.items()}
    name = original.get_name()
    if name.startswith(("PrimaryAttack", "Jog_")):
        hand_motion = max(math.sqrt(sum(v*v for v in ranges[metadata["chains"][limb]["end"]]))
                          for limb in ("LeftArm", "RightArm"))
        require(hand_motion > 0.2, f"Retarget output appears static: {name}")
    return {"asset": sequence.get_path_name(), "duration_seconds": length,
            "frames": library.get_num_frames(sequence), "bone_tracks": len(tracks),
            "pose_samples_checked": 13, "component_translation_ranges_cm": ranges,
            "notifies": 0, "source_curves": 0, "root_motion": False}


def run_unreal(args, contract, report):
    import unreal as ue
    project = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir())).resolve()
    require(project == ROOT or (project == ROOT / ".cache/samurai-retarget-tools"
                               and (project / "Content").resolve() == ROOT / "Content"), "Wrong project")
    if args.retarget:
        command_line = ue.SystemLibrary.get_command_line().lower()
        require(all(flag in command_line for flag in ("-executepythonscript=", "-nullrhi", "-renderoffscreen")),
                "UE 5.7 batch retarget requires an offscreen NullRHI editor with -ExecutePythonScript; commandlet Slate is unavailable")
    ue.load_module("BinkAudioDecoder")
    ue.load_module("IKRigEditor")
    assets = ue.get_editor_subsystem(ue.EditorAssetSubsystem)
    source = require(assets.load_asset(SOURCE), "Install the licensed Kwang source locally first")
    target = require(assets.load_asset(args.target), "The samurai importer must finish before retargeting")
    originals = {name: require(assets.load_asset(ANIMATIONS + "/" + name), f"Missing source clip: {name}")
                 for name in contract["animations"]}
    for name, asset in originals.items():
        require(isinstance(asset, ue.AnimSequence), f"Expected AnimSequence: {name}")
        require(asset.get_editor_property("skeleton") == source.get_editor_property("skeleton"), f"Source skeleton mismatch: {name}")
        require(asset.get_editor_property("additive_anim_type") == ue.AdditiveAnimationType.AAT_NONE, f"Additive source: {name}")
    protected = [content_path(asset.get_path_name()) for asset in [source, target, *originals.values(),
                 source.get_editor_property("skeleton"), target.get_editor_property("skeleton")]]
    before_hashes = {str(path.relative_to(ROOT)): digest(path) for path in set(protected)}
    before_files = content_snapshot()
    report["source"] = inspect_mesh(ue, source, False)
    report["target"] = inspect_mesh(ue, target, True)
    if args.inspect:
        report["status"] = "inspected; no Content saved"
    elif args.retarget:
        for path in [*contract["rigs"].values(), *contract["animations"].values()]:
            assert_ignored(path)
            require(not assets.does_asset_exist(path), f"Output collision: {path}; use a fresh --revision")
        stage = "SAMURAI_RT_STAGE_" + uuid.uuid4().hex[:12] + "_"
        assert_ignored("/Game/" + stage + "Probe")
        source_rig = build_rig(ue, assets, contract["rigs"]["IK_Kwang"], source, report["source"], False)
        target_rig = build_rig(ue, assets, contract["rigs"]["IK_Samurai"], target, report["target"], True)
        corrections = json.loads(args.pose_corrections.read_text()) if args.pose_corrections else {}
        require(isinstance(corrections, dict), "Pose corrections must be a bone -> quaternion object")
        retargeter = build_retargeter(ue, assets, contract, source, target, source_rig, target_rig, report, corrections)
        # UE 5.7's Python wrapper has no output-directory argument: its default
        # NameRule writes /Game. Give only our new objects a unique staging name,
        # then validate only the returned new copies before renaming. AssetTools
        # rename saves destination packages, so all validation precedes it.
        results = ue.IKRetargetBatchOperation.duplicate_and_retarget(
            [assets.find_asset_data(asset.get_path_name()) for asset in originals.values()],
            source, target, retargeter, "", "", stage, "", False, False)
        require(len(results) == len(originals), "Batch retarget returned the wrong number of clips")
        generated = {}
        for data in results:
            sequence = data.get_asset()
            staged_path, staged_name = sequence.get_path_name(), sequence.get_name()
            require(staged_path.startswith("/Game/" + stage), f"Unexpected batch output: {staged_path}")
            name = staged_name.removeprefix(stage)
            require(name in originals and name not in generated, f"Unexpected/duplicate output: {name}")
            ue.AnimationLibrary.remove_all_animation_notify_tracks(sequence)
            # The native graph consumes bone poses only. Legacy Kwang morph and
            # material curves have no target meaning; retaining them can force
            # target skeleton metadata mutation during early CDO loading.
            ue.AnimationLibrary.remove_all_curve_data(sequence)
            sequence.set_editor_property("enable_root_motion", False)
            # Preserve animated pelvis translation even when Hips is bone zero.
            sequence.set_editor_property("force_root_lock", False)
            generated[name] = sequence
        report["validation"] = {name: validate_animation(ue, sequence, originals[name], target, report["target"])
                                for name, sequence in generated.items()}
        for name, sequence in generated.items():
            destination = contract["animations"][name].split(".", 1)[0]
            require(assets.rename_loaded_asset(sequence, destination), f"Cannot relocate {name}")
            report["validation"][name]["asset"] = sequence.get_path_name()
        for asset in (source_rig, target_rig, retargeter, *generated.values()):
            require(assets.save_loaded_asset(asset, only_if_is_dirty=False), f"Could not save {asset.get_path_name()}")
        # Rename may create transient redirectors. Never delete through load_asset
        # (it follows redirects); unsaved staging packages disappear on exit.
        report["staging_packages_on_disk"] = [str(path.relative_to(ROOT)) for path in (ROOT / "Content").glob(stage + "*.uasset")]
        require(not report["staging_packages_on_disk"], "Unexpected saved staging package; inspect before continuing")
        report["status"] = "retargeted and sampled; fresh-load and rendered review pending"
    elif args.strip_source_curves:
        generated = {name: require(assets.load_asset(path), f"Missing result: {path}")
                     for name, path in contract["animations"].items()}
        for name, sequence in generated.items():
            assert_ignored(contract["animations"][name])
            ue.AnimationLibrary.remove_all_curve_data(sequence)
        report["validation"] = {name: validate_animation(ue, sequence, originals[name], target, report["target"])
                                for name, sequence in generated.items()}
        for sequence in generated.values():
            require(assets.save_loaded_asset(sequence, only_if_is_dirty=False), f"Cannot save {sequence.get_path_name()}")
        report["status"] = "unused source curves stripped; fresh-load verification pending"
    else:
        report["validation"] = {name: validate_animation(ue, require(assets.load_asset(path), f"Missing result: {path}"),
                                                        originals[name], target, report["target"])
                                for name, path in contract["animations"].items()}
        report["status"] = "fresh-load animation validation passed; rendered review pending"
    after_files = content_snapshot()
    changed = sorted(path for path in set(before_files) | set(after_files) if before_files.get(path) != after_files.get(path))
    allowed = "Content/" + contract["output_folder"][6:] + "/"
    require(all((args.retarget or args.strip_source_curves) and path.startswith(allowed) for path in changed),
            f"Unexpected Content changes: {changed}")
    require(all(digest(ROOT / path) == value for path, value in before_hashes.items()), "A source/target package changed")
    report["protected_package_sha256"] = before_hashes
    report["changed_content_packages"] = changed
    report["source_and_target_packages_unchanged"] = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--plan", action="store_true", help="Print the contract with ordinary Python; default")
    modes.add_argument("--inspect", action="store_true", help="Inspect imported anatomy through Unreal, without Content writes")
    modes.add_argument("--retarget", action="store_true", help="Create local ignored rig assets and animation derivatives")
    modes.add_argument("--verify", action="store_true", help="Fresh-load and sample existing results without Content writes")
    modes.add_argument("--strip-source-curves", action="store_true", help="Repair generated clips by removing unused source curves")
    parser.add_argument("--target", default=TARGET, help="Imported skeletal mesh package")
    parser.add_argument("--revision", default="", help="Fresh output subfolder for an alignment iteration")
    parser.add_argument("--clip", action="append", choices=CLIPS, help="Optional proof subset; default transfers all 20 clips")
    parser.add_argument("--pose-corrections", type=Path, help="Reviewed target bone -> local offset quaternion XYZW JSON")
    parser.add_argument("--report", type=Path, help="Visible JSON report path beneath reports/")
    args = parser.parse_args()
    require(not args.revision or re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", args.revision), "Invalid revision folder")
    require(args.target.startswith("/Game/Characters/Samurai/") and "." not in args.target, "Target must be a samurai package")
    require(not args.clip or len(args.clip) == len(set(args.clip)), "Duplicate --clip selection")
    contract = plan(args)
    if not (args.inspect or args.retarget or args.verify or args.strip_source_curves):
        print(json.dumps(contract, indent=2))
        return
    mode = "inspect" if args.inspect else "retarget" if args.retarget else "strip-curves" if args.strip_source_curves else "verify"
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    destination = (args.report or ROOT / "reports/assets" / f"samurai-{mode}-{stamp}.json").resolve()
    require(destination.is_relative_to(ROOT / "reports"), "Report must be visible beneath reports/")
    report = {"created_utc": stamp, "mode": mode, "contract": contract, "status": "started"}
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + "\n")
    try:
        run_unreal(args, contract, report)
    except Exception as error:
        report.update(status="failed", error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2) + "\n")
    print(f"RONIN_SAMURAI_{mode.upper()}_COMPLETE report={destination}")


if __name__ == "__main__":
    main()
