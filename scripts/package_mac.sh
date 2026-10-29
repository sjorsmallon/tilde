#!/bin/sh
# Stages a self-contained macOS build under dist/ and zips it.
#
# Three things make a build portable that a dev build is not: the asset bytes
# come from one assets.pkg rather than the project tree, every dylib reference
# is rewritten to @loader_path (a dev build hardcodes this machine's build and
# Homebrew paths), and the Vulkan loader plus MoltenVK ride along with an ICD
# manifest pointed at by VK_DRIVER_FILES -- the recipient installs nothing.
#
# MyGame chdirs to three levels above its own path and spells every data path
# from there, so the exe sits two directories deep (bin/macos/) and maps/ and
# assets.pkg sit at the staged root.
set -eu

project_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build_directory="$project_root/cmake_build_ship"
stage_directory="${1:-$project_root/dist/tilde-mac}"
binary_directory="$stage_directory/bin/macos"

echo "==> building (Release, pkg assets) in $build_directory"
cmake -S "$project_root" -B "$build_directory" \
    -DCMAKE_BUILD_TYPE=Release \
    -DTILDE_ASSET_SOURCE=pkg > /dev/null
cmake --build "$build_directory" -j"$(sysctl -n hw.ncpu)" \
    --target MyGame MyGame_Server asset_package

echo "==> staging $stage_directory"
rm -rf "$stage_directory"
mkdir -p "$binary_directory" "$stage_directory/maps"

cp "$build_directory/bin/MyGame" \
   "$build_directory/bin/MyGame_Server" \
   "$build_directory/bin/libgame_client.dylib" \
   "$build_directory/bin/libgame_server.dylib" \
   "$binary_directory/"
cp "$build_directory/assets.pkg" "$stage_directory/"

# resources/ is in the package; maps, their lightmap sidecars and their navmeshes
# are deliberately outside it, so they ship as files. Editor-only inputs (.bak,
# .source_backup) and a local player's run times and ghosts stay behind.
for map_file in "$project_root"/maps/*.source "$project_root"/maps/*.lightmap \
                "$project_root"/maps/*.navmesh; do
    [ -f "$map_file" ] && cp "$map_file" "$stage_directory/maps/"
done
cp -R "$project_root/prefabs" "$stage_directory/prefabs"

echo "==> bundling the Vulkan loader and MoltenVK"
# Taken from what the binary actually links against rather than a guessed
# Homebrew prefix, so this follows the machine that built it.
loader_link=$(otool -L "$binary_directory/MyGame" | awk '/libvulkan/ {print $1; exit}')
loader_path=$(cd -- "$(dirname -- "$loader_link")" && pwd)/$(basename -- "$loader_link")
while [ -L "$loader_path" ]; do
    loader_path=$(cd -- "$(dirname -- "$loader_path")" && pwd)/$(readlink "$loader_path")
done
cp "$loader_path" "$binary_directory/libvulkan.1.dylib"

molten_vk_path=""
for candidate in /opt/homebrew/opt/molten-vk/lib/libMoltenVK.dylib \
                 /usr/local/opt/molten-vk/lib/libMoltenVK.dylib \
                 "$HOME/VulkanSDK"/*/macOS/lib/libMoltenVK.dylib; do
    [ -f "$candidate" ] && molten_vk_path="$candidate" && break
done
if [ -z "$molten_vk_path" ]; then
    echo "package_mac: no libMoltenVK.dylib found (brew install molten-vk)" >&2
    exit 1
fi
cp "$molten_vk_path" "$binary_directory/libMoltenVK.dylib"

# library_path is resolved relative to the manifest's own directory.
mkdir -p "$binary_directory/vulkan/icd.d"
cat > "$binary_directory/vulkan/icd.d/MoltenVK_icd.json" <<'JSON'
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../../libMoltenVK.dylib",
        "api_version": "1.2.0",
        "is_portability_driver": true
    }
}
JSON

