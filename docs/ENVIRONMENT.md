# Linux development environment

Setup report for **16 October 2026, Asia/Tashkent**. Host compiler prerequisites and NVIDIA/Vulkan access have been verified. Unreal installation, compilation, and gameplay remain unverified.

## Observed host state

| Item | Observation |
| --- | --- |
| OS | Ubuntu 26.04.1 LTS, kernel `7.0.0-34-generic`, glibc 2.43 |
| CPU | Intel Core i7-14650HX, 16 cores / 24 logical CPUs |
| Memory | `free -h` reported 30 GiB usable RAM and 8 GiB swap |
| GPU | NVIDIA GeForce RTX 5060 Laptop GPU; `nvidia-smi` reports 8151 MiB memory |
| NVIDIA driver | 595.91.07, matching Ubuntu `nvidia-driver-595-open` packages |
| Kernel module | Matching prebuilt module for the running kernel; `modinfo` reports Canonical signing |
| Secure Boot | Enabled |
| PRIME profile | `on-demand` |
| Vulkan | Host probe enumerated the NVIDIA discrete GPU with API version 1.4.329 |
| Host packages | `build-essential` and `vulkan-tools` installed and confirmed by `dpkg-query` |
| Utilities | Git, Python 3, make, g++, unzip, zip, curl, wget, vulkaninfo |
| Storage | Initial inspection found approximately 127 GiB available; recheck before extracting the engine |

Hardware and OS observations came from `lsb_release`, `uname`, `ldd`, `lscpu`, `free`, `lspci`, `dpkg`, `modinfo`, `mokutil`, `prime-select`, `lsmod`, sysfs, and `df`. The final GPU checks ran outside the restricted execution sandbox and succeeded:

```sh
nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader
env -u DISPLAY -u WAYLAND_DISPLAY __NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only vulkaninfo --summary
```

The Vulkan output also listed Intel integrated graphics and the llvmpipe CPU renderer. NVIDIA's offload environment variables do not replace checking the device selected in Unreal's own log.

## Graphics correction

Initially `prime-select query` returned `intel`, and the generated `blacklist-nvidia.conf` deliberately disabled the NVIDIA modules. Matching signed modules were already installed. The boot journal did not establish a Secure Boot signature failure. [Canonical's PRIME implementation](https://github.com/canonical/nvidia-prime/blob/master/prime-select) explains the generated blacklist and the hybrid profile.

The setup changed the profile to `on-demand` and loaded `nvidia` and `nvidia_uvm`. Both `nvidia-smi` and the headless Vulkan probe then succeeded on the host without a reboot. A future restart still needs ordinary post-boot validation; do not infer it from this live-session result.

The restricted shell could not access the GPU/display correctly and produced failed NVIDIA/Vulkan probes. Those failures were not evidence that the now-working host driver had stopped. `scripts/check-environment.sh` reports the results available to its own process; use a normal host terminal when device access is restricted.

[Ubuntu's driver guide](https://documentation.ubuntu.com/server/how-to/graphics/install-nvidia-drivers/) recommends packaged signed modules for Secure Boot. No driver purge, DKMS replacement, or disabling Secure Boot was needed. [NVIDIA's 595.91.07 PRIME documentation](https://download.nvidia.com/XFree86/Linux-x86_64/595.91.07/README/primerenderoffload.html) describes the offload variables used by the launcher.

## Unreal acquisition and requirements

The official Epic download selection was **`Linux_Unreal_Engine_5.7.4.zip`**, approximately **29.87 GB compressed**. Download was initiated through the signed-in [Unreal Engine for Linux page](https://www.unrealengine.com/en-US/linux). Completion, archive integrity, expanded size, extraction, and editor execution have not yet been verified in this report.

Budget for the archive and extracted engine together, plus the native toolchain, project output, shader/derived-data caches, and packaged builds. The initial free-space figure does not prove they all fit. Inspect the acquired ZIP's uncompressed total with `zipinfo -t` before extraction.

[Epic's Linux quickstart](https://dev.epicgames.com/documentation/en-us/unreal-engine/linux-development-quickstart-for-unreal-engine) supports this precompiled installed-build route. [Epic's version-specific requirements](https://dev.epicgames.com/documentation/en-us/unreal-engine/linux-development-requirements-for-unreal-engine) map UE 5.7 to native toolchain **v26 / clang 20.1.8**. Inspection of the official 5.7.4 ZIP index and extracted SDK metadata confirmed a bundled native SDK at `Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/v26_clang-20.1.8-rockylinux8/x86_64-unknown-linux-gnu`; this archive has no `SetupToolchain.sh`. The project setup command validates the bundled tools after complete extraction.

Epic recommends Ubuntu 22.04, 32 GB RAM, at least 8 GB graphics memory, and NVIDIA driver 570 or later. The host clears the numerical driver requirement. Ubuntu 26.04 is newer than the documented recommended distribution and still needs a real compile/run check. See [SETUP.md](SETUP.md) for the maintained project commands.

## Verification remaining

1. Complete and validate the engine download, check extraction space, and initialize its matching native toolchain.
2. Compile `RoninEditor` and run the asset bootstrap with the real engine.
3. Launch the arena, confirm NVIDIA selection and 16:10 presentation, and playtest keyboard/mouse and gamepad controls.
4. Test standard controller rumble on hardware and tune combat, audio, and frame time.
5. Package and run the Linux build outside the editor.

Successful host GPU probes do not establish that Unreal or the game runs correctly.
