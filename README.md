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

**Quick Start**:
*   **macOS**: Run `./bootstrap_mac.sh`
*   **Windows**: Run `bootstrap_windows.bat`

**Manual Build**:
1.  **Configure**:
    ```bash
    cmake -S . -B build
    ```
    *First run may take a while as it downloads SDL2 and Protobuf.*

2.  **Build**:
    ```bash
    cmake --build build
    ```

3.  **Run**:
    ```bash
    ./build/MyGame
    ```
    (On Windows: `.\build\Debug\MyGame.exe` or similar)

## Troubleshooting

*   **CMake not found**: Install CMake via Homebrew (`brew install cmake`) or installer.
*   **Vulkan not found**: Reinstall Vulkan SDK. On macOS, ensure `source ~/VulkanSDK/x.y.z/setup-env.sh` is run if not permanently added to shell.