echo "==> rewriting dylib references to @loader_path"
chmod u+w "$binary_directory"/*.dylib "$binary_directory"/MyGame*
install_name_tool -id @loader_path/libvulkan.1.dylib "$binary_directory/libvulkan.1.dylib"
install_name_tool -id @loader_path/libMoltenVK.dylib "$binary_directory/libMoltenVK.dylib"
for binary in MyGame MyGame_Server libgame_client.dylib libgame_server.dylib; do
    for reference in $(otool -L "$binary_directory/$binary" | awk '/^\t\/(opt|usr\/local)\// {print $1}'); do
        install_name_tool -change "$reference" "@loader_path/$(basename -- "$reference")" \
            "$binary_directory/$binary"
    done
    for reference in $(otool -L "$binary_directory/$binary" | awk '/^\t@rpath\// {print $1}'); do
        install_name_tool -change "$reference" "@loader_path/$(basename -- "$reference")" \
            "$binary_directory/$binary"
    done
    # The build-tree rpath is this machine's absolute path and means nothing
    # elsewhere; @loader_path above replaces what it used to answer for.
    for stale_rpath in $(otool -l "$binary_directory/$binary" |
                         awk '/LC_RPATH/ {found = 1} found && /^ *path /{print $2; found = 0}'); do
        install_name_tool -delete_rpath "$stale_rpath" "$binary_directory/$binary"
    done
done

# install_name_tool invalidates the signature, and an arm64 binary with a broken
# one will not start at all. Ad-hoc is enough to launch; see the note in the
# launcher about Gatekeeper on the receiving machine.
for binary in libvulkan.1.dylib libMoltenVK.dylib libgame_client.dylib libgame_server.dylib \
              MyGame MyGame_Server; do
    codesign --force --sign - --timestamp=none "$binary_directory/$binary" 2>/dev/null
done

cat > "$stage_directory/Tilde.command" <<'LAUNCHER'
#!/bin/sh
set -eu
here=$(cd -- "$(dirname -- "$0")" && pwd)
# The loader only searches system directories for a driver manifest, so the
# bundled MoltenVK has to be named explicitly.
VK_DRIVER_FILES="$here/bin/macos/vulkan/icd.d/MoltenVK_icd.json"
VK_ICD_FILENAMES="$VK_DRIVER_FILES"
export VK_DRIVER_FILES VK_ICD_FILENAMES
cd "$here"
exec "$here/bin/macos/MyGame" "$@"
LAUNCHER
chmod +x "$stage_directory/Tilde.command"

# The dedicated server does not chdir the way MyGame does, so its launcher is
# what puts it at the payload root.
cat > "$stage_directory/Tilde_Server.command" <<'SERVER_LAUNCHER'
#!/bin/sh
set -eu
cd -- "$(cd -- "$(dirname -- "$0")" && pwd)"
exec ./bin/macos/MyGame_Server "$@"
SERVER_LAUNCHER
chmod +x "$stage_directory/Tilde_Server.command"

echo "==> checking nothing outside the payload is still referenced"
leftover=$(find "$binary_directory" -type f \( -name 'MyGame*' -o -name '*.dylib' \) \
           -exec otool -L {} \; | awk '/^\t\/(opt|usr\/local)\//' || true)
if [ -n "$leftover" ]; then
    echo "package_mac: unbundled references remain:" >&2
    echo "$leftover" >&2
    exit 1
fi

archive_path="$stage_directory.zip"
rm -f "$archive_path"
(cd -- "$(dirname -- "$stage_directory")" && \
 ditto -c -k --sequesterRsrc --keepParent "$(basename -- "$stage_directory")" "$archive_path")

echo
echo "staged: $stage_directory"
echo "zipped: $archive_path"
echo
echo "Tell the recipient: unzip, then run"
echo "    xattr -dr com.apple.quarantine <unzipped folder>"
echo "once (the build is ad-hoc signed, not notarized), then open Tilde.command."
