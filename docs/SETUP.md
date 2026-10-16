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

Uncooked editor-game launches set `r.ShaderCompiler.JobCacheDDC=0` at startup to compile complete material shader maps. The first rendered launch can spend several minutes compiling shaders; subsequent launches reuse the compiled shader cache. Allow that work to finish before judging missing materials or startup performance. Visual validation is tracked separately in the [verification report](../reports/2026-10-16-verification.md).

`play` launches fullscreen at 1920 × 1200 by default, matching a 16:10 display. Explicit `-windowed`/`-fullscreen`, `-ResX=...`, and `-ResY=...` replace their corresponding defaults; each resolution dimension can be supplied independently. Additional arguments are passed as distinct arguments to Unreal, including quoted paths or console commands:

```sh
./scripts/unreal.sh play -windowed -ResX=1920 -ResY=1200
./scripts/unreal.sh editor -NoSplash
```

Unreal can clamp the requested resolution to display bounds. For an offscreen capture that requires an exact size, pass `-RenderOffscreen -ForceRes` alongside the resolution arguments.

Development builds also expose `ronin.Capture /absolute/output.png`. It waits for
the game viewport and HUD, then five seconds without pending shader jobs,
captures the rendered viewport, checks the new output file, and exits. It fails
after a 300-second deadline by default; an optional second argument sets a
10–1800 second deadline for first-use compilation. Capture runs cap rendering at
30 fps while waiting. The opening duel gate keeps combat idle during capture.

```sh
./scripts/unreal.sh play -windowed -RenderOffscreen -ForceRes \
  -ResX=1920 -ResY=1200 \
  "-ExecCmds=ronin.Capture $PWD/reports/startup.png"
```

Prefix the console command with `showhud,` for a scene-only capture. The capture
command is excluded from Shipping and Test builds.

Interactive commands check NVIDIA/Vulkan first and set NVIDIA's offload environment variables. Verify the GPU selected in Unreal's log after the first launch. Both commands require the generated assets; neither silently rebuilds code. Find the controls in the [README](../README.md#controls).

The `editor` command defaults SDL's window backend to `x11`, using XWayland on a Wayland desktop. Epic documents native Wayland menu/input issues in [UE 5.7's SDL3 integration](https://dev.epicgames.com/documentation/en-us/unreal-engine/updating-unreal-engine-on-linux-to-sdl3). This applies to both terminal and app-menu launches, without changing the desktop session. An explicitly supplied `SDL_VIDEO_DRIVER` (or legacy `SDL_VIDEODRIVER`) takes precedence. Confirm `Using SDL video driver 'x11'` in the editor log when diagnosing menus.

The embedded Fab browser uses software rendering through `r.CEFGPUAcceleration=0` in `Config/DefaultEngine.ini`. This leaves the arena's Vulkan rendering enabled. Restart the editor after changing this startup setting; its log should report `CEF GPU acceleration disabled`.

Linux controller ownership is configured in `Config/DefaultInput.ini`, under
`[InputPlatformSettings_Linux InputPlatformSettings]`. The integer setting
`input.DeviceMappingPolicy=1` selects Unreal's
`PrimaryUserSharesKeyboardAndFirstGamepad` policy: keyboard, mouse, and the first
gamepad share the primary player. Keep this explicit so device connection changes
do not rely on a missing platform default. Restart the editor or game after
changing this setting because Unreal caches the policy within the running process.

To add **RONIN** to Ubuntu's applications search, run this explicitly as your normal user:

```sh
./scripts/install-launcher.sh
```

It creates `~/.local/share/applications/ronin.desktop`, launching this checkout fullscreen without a terminal window. It records `UE_ROOT` if set when installed; otherwise the launcher uses `.tools/unreal`. The checkout and engine must remain available. Rerun the installer after moving either location. It does not install Unreal or rebuild the game.

To add **Unreal Editor — RONIN** as a separate applications entry:

```sh
./scripts/install-launcher.sh --editor
```

Press **Super**, type **Unreal**, and select the editor entry. This shortcut opens the project with a 30 FPS cap, reduced viewport shadows/post-processing, 50% viewport resolution, and on-demand shader compilation. These settings keep asset browsing lighter and apply to that editor process; the game launcher retains its own settings. The entry is saved as `~/.local/share/applications/ronin-editor.desktop`. In the editor, use **Window → Fab → My Library** to import assets acquired through Fab.

## 5. Package and verify

```sh
./scripts/unreal.sh package-linux
```

This invokes `RunUAT.sh BuildCookRun` for a Linux Development build, cooks `/Game/Maps/Arena`, stages and packages it, and archives output under ignored `Builds/Linux`. It passes the same 1–8 compile-action limit to UnrealBuildTool. Packaging requires a successful editor build and bootstrap first.

For this command, UE 5.7 writes the launcher directly to `Builds/Linux/Ronin.sh`; it starts `Ronin/Binaries/Linux/Ronin` inside that folder. Launch it independently of the editor:

```sh
./Builds/Linux/Ronin.sh -fullscreen -ResX=1920 -ResY=1200
```

The [packaged run guide](LINUX-PACKAGE.md) covers runtime requirements and NVIDIA hybrid graphics. After packaging, an optional portable archive can be created with `./scripts/archive-linux.sh`. This adds a README containing the run guide and the control table from the project README, preserves executable modes, and writes `Builds/Ronin-Linux-x86_64.tar.gz` plus its SHA-256 checksum. It verifies that launcher, executable, and cooked content exist; it does not build or launch Unreal. Repeating the command replaces the archive. Source development and editor-game play do not require packaging.

Verification is complete only after the packaged executable launches and the core interaction works: movement/camera, light/heavy attacks, guarding/parrying, dodging, stance switching, execution, pause/restart, and quality/frame-limit controls. Check audio, HUD visibility, GPU selection, and frame time on the actual laptop. Record failures rather than treating source checks or a successful package command as a playtest.

For the engine-side workflow, see [Epic's Linux quickstart](https://dev.epicgames.com/documentation/en-us/unreal-engine/linux-development-quickstart-for-unreal-engine), [Python commandlet guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/scripting-the-unreal-editor-using-python), and [build configuration reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/build-configuration-for-unreal-engine).
