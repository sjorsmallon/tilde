#!/bin/bash
set -e

echo "--- Checking Dependencies ---"

# 1. Check/Install Homebrew
if ! command -v brew &> /dev/null; then
    echo "Homebrew not found. Installing..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
else
    echo "Homebrew detected."
fi

# 2. Check/Install CMake
if ! command -v cmake &> /dev/null; then
    echo "CMake not found. Installing..."
    brew install cmake
else
    echo "CMake detected."
fi

# 3. Install Vulkan components
# Using individual formulae instead of the cask, as the cask is sometimes missing or requires tapping.
echo "Ensuring Vulkan components are installed..."
brew install vulkan-headers vulkan-loader vulkan-tools vulkan-validationlayers shaderc

# We don't need to manually set VULKAN_SDK env var if we use homebrew's standard paths,
# but we might need to tell CMake where to look if it struggles. 
# Usually find_package(Vulkan) works fine with standard Brew paths.

echo "--- Building Project ---"
rm -rf build
mkdir -p build
cd build

# Pass CMAKE_PREFIX_PATH just in case, though usually automatic
cmake ..
cmake --build .

echo "--- Build Complete ---"
echo "Run with: ./build/MyGame"
