# MyGame (SDL + Vulkan + Protobuf)

A cross-platform C++23 project template.

## Prerequisites

### General
*   **CMake**: 3.20 or newer.
*   **C++ Compiler**: Clang (macOS), MSVC (Windows), or GCC.

### Vulkan
*   Download and install the [Vulkan SDK](https://vulkan.lunarg.com/).
*   Ensure `VULKAN_SDK` environment variable is set (usually handled by the installer).

### Build Instructions

You can build this project using either **Meson** or **CMake**.

#### 1. Meson (Recommended)
This requires `meson` and `ninja` to be installed.

**Configure**:
```bash
meson setup meson_build
```

**Build**:
```bash
meson compile -C meson_build
```

**Run**:
```bash
./meson_build/MyGame
```
(On Windows: `.\meson_build\MyGame.exe` or `.\meson_build\MyGame_Server.exe`)

#### 2. CMake (Alternative)

**Configure**:
```bash
cmake -S . -B cmake_build
```
*First run may take a while as it downloads SDL2 and Protobuf.*

**Build**:
```bash
cmake --build cmake_build
```

**Run**:
```bash
./cmake_build/MyGame
```
(On Windows: `.\cmake_build\Debug\MyGame.exe`)

## Troubleshooting

*   **CMake not found**: Install CMake via Homebrew (`brew install cmake`) or installer.
*   **Vulkan not found**: Reinstall Vulkan SDK. On macOS, ensure `source ~/VulkanSDK/x.y.z/setup-env.sh` is run if not permanently added to shell.
