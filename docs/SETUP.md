# Build and run on Linux

The project targets the official **Unreal Engine 5.7 Linux installed build**. The 5.7.4 editor build, asset bootstrap, and all five engine integration tests passed on 2026-10-16; see the [dated verification report](../reports/2026-10-16-verification.md) and [Unreal test results](../reports/unreal-tests/run-20261016T080105Z-U0gArY/index.json). Rendered gameplay and a packaged Linux build still require validation. Run commands from the repository root unless an engine directory is explicitly specified.

## 1. Prepare the workstation

The [environment report](ENVIRONMENT.md) records the laptop's verified host prerequisites and NVIDIA setup. Run a fresh read-only check:

```sh
./scripts/check-environment.sh
```

The check exits nonzero if the engine or NVIDIA/Vulkan setup is unavailable to its process. It does not install packages or change graphics settings. A restricted execution sandbox can deny GPU access even when the host driver works; repeat a failed probe in a normal host terminal before changing the driver. On a new workstation, the ordinary host packages can be installed with:

```sh
sudo apt update
sudo apt install build-essential vulkan-tools
```

Use the engine's matching native toolchain for Unreal C++; a distribution compiler is not a replacement. On this hybrid NVIDIA laptop, a working `nvidia-smi` and NVIDIA enumeration from `vulkaninfo --summary` are required before an interactive launch. Build and asset bootstrap can run without an active GPU.

## 2. Install and select Unreal Engine

Download the Linux ZIP from [Epic's Linux page](https://www.unrealengine.com/en-US/linux) while signed into your Epic account. Select the stable 5.7 release, and inspect its archive size and available disk space before downloading/extracting. Allow room for the ZIP and extracted engine together, the toolchain, shader caches, and project build output.

Extract it into a dedicated engine directory outside the repository. Select that directory in the shell:

```sh
export UE_ROOT="/absolute/path/to/UnrealEngine"
```

`UE_ROOT` must contain `Engine/Binaries/Linux/UnrealEditor`. Alternatively, create the local ignored link `.tools/unreal`:

```sh
mkdir -p .tools
ln -s "/absolute/path/to/UnrealEngine" .tools/unreal
```

Use either method; `UE_ROOT` takes precedence. The scripts reject missing/non-executable engine launchers and do not download an engine implicitly.

Validate the engine's native C++ toolchain:

```sh
./scripts/unreal.sh setup
```

The official Linux 5.7.4 ZIP bundles its native SDK and has no `SetupToolchain.sh`. The command reads `Engine/Config/Linux/Linux_SDK.json`, finds the matching x86_64 SDK under `Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64`, and checks that clang, clang++, and the linker execute. UnrealBuildTool discovers that same SDK automatically. Complete the ZIP extraction first: the SDK and runtime files occur near the archive's end.

For a source tree without a bundled SDK, the command falls back to `Engine/Build/BatchFiles/Linux/SetupToolchain.sh` if present; that fallback may download dependencies. [Epic's requirements](https://dev.epicgames.com/documentation/en-us/unreal-engine/linux-development-requirements-for-unreal-engine) map UE 5.7 to v26 / clang 20.1.8. Setup is explicit; ordinary build/play commands do not repeat it.

## 3. Build code and bootstrap assets

```sh
./scripts/unreal.sh build
./scripts/unreal.sh bootstrap
```

`build` compiles the `RoninEditor` Linux Development target. It defaults to at most eight parallel compile actions; reduce memory pressure with `RONIN_BUILD_JOBS=4 ./scripts/unreal.sh build`. Valid values are 1–8.

`bootstrap` first generates five deterministic mono 48 kHz/16-bit PCM WAVs in ignored `.cache/audio`, using only Python's standard library. It then runs the project's Python script in `UnrealEditor-Cmd` with `-run=pythonscript`, `-nullrhi`, and `-unattended`. The script creates:

- `/Game/Maps/Arena`: a blank persistent level; runtime code builds the playable scene.
- `/Game/Materials/M_Combat`: opaque material with `Color`, `Roughness`, and `Metallic` parameters and instanced-static-mesh shader support for the courtyard.
- `/Game/Audio/Slash`, `Clash`, `Hit`, `Dodge`, and `Execute`: imported sound waves.

The Python Script Plugin is enabled for Editor targets in `Ronin.uproject`. Bootstrap explicitly loads the Bink decoder module so sound assets can initialize without an audio output device. It preserves existing assets, including a map you have edited, and validates the material's parameters and the imported sound types. It enables instanced-static-mesh usage on an existing combat material if missing, preserving its other edits. To regenerate a particular imported asset after modifying a generator, remove that asset deliberately in Unreal Editor, then rerun bootstrap. Generation of WAV source files itself is repeatable and overwrites `.cache/audio/*.wav`.

A completion marker and on-disk asset checks guard against a commandlet returning without completing the script. Check `Saved/Logs/Ronin.log` if bootstrap fails. Run `./scripts/test-unreal.sh` after bootstrap to verify the generated assets and live gameplay world with NullRHI; see [VERIFICATION.md](VERIFICATION.md) for its scope and the separate live acceptance pass.

## 4. Play or edit

```sh
./scripts/unreal.sh play
./scripts/unreal.sh editor
```

`play` launches a 1600 × 1000 window by default, matching a 16:10 display. Explicit `-windowed`/`-fullscreen`, `-ResX=...`, and `-ResY=...` replace their corresponding defaults; each resolution dimension can be supplied independently. Additional arguments are passed as distinct arguments to Unreal, including quoted paths or console commands:

```sh
./scripts/unreal.sh play -windowed -ResX=1920 -ResY=1200
./scripts/unreal.sh editor -NoSplash
```

Unreal can clamp the requested resolution to display bounds. For an offscreen capture that requires an exact size, pass `-RenderOffscreen -ForceRes` alongside the resolution arguments.

Interactive commands check NVIDIA/Vulkan first and set NVIDIA's offload environment variables. Verify the GPU selected in Unreal's log after the first launch. Both commands require the generated assets; neither silently rebuilds code. Find the controls in the [README](../README.md#controls).

## 5. Package and verify

```sh
./scripts/unreal.sh package-linux
```

This invokes `RunUAT.sh BuildCookRun` for a Linux Development build, cooks `/Game/Maps/Arena`, stages and packages it, and archives output under ignored `Builds/Linux`. It passes the same 1–8 compile-action limit to UnrealBuildTool. Packaging requires a successful editor build and bootstrap first.

Verification is complete only after the packaged executable launches and the core interaction works: movement/camera, light/heavy attacks, guarding/parrying, dodging, stance switching, execution, pause/restart, and quality/frame-limit controls. Check audio, HUD visibility, GPU selection, and frame time on the actual laptop. Record failures rather than treating source checks or a successful package command as a playtest.

For the engine-side workflow, see [Epic's Linux quickstart](https://dev.epicgames.com/documentation/en-us/unreal-engine/linux-development-quickstart-for-unreal-engine), [Python commandlet guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/scripting-the-unreal-editor-using-python), and [build configuration reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/build-configuration-for-unreal-engine).
