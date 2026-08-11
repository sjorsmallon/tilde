# Checks an exported .mesh against the engine's placement convention.
#
#   python src/tools/check_model.py resources/models/*.mesh
#
# Exit code is 0 if every file passes, 1 otherwise, so this can be a pre-commit
# or CI step later. Reads the text format directly -- no build, no engine.
#
# THE CONVENTION, and why the axes are not checked the same way:
#
#   Y: the origin is at the FEET. Not the centre, not the hips. `player_eye_height`
#      is measured from it, the hitbox table is offset from it, and play_state
#      draws the collision hull entirely ABOVE it. So min.y == 0, tightly -- a
#      model that misses this floats or sinks, and the error is uniform so it
#      never looks like a modelling mistake, only like the floor is wrong.
#
#   X/Z: the body is CENTRED on the origin, loosely. The player rotates about
#      this axis, so an off-centre model orbits its own feet when it turns. The
#      tolerance is wide because the bounding-box centre is a crude proxy: one
#      arm raised legitimately skews it, and tightening this would fail good
#      exports.

import sys


FEET_TOLERANCE = 0.5      # units (1 unit == 1 inch)
CENTRE_TOLERANCE = 4.0


def read_positions(path):
    positions = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("v "):
                fields = line.split()
                positions.append((float(fields[1]), float(fields[2]),
                                  float(fields[3])))
    return positions


def check(path):
    positions = read_positions(path)
    if not positions:
        print(f"{path}: FAIL  no vertices")
        return False

    low = [min(p[axis] for p in positions) for axis in range(3)]
    high = [max(p[axis] for p in positions) for axis in range(3)]
    centre = [(low[axis] + high[axis]) / 2.0 for axis in range(3)]

    print(f"{path}")
    for axis, name in enumerate("xyz"):
        print(f"  {name}: min {low[axis]:8.2f}  max {high[axis]:8.2f}  "
              f"centre {centre[axis]:7.2f}  span {high[axis] - low[axis]:7.2f}")

    failures = []
    if abs(low[1]) > FEET_TOLERANCE:
        failures.append(f"feet are {low[1]:+.2f} off the ground plane "
                        f"(min.y must be 0 +/- {FEET_TOLERANCE})")
    for axis, name in ((0, "x"), (2, "z")):
        if abs(centre[axis]) > CENTRE_TOLERANCE:
            failures.append(f"body is {centre[axis]:+.2f} off centre in {name} "
                            f"(must be 0 +/- {CENTRE_TOLERANCE})")

    # Y up is the whole axis conversion. A standing figure is tall and thin, so
    # if the vertical span is not the dominant one against depth, the Blender
    # Z-up conversion did not happen. Same check model_format_test makes.
    if (high[1] - low[1]) <= 3.0 * (high[2] - low[2]):
        failures.append("vertical span is not much larger than depth -- "
                        "is the Blender Z-up conversion missing?")

    for failure in failures:
        print(f"  FAIL  {failure}")
    if not failures:
        print("  ok")
    return not failures


def main():
    paths = sys.argv[1:]
    if not paths:
        print(__doc__ or "usage: check_model.py <file.mesh> ...")
        return 1
    return 0 if all([check(path) for path in paths]) else 1


if __name__ == "__main__":
    sys.exit(main())
