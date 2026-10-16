# RONIN for Linux

This package runs without Unreal Editor or a development toolchain. It requires Linux x86_64, a working Vulkan graphics driver, and a GPU supporting Unreal's Vulkan SM6 renderer. Keyboard and mouse are supported; an Xbox-compatible controller is optional.

Extract the archive before launching. Keep `Ronin.sh`, `Ronin/`, and `Engine/` together. From the extracted directory:

```sh
./Ronin.sh -fullscreen -ResX=1920 -ResY=1200
```

On a laptop with NVIDIA hybrid graphics, select the discrete GPU with:

```sh
__NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only ./Ronin.sh -fullscreen -ResX=1920 -ResY=1200
```

Use `-windowed` instead of `-fullscreen` for windowed play. Press Escape or Start/Menu to pause; change quality while paused with F2 or A. Press H or X for the in-game controls. Standard controller rumble is supported, with intensity cycling on F4 or D-pad down.

The game is a prototype. A successful package or archive command does not establish rendering performance or a hardware playtest.
