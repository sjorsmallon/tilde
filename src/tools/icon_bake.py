# Strokes the editor's hand-authored entity glyphs into resources/icons/*.png.
#
#   python src/tools/icon_bake.py
#
# This is a ONE-TIME conversion tool, in the shape of map_convert and
# orm_pack.py: the glyphs below were the polyline data in entity_icons.cpp, an
# icon is a PNG now, and this is where those points went so they are not lost.
# A new icon is DRAWN, not added here.
#
# The art is WHITE WITH AN ALPHA CHANNEL because the icon pass tints it with the
# light's own colour -- a coloured PNG can only be multiplied, which would lose
# exactly the thing the tint exists to say. Coverage is baked by drawing at 4x
# and resampling down, since the uploader generates no mips.
#
# Each icon's square is its own strokes' extent plus half a stroke width, so
# nothing is clipped at the edge and every icon fills the 44px square the pass
# draws it in.

import os

from PIL import Image, ImageDraw

OUTPUT_SIZE = 64
SUPERSAMPLE = 4

# 1.75px of stroke at the 44px the pass draws an icon at, expressed in the pixels
# of the file it is being baked into.
ICON_PIXEL_SIZE = 44.0
ICON_THICKNESS = 1.75

# -- The glyphs, in the 680x460 y-down canvas they were authored in ------------

BULB_GLASS = [
    (340, 60), (392, 72), (434, 104), (458, 150), (458, 206), (434, 254),
    (406, 290), (390, 340), (290, 340), (274, 290), (246, 254), (222, 206),
    (222, 150), (246, 104), (288, 72),
]

BULB = [
    (BULB_GLASS, True),
    ([(290, 340), (390, 340)], False),
    ([(296, 358), (384, 358)], False),
    ([(300, 376), (380, 376)], False),
    ([(306, 394), (374, 394)], False),
    ([(314, 412), (366, 412)], False),
    ([(322, 428), (358, 428)], False),
    ([(314, 340), (314, 214)], False),
    ([(366, 340), (366, 214)], False),
    ([(314, 214), (324, 196), (334, 214), (344, 196), (354, 214), (366, 196)], False),
    ([(340, 24), (340, 8)], False),
    ([(256, 46), (246, 32)], False),
    ([(424, 46), (434, 32)], False),
    ([(198, 108), (182, 100)], False),
    ([(482, 108), (498, 100)], False),
    ([(186, 184), (170, 184)], False),
    ([(494, 184), (510, 184)], False),
]

SPOT = [
    ([(340, 30), (340, 80)], False),
    ([(250, 80), (430, 80), (490, 210), (190, 210)], True),
    ([(230, 250), (170, 410)], False),
    ([(340, 255), (340, 420)], False),
    ([(450, 250), (510, 410)], False),
]

SUN_DISC = [
    (430.0, 230.0), (423.2, 264.4), (403.6, 293.6), (374.4, 313.2),
    (340.0, 320.0), (305.6, 313.2), (276.4, 293.6), (256.8, 264.4),
    (250.0, 230.0), (256.8, 195.6), (276.4, 166.4), (305.6, 146.8),
    (340.0, 140.0), (374.4, 146.8), (403.6, 166.4), (423.2, 195.6),
]

SUN = [
    (SUN_DISC, True),
    ([(460.0, 230.0), (540.0, 230.0)], False),
    ([(424.9, 314.9), (481.4, 371.4)], False),
    ([(340.0, 350.0), (340.0, 430.0)], False),
    ([(255.1, 314.9), (198.6, 371.4)], False),
    ([(220.0, 230.0), (140.0, 230.0)], False),
    ([(255.1, 145.1), (198.6, 88.6)], False),
    ([(340.0, 110.0), (340.0, 30.0)], False),
    ([(424.9, 145.1), (481.4, 88.6)], False),
]

CANVAS_CENTER = (340.0, 230.0)

# game_rules is its own file holding the sun for now, exactly as its icon was the
# sun's strokes: two icons that happen to look alike, not one icon named twice.
ICONS = {
    "point_light": (BULB, 420.0),
    "spot_light": (SPOT, 400.0),
    "directional_light": (SUN, 400.0),
    "game_rules": (SUN, 400.0),
}


def bake(polylines, authored_height):
    resolution = OUTPUT_SIZE * SUPERSAMPLE

    # In canvas units, so the stroke ends up the same weight it was stroked at.
    stroke = ICON_THICKNESS * authored_height / ICON_PIXEL_SIZE

    reach = 0.0
    for points, _closed in polylines:
        for x, y in points:
            reach = max(reach, abs(x - CANVAS_CENTER[0]), abs(y - CANVAS_CENTER[1]))
    side = 2.0 * (reach + 0.5 * stroke)

    scale = resolution / side
    width = max(1, round(stroke * scale))

    def to_image(point):
        return (resolution * 0.5 + (point[0] - CANVAS_CENTER[0]) * scale,
                resolution * 0.5 + (point[1] - CANVAS_CENTER[1]) * scale)

    coverage = Image.new("L", (resolution, resolution), 0)
    draw = ImageDraw.Draw(coverage)
    for points, closed in polylines:
        path = [to_image(point) for point in points]
        if closed:
            path.append(path[0])
        draw.line(path, fill=255, width=width, joint="curve")

        # PIL's line caps are square; a disc at every vertex is what rounds the
        # joins the "curve" joint does not cover and the two free ends.
        radius = width * 0.5
        for x, y in path:
            draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=255)

    coverage = coverage.resize((OUTPUT_SIZE, OUTPUT_SIZE), Image.LANCZOS)

    icon = Image.new("RGBA", (OUTPUT_SIZE, OUTPUT_SIZE), (255, 255, 255, 0))
    icon.putalpha(coverage)
    return icon


def main():
    directory = os.path.join("resources", "icons")
    os.makedirs(directory, exist_ok=True)

    for name, (polylines, authored_height) in ICONS.items():
        path = os.path.join(directory, name + ".png")
        bake(polylines, authored_height).save(path)
        print("wrote " + path)


if __name__ == "__main__":
    main()
