@echo off
setlocal

echo --- Checking Dependencies ---

:: 1. Check CMake
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo CMake not found.
    set INSTALLED=0
    
    echo Attempting to install via winget...
    winget install Kitware.CMake -e --accept-package-agreements --accept-source-agreements
    if %errorlevel% equ 0 set INSTALLED=1
    
    if %INSTALLED% equ 0 (
        echo Winget failed or not found. Checking for Chocolatey...
        where choco >nul 2>nul
        if %errorlevel% equ 0 (
            echo Attempting to install via choco...
            choco install cmake -y
            if %errorlevel% equ 0 set INSTALLED=1
        ) else (
            echo Chocolatey not found.
        )
    )

    if %INSTALLED% equ 0 (
         echo [ERROR] Failed to install CMake via winget or choco.
         echo Please install it manually from https://cmake.org/download/
         pause
         exit /b 1
    )
    
    echo CMake installed. Please restart this script to update PATH.
    pause
    exit /b 0
) else (
    echo CMake detected.
)

:: 2. Check Vulkan SDK
if "%VULKAN_SDK%"=="" (
    echo Vulkan SDK not found (VULKAN_SDK env var is empty).
    set INSTALLED=0

    echo Attempting to install via winget...
    winget install KhronosGroup.VulkanSDK -e --accept-package-agreements --accept-source-agreements
    if %errorlevel% equ 0 set INSTALLED=1

    if %INSTALLED% equ 0 (
        echo Winget failed or not found. Checking for Chocolatey...
        where choco >nul 2>nul
        if %errorlevel% equ 0 (
            echo Attempting to install via choco...
            choco install vulkan-sdk -y
            if %errorlevel% equ 0 set INSTALLED=1
        ) else (
            echo Chocolatey not found.
        )
    )

    if %INSTALLED% equ 0 (
        echo [ERROR] Failed to install Vulkan SDK. Please install it manually from https://vulkan.lunarg.com/
        pause
        exit /b 1
    )
    echo Vulkan SDK installed. Please restart this script to update environment variables.
    pause
    exit /b 0
) else (
    echo Vulkan SDK detected at %VULKAN_SDK%
)

echo --- Building Project ---
if not exist build mkdir build
cd build

:: 3. Check Ninja
where ninja >nul 2>nul
if %errorlevel% neq 0 (
    echo Ninja not found.
    set INSTALLED=0
    
    echo Attempting to install via winget...
    winget install Ninja-build.Ninja -e --accept-package-agreements --accept-source-agreements
    if %errorlevel% equ 0 set INSTALLED=1
    
    if %INSTALLED% equ 0 (
        echo Winget failed or not found. Checking for Chocolatey...
        where choco >nul 2>nul
        if %errorlevel% equ 0 (
            echo Attempting to install via choco...
            choco install ninja -y
            if %errorlevel% equ 0 set INSTALLED=1
        ) else (
            echo Chocolatey not found.
        )
    )

    if %INSTALLED% equ 0 (
        echo [ERROR] Failed to install Ninja. Please install it manually from https://github.com/ninja-build/ninja/releases
        pause
        exit /b 1
    )
    
    echo Ninja installed. Please restart this script to update PATH.
    pause
    exit /b 0
) else (
    echo Ninja detected.
)

echo --- Building Project ---
if not exist build mkdir build
cd build

:: Use Ninja generator and set Debug build type (Ninja is single-config)
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ..
if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b 1
)

cmake --build .
if %errorlevel% neq 0 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo --- Build Complete ---
echo Run with: .\Debug\MyGame.exe
pause
