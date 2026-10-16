#!/usr/bin/env bash
# Read-only checks. Missing prerequisites are reported; nothing is installed.
set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
require_gpu=false
case "${1:-}" in
    --require-gpu) require_gpu=true ;;
    '') ;;
    *) printf 'Usage: scripts/check-environment.sh [--require-gpu]\n' >&2; exit 2 ;;
esac
missing=0
report() { printf '%-12s %s\n' "$1" "$2"; }
report OS "$(uname -srm)"
if command -v free >/dev/null; then free -h; fi
df -h -- "$PROJECT_ROOT"

engine_candidate="${UE_ROOT:-$PROJECT_ROOT/.tools/unreal}"
if [[ -x "$engine_candidate/Engine/Binaries/Linux/UnrealEditor" ]]; then
    report Engine "$engine_candidate"
else
    report Engine 'MISSING — set UE_ROOT or create .tools/unreal (see docs/SETUP.md)'
    if ! "$require_gpu"; then missing=1; fi
fi
for tool in python3 git make g++; do
    if command -v "$tool" >/dev/null; then
        report "$tool" "$(command -v "$tool")"
    else
        report "$tool" 'not found in PATH'
    fi
done
if command -v prime-select >/dev/null; then report PRIME "$(prime-select query 2>/dev/null || true)"; fi

gpu_ok=true
if command -v nvidia-smi >/dev/null && gpu_output="$(nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader 2>&1)"; then
    report NVIDIA "$gpu_output"
else
    report NVIDIA 'UNAVAILABLE — probe failed; check driver/device access and docs/ENVIRONMENT.md'
    if [[ -n "${gpu_output:-}" ]]; then printf '%s\n' "$gpu_output" >&2; fi
    gpu_ok=false
fi
if command -v vulkaninfo >/dev/null; then
    # Inspect devices without a display-surface probe; an isolated shell may lack X access.
    # Match a device name, not the NVIDIA layer list (which exists without a usable GPU).
    if vulkan_output="$(env -u DISPLAY -u WAYLAND_DISPLAY __NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only vulkaninfo --summary 2>&1)" && [[ "$vulkan_output" =~ deviceName[[:blank:]]*=[[:blank:]]*(NVIDIA|GeForce|Quadro) ]]; then
        report Vulkan 'NVIDIA device enumerated'
    else
        report Vulkan 'FAILED — NVIDIA Vulkan device not enumerated'
        printf '%s\n' "$vulkan_output" >&2
        gpu_ok=false
    fi
else
    report Vulkan 'MISSING — install vulkan-tools for verification'
    gpu_ok=false
fi
if ! "$gpu_ok"; then
    missing=1
    if "$require_gpu"; then
        printf 'SAMURAI: graphics launch blocked until NVIDIA/Vulkan checks pass. Build and bootstrap work without a GPU.\n' >&2
        printf 'If this is a restricted shell, repeat the probe in a normal host terminal with GPU device access.\n' >&2
    fi
fi
exit "$missing"
