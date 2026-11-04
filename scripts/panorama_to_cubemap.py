"""Cut an equirectangular panorama (.hdr, .exr, .jpg, .png) into the six face
PNGs load_cubemap reads: resources/cubemaps/<name>/{front,back,up,down,right,left}.png

Faces are cut in THIS ENGINE's axes (asset.cpp load_cubemap): forward +X, up +Y,
right +Z, laid out per face the way samplerCube fetches a texel, so no rebase
is needed in skybox.frag. The panorama's horizontal centre is forward.

    python -I scripts/panorama_to_cubemap.py <panorama> <name> [--size 1024] [--exposure 1.0]
"""

import argparse
import os
import sys

os.environ["OPENCV_IO_ENABLE_OPENEXR"] = "1"
import cv2
import numpy as np


def cube_face_directions(size):
    s = (np.arange(size, dtype=np.float32) + 0.5) / size * 2.0 - 1.0
    sc, tc = np.meshgrid(s, s)
    one = np.ones_like(sc)
    return {
        "front": (one, -tc, -sc),    # +X
        "back": (-one, -tc, sc),     # -X
        "up": (sc, one, tc),         # +Y
        "down": (sc, -one, -tc),     # -Y
        "right": (sc, -tc, one),     # +Z
        "left": (-sc, -tc, -one),    # -Z
    }


def equirect_coordinates(x, y, z, width, height):
    longitude = np.arctan2(z, x)
    latitude = np.arcsin(y / np.sqrt(x * x + y * y + z * z))
    u = (longitude / (2.0 * np.pi) + 0.5) * width - 0.5
    v = (0.5 - latitude / np.pi) * height - 0.5
    return u.astype(np.float32), v.astype(np.float32)


def load_panorama(path):
    image = cv2.imread(path, cv2.IMREAD_UNCHANGED | cv2.IMREAD_COLOR)
    if image is None:
        sys.exit(f"could not read '{path}'")
    if image.dtype == np.uint8:
        image = (image.astype(np.float32) / 255.0) ** 2.2
    elif image.dtype == np.uint16:
        image = (image.astype(np.float32) / 65535.0) ** 2.2
    return image.astype(np.float32)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("panorama")
    parser.add_argument("name")
    parser.add_argument("--size", type=int, default=1024)
    parser.add_argument("--exposure", type=float, default=1.0)
    parser.add_argument("--out_root", default="resources/cubemaps")
    args = parser.parse_args()

    panorama = load_panorama(args.panorama)
    height, width = panorama.shape[:2]
    target_width = args.size * 4
    if width > target_width:
        panorama = cv2.resize(panorama, (target_width, target_width // 2), interpolation=cv2.INTER_AREA)
        height, width = panorama.shape[:2]

    out_dir = os.path.join(args.out_root, args.name)
    os.makedirs(out_dir, exist_ok=True)
    for face, (x, y, z) in cube_face_directions(args.size).items():
        u, v = equirect_coordinates(x, y, z, width, height)
        linear = cv2.remap(panorama, u, v, cv2.INTER_LINEAR, borderMode=cv2.BORDER_WRAP)
        encoded = np.clip(linear * args.exposure, 0.0, 1.0) ** (1.0 / 2.2)
        cv2.imwrite(os.path.join(out_dir, face + ".png"), (encoded * 255.0 + 0.5).astype(np.uint8))
        print(f"wrote {face}.png  max linear {float(linear.max()):.3f}")


if __name__ == "__main__":
    main()
