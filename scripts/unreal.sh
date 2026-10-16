#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
PROJECT="$PROJECT_ROOT/Ronin.uproject"

usage() {
    cat <<'HELP'
Usage: scripts/unreal.sh <command> [Unreal arguments...]

  setup          Download/setup the selected engine's native C++ toolchain
  build          Build RoninEditor (Development, Linux)
  bootstrap      Generate/import audio, material, and empty arena map; no GPU needed
  play           Run the arena in a game window
  editor         Open the project in Unreal Editor
  package-linux  Build, cook, and package to Builds/Linux

Set UE_ROOT to the extracted engine root, or link it at .tools/unreal.
RONIN_BUILD_JOBS selects 1–8 compile actions (default: 8).
Example: scripts/unreal.sh play -windowed -ResX=1600 -ResY=1000
HELP
}

die() { printf 'RONIN: %s\n' "$*" >&2; exit 1; }
require_executable() { [[ -x "$1" ]] || die "Executable missing: $1. Check the Linux engine extraction and permissions."; }

command_name="${1:-help}"
if (( $# )); then shift; fi
case "$command_name" in
    help|-h|--help) usage; exit 0 ;;
    setup|build|bootstrap|play|editor|package-linux) ;;
    *) usage >&2; die "Unknown command: $command_name" ;;
esac

engine_candidate="${UE_ROOT:-$PROJECT_ROOT/.tools/unreal}"
[[ -d "$engine_candidate/Engine" ]] || die "Unreal Engine not found. Set UE_ROOT to its extracted root (containing Engine/), or create .tools/unreal. See docs/SETUP.md."
ENGINE_ROOT="$(cd -- "$engine_candidate" && pwd -P)"
export UE_ROOT="$ENGINE_ROOT"
EDITOR="$ENGINE_ROOT/Engine/Binaries/Linux/UnrealEditor"
EDITOR_CMD="$ENGINE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"
BUILD="$ENGINE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"
UAT="$ENGINE_ROOT/Engine/Build/BatchFiles/RunUAT.sh"
JOBS="${RONIN_BUILD_JOBS:-8}"
[[ "$JOBS" =~ ^[1-8]$ ]] || die "RONIN_BUILD_JOBS must be an integer from 1 to 8."
export __NV_PRIME_RENDER_OFFLOAD=1
export __VK_LAYER_NV_optimus=NVIDIA_only

require_content() {
    local asset
    for asset in Maps/Arena.umap Materials/M_Combat.uasset Audio/Slash.uasset Audio/Clash.uasset Audio/Hit.uasset Audio/Dodge.uasset Audio/Execute.uasset; do
        [[ -f "$PROJECT_ROOT/Content/$asset" ]] || die "Missing Content/$asset. Run build, then bootstrap first."
    done
}

case "$command_name" in
    setup)
        TOOLCHAIN="$ENGINE_ROOT/Engine/Build/BatchFiles/Linux/SetupToolchain.sh"
        require_executable "$TOOLCHAIN"
        cd -- "$ENGINE_ROOT"
        exec "$TOOLCHAIN" "$@"
        ;;
    build)
        require_executable "$BUILD"
        # Prevent user-supplied concurrency flags from defeating the memory cap.
        for argument in "$@"; do
            case "${argument,,}" in
                -maxparallelactions*|-maxprocessorcount*) die "Set RONIN_BUILD_JOBS=1..8 instead of overriding compile concurrency." ;;
            esac
        done
        exec "$BUILD" RoninEditor Linux Development "-Project=$PROJECT" -WaitMutex -NoHotReload "$@" "-MaxParallelActions=$JOBS"
        ;;
    bootstrap)
        require_executable "$EDITOR_CMD"
        command -v python3 >/dev/null || die "python3 is required to synthesize the audio."
        python3 "$PROJECT_ROOT/scripts/generate_audio.py"
        marker="$PROJECT_ROOT/.cache/bootstrap.complete"
        rm -f -- "$marker"
        "$EDITOR_CMD" "$PROJECT" -run=pythonscript "-script=$PROJECT_ROOT/scripts/bootstrap_project.py" -nullrhi -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput "$@"
        [[ -f "$marker" ]] || die "Unreal did not confirm bootstrap completion. Inspect Saved/Logs/Ronin.log for Python errors."
        require_content
        printf 'RONIN: map, material, and audio are ready.\n'
        ;;
    play|editor)
        require_executable "$EDITOR"
        require_content
        "$PROJECT_ROOT/scripts/check-environment.sh" --require-gpu
        launch_args=("$PROJECT" /Game/Maps/Arena -log)
        if [[ "$command_name" == play ]]; then
            launch_args+=(-game -windowed -ResX=1600 -ResY=1000)
        fi
        exec "$EDITOR" "${launch_args[@]}" "$@"
        ;;
    package-linux)
        require_executable "$UAT"
        require_content
        # UAT forwards these flags to UBT for both editor/game compile steps.
        for argument in "$@"; do
            case "${argument,,}" in
                -ubtargs*|-maxparallelactions*|-maxprocessorcount*) die "Set RONIN_BUILD_JOBS=1..8 instead of overriding compile concurrency." ;;
            esac
        done
        exec "$UAT" BuildCookRun "-project=$PROJECT" -noP4 -platform=Linux -clientconfig=Development -build -cook -map=/Game/Maps/Arena -stage -pak -package -archive "-archivedirectory=$PROJECT_ROOT/Builds/Linux" -unattended -utf8output "-ubtargs=-MaxParallelActions=$JOBS" "$@"
        ;;
esac
