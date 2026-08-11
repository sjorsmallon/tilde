# Verifies an exported .mesh against the .blend it came from.
#
#   blender <file>.blend --background --python src/tools/verify_export.py -- --out resources/models
#
# Run it from the project root, same as blender_export.py, and run it AFTER an
# export -- it compares the files on disk against the scene now, so a stale pair
# reports as a mismatch (which is itself worth knowing).
#
# WHY THIS IS NOT A ctest: it needs Blender, and the test suite must run without
# it. `model_format_test` guards everything checkable from the file alone -- that
# submeshes tile the index buffer, that weights normalise, that bone indices
# exist, that the model is Y-up at engine scale. What it CANNOT check is whether
# the file agrees with the .blend, because it has never seen the .blend. That
# gap is this script, and it is the gap where a plausible-but-wrong export
# lives: every submesh well-formed, every invariant satisfied, and the hands
# textured with the face.
#
# WHAT IT PROVES. For each material it compares triangle count, mean corner
# position and mean UV between Blender and the file. Comparing over the INDEX
# BUFFER (corners with repetition) rather than unique vertices is what makes the
# two sides comparable at all: the exporter dedups vertices, but each corner
# still appears once in the index buffer, so the multiset being averaged is
# identical on both sides and the means must match to float precision. Crossed
# submeshes, a dropped UV layer, a wrong axis conversion or a lost material slot
# all move at least one of those three numbers.

import bpy
import mathutils
import os
import sys

METRES_TO_UNITS = 39.37
POSITION_TOLERANCE = 0.01      # units; the means are over thousands of corners
UV_TOLERANCE = 1e-4

AXIS_CONVERSION = mathutils.Matrix(((1, 0, 0, 0),
                                    (0, 0, 1, 0),
                                    (0, -1, 0, 0),
                                    (0, 0, 0, 1)))


def log(message):
    print(f"[verify] {message}")


def to_engine_position(vector):
    return (AXIS_CONVERSION @ vector) * METRES_TO_UNITS


def parse_output_directory(argv):
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []
    if "--out" in argv:
        position = argv.index("--out")
        if position + 1 >= len(argv):
            print("[verify] ERROR --out given with no directory", file=sys.stderr)
            sys.exit(1)
        return argv[position + 1]
    return "resources/models"


def read_mesh_file(path):
    """The .mesh as flat arrays. Deliberately a separate, dumber reader than the
    C++ one: if both had the same bug they would agree and prove nothing."""
    positions, uvs, indices, submeshes, materials = [], [], [], [], []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            fields = line.split()
            if not fields or fields[0].startswith("//"):
                continue
            if fields[0] == "mat":
                materials.append((int(fields[1]), fields[2], fields[3]))
            elif fields[0] == "v":
                positions.append((float(fields[1]), float(fields[2]), float(fields[3])))
                uvs.append((float(fields[7]), float(fields[8])))
            elif fields[0] == "i":
                indices.extend(int(x) for x in fields[1:4])
            elif fields[0] == "sub":
                submeshes.append((int(fields[1]), int(fields[2]), int(fields[3]),
                                  int(fields[4])))
    return positions, uvs, indices, submeshes, materials


def blender_truth(mesh_object):
    """Per material index: corner count and the running sums the means come from."""
    mesh = mesh_object.data
    if mesh.is_editmode:
        return None
    mesh.calc_loop_triangles()
    if not mesh.uv_layers.active or len(mesh.uv_layers.active.data) != len(mesh.loops):
        return None
    uv_data = mesh.uv_layers.active.data

    buckets = {}
    for triangle in mesh.loop_triangles:
        bucket = buckets.setdefault(triangle.material_index,
                                    {"corners": 0, "position": [0.0, 0.0, 0.0],
                                     "uv": [0.0, 0.0]})
        for loop_index in triangle.loops:
            loop = mesh.loops[loop_index]
            position = to_engine_position(mesh.vertices[loop.vertex_index].co)
            uv = uv_data[loop_index].uv
            bucket["corners"] += 1
            for axis in range(3):
                bucket["position"][axis] += position[axis]
            bucket["uv"][0] += uv[0]
            bucket["uv"][1] += uv[1]
    return buckets


def verify(mesh_object, output_directory):
    path = os.path.join(output_directory, f"{mesh_object.name}.mesh")
    if not os.path.isfile(path):
        log(f"FAIL '{mesh_object.name}': no exported file at '{path}' -- export first")
        return False

    truth = blender_truth(mesh_object)
    if truth is None:
        log(f"FAIL '{mesh_object.name}': saved in Edit Mode, or its UV layer is "
            f"empty -- there is nothing trustworthy to compare against")
        return False

    positions, uvs, indices, submeshes, materials = read_mesh_file(path)
    log(f"'{mesh_object.name}' -> {path}")

    ok = True
    covered = 0
    for _, offset, count, material_index in submeshes:
        corners = indices[offset:offset + count]

        if offset != covered:
            log(f"  FAIL submesh at material {material_index} starts at {offset}, "
                f"expected {covered} -- submeshes must tile the index buffer")
            ok = False
        covered += count

        bucket = truth.get(material_index)
        if bucket is None:
            log(f"  FAIL material {material_index} has triangles in the file but "
                f"none in Blender")
            ok = False
            continue
        if bucket["corners"] != len(corners):
            log(f"  FAIL material {material_index}: {bucket['corners'] // 3} "
                f"triangles in Blender, {len(corners) // 3} in the file")
            ok = False
            continue

        total = float(len(corners))
        file_position = [0.0, 0.0, 0.0]
        file_uv = [0.0, 0.0]
        for corner in corners:
            for axis in range(3):
                file_position[axis] += positions[corner][axis]
            file_uv[0] += uvs[corner][0]
            file_uv[1] += uvs[corner][1]

        position_delta = max(abs(bucket["position"][axis] / total - file_position[axis] / total)
                             for axis in range(3))
        uv_delta = max(abs(bucket["uv"][axis] / total - file_uv[axis] / total)
                       for axis in range(2))
        texture = next((m[2] for m in materials if m[0] == material_index), "?")

        failed = position_delta > POSITION_TOLERANCE or uv_delta > UV_TOLERANCE
        ok = ok and not failed
        log(f"  {'FAIL' if failed else 'ok  '} material {material_index}: "
            f"{len(corners) // 3:>5} triangles, centroid delta {position_delta:.6f}, "
            f"mean-uv delta {uv_delta:.8f}, texture {texture}")

    if covered != len(indices):
        log(f"  FAIL submeshes cover {covered} of {len(indices)} indices")
        ok = False

    # Materials in Blender that produced no submesh at all. A slot with no
    # polygons assigned is legitimate, so this is a note, not a failure -- but it
    # is the shape of "the texture you painted never reached the model".
    for material_index in sorted(truth):
        if not any(s[3] == material_index for s in submeshes):
            log(f"  NOTE material {material_index} has triangles in Blender but no "
                f"submesh in the file")
            ok = False

    return ok


def main():
    output_directory = parse_output_directory(sys.argv)
    mesh_objects = [o for o in bpy.data.objects
                    if o.type == "MESH" and not o.name.startswith("WGT-")]
    if not mesh_objects:
        log("ERROR no mesh objects in this .blend")
        return 1

    results = [verify(mesh_object, output_directory) for mesh_object in mesh_objects]
    if all(results):
        log("PASS every submesh carries its own material's triangles, positions and UVs")
        return 0
    log("FAIL the exported file does not agree with this .blend")
    return 1


sys.exit(main())
