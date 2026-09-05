# Packs a material folder's ao.png, roughness.png and metallic.png (or
# metal.png) into one orm.png, and deletes the three it consumed.
#
#   python src/tools/orm_pack.py resources/textures/harsh_bricks [...]
#
# The CHANNEL ORDER IS glTF's -- R occlusion, G roughness, B metallic -- and it
# is the one thing here that must not be guessed at: nothing downstream can tell
# a swapped roughness and metallic from a material that was authored wrong.
#
# This is a ONE-TIME conversion tool, in the shape of map_convert: the folders
# on disk predate the packed convention and had to be brought to it, and a DCC
# exports orm.png directly. It is Python rather than a CMake target for the same
# reason blender_export.py is -- it touches content, never the build.
#
# The three sources are grayscale that happens to be stored as RGB (and, in one
# case, as 16-bit), so each one contributes its RED channel after an 8-bit
# convert. A source whose channels DISAGREE is not grayscale and is refused
# rather than silently reduced.

import os
import sys

from PIL import Image, ImageChops

# Each role names the spellings a DCC export is seen to use, first match wins.
CHANNEL_SOURCES = [
    (("ao.png",), "occlusion"),
    (("roughness.png",), "roughness"),
    (("metallic.png", "metal.png"), "metallic"),
]


def find_source(folder, filenames):
    for filename in filenames:
        path = os.path.join(folder, filename)
        if os.path.exists(path):
            return path
    return None


def load_single_channel(path):
    image = Image.open(path)

    if image.mode in ("I", "I;16", "I;16B", "I;16L"):
        image = image.point(lambda value: value * (1.0 / 256.0)).convert("L")
    elif image.mode in ("RGB", "RGBA"):
        red, green, blue = image.convert("RGB").split()
        for other in (green, blue):
            difference = ImageChops.difference(red, other)
            if difference.getbbox() is not None:
                raise ValueError("%s is not grayscale -- its channels disagree" % path)
        image = red
    else:
        image = image.convert("L")

    return image


def pack_folder(folder):
    output_path = os.path.join(folder, "orm.png")
    if os.path.exists(output_path):
        print("%s: orm.png already exists, nothing to do" % folder)
        return True

    channels = []
    consumed = []
    for filenames, role in CHANNEL_SOURCES:
        path = find_source(folder, filenames)
        if path is None:
            print("%s: no %s to take %s from" % (folder, " or ".join(filenames), role))
            return False
        try:
            channels.append(load_single_channel(path))
        except ValueError as error:
            print("%s: %s" % (folder, error))
            return False
        consumed.append(path)

    sizes = {channel.size for channel in channels}
    if len(sizes) != 1:
        print("%s: the three maps differ in size (%s)" % (folder, sorted(sizes)))
        return False

    Image.merge("RGB", channels).save(output_path, optimize=True)
    for path in consumed:
        os.remove(path)

    print("%s: wrote orm.png %s, removed the three it packed" % (folder, channels[0].size))
    return True


def main(folders):
    if not folders:
        print(__doc__ or "usage: orm_pack.py <material folder> [...]")
        return 1
    return 0 if all([pack_folder(folder) for folder in folders]) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
