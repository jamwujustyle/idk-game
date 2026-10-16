# RONIN verification — 2026-10-16

## Confirmed

- Host: Ubuntu 26.04.1 LTS, Intel Core i7-14650HX, 32 GB RAM, NVIDIA GeForce RTX 5060 Laptop GPU with 8 GB VRAM.
- NVIDIA driver 595.91.07 loaded successfully after selecting PRIME on-demand. Vulkan enumerates the NVIDIA GPU; the project environment check passes.
- Engine-independent combat suite: 224 checks passed. Haptic-envelope suite: 1,494 checks passed.
- USB controller: Microsoft Xbox Series S|X Controller, USB vendor/product `045e:0b12`. Linux exposes `FF_RUMBLE`.
- The compiled Linux driver diagnostic successfully uploaded, played, stopped, and removed a gentle 250 ms rumble effect. The user confirmed feeling the repeated pulse. This verifies the USB/driver motor path; Unreal gameplay feedback remains to be checked.
- Unreal Engine 5.7.4 is installed locally. All 268,659 archive entries were extracted with CRC validation, and the bundled clang/LLD 20.1.8 tools run successfully.
- The `RoninEditor` Linux Development target compiled and linked successfully with Unreal 5.7 build settings.
- The standalone `Ronin` Linux Development target also compiled and linked successfully, producing an x86-64 executable with no compiler warnings or errors.
- Asset bootstrap completed with zero errors and zero warnings, validating the arena map, instanced-mesh material, and five imported sounds.
- All five Unreal integration tests passed with zero errors or warnings: Assets, ClassDefaults, CookConfiguration, InputCoverage, and LiveWorld. The live-world test exercised BeginPlay, possession, character collision and settling, camera setup, and bound combat actions. [Machine-readable results](unreal-tests/run-20261016T080105Z-U0gArY/index.json).

## Pending

The NVIDIA Vulkan SM6 renderer initialized successfully and is compiling its first shader cache. Visual gameplay review, frame-time measurement, gameplay audio/controller feedback, and Linux packaging remain pending.
