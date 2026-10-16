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
- The environment replacement compiled and imported 33 CC0 assets (46,358,934 bytes), including scanned rocks, fir meshes, PBR surfaces, and an HDR sky. Import verification checked source hashes, centimeter bounds, original triangle counts, UVs, material slots, and color-space settings. All five integration tests passed again with zero test warnings/errors after this change. [Environment integration results](unreal-tests/run-20261016T085141Z-D3CmYZ/index.json). GPU appearance is not yet verified.
- The editor app-menu shortcut is installed. Native Wayland produced a fatal display-protocol disconnect when opening menus, leaving invisible editor processes alive. The launcher now defaults the editor to X11/XWayland; startup logs confirmed the backend, and the user reached Fab and initiated the Kwang import.
- With embedded-browser GPU acceleration disabled, the Kwang retry completed: Fab reported `ProcessSuccess: TRUE`, `ErrorCode: OK`, and 1,602 constructed files, with 2,446,401,665 downloaded bytes. The user confirmed the resulting Content Browser folder. This establishes a successful import under the workaround, not the root cause of the prior GPU failure.
- The native Kwang character integration compiled with two compiler workers. All six integration tests passed with no test warnings or errors, including skeleton/clip compatibility, attack-phase mapping, actual weapon-bone movement, startup combat gating, pause and contact hit-stop, death playback, and rematch resets. [Character integration results](unreal-tests/run-20261016T091829Z-cWAt3j/index.json). These are headless engine checks, not a visual-quality assessment.

## Pending

The NVIDIA Vulkan SM6 renderer initialized successfully and rendered the initial primitive arena. Its first offscreen capture exposed checkerboard fallback materials and undersized HUD text. HUD text was enlarged and full material compilation enabled. The user rejected the primitive visual quality. The replacement environment, skeletal fighters, adapted guard/evasive-step animations, and simplified HUD still need a captured GPU review. Character and environment integration do not establish AAA visual quality.

The first Kwang download attempt was interrupted by `VK_ERROR_DEVICE_LOST` on the XWayland editor run. The successful retry used CEF browser GPU acceleration disabled. The editor-menu workaround and this later GPU failure are distinct issues; stability beyond the completed import remains to be assessed.

The inherited Development-build F2/F3/F4 view-mode bindings were removed to prevent conflicts with the game's settings controls. Static comparison against Unreal's defaults confirmed the exact removals and preservation of F9 screenshots. The portable Linux archive helper passed mocked package, executable-mode, controls, checksum, and invalid-input checks; a real package has not been produced.

Interactive visual review, representative frame-time measurement, gameplay audio/controller feedback, and Linux packaging remain pending. The earlier 600-frame capture includes first-use compilation and screenshot stalls and is not a valid performance benchmark.
