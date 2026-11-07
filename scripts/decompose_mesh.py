"""Decomposes a mesh asset into convex pieces and writes each piece's 26-DOP as its .collision sidecar
(geometry_def.md ss11.6).

  python scripts/decompose_mesh.py resources/glb/mvmt.glb [--threshold 0.05] [--cpu]

Run from the project root after building mesh_triangles (pip install -r scripts/decompose_mesh_requirements.txt).
The triangles are the game's own decode, so the pieces are in the asset's space. cuacd decomposes on the GPU;
CoACD on the CPU when cuacd cannot import, finds no GPU, or --cpu asks. Neither is bit reproducible across
machines; the sidecar is committed, never regenerated at build. Each piece is written as a `dop` block: how
near and how far it reaches along each of the 13 directions, in the game's order (directional_bounds.hpp,
DIRECTIONAL_BOUND_DIRECTION_VECTORS), so the loader builds its 26 planes directly and every piece casts a
shadow volume (shadow_volume.hpp caps light-facing planes at 14; a DOP shows a light at most 13).
"""

import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np

DOP_DIRECTIONS = np.array([[1, 0, 0], [0, 1, 0], [0, 0, 1],
                           [1, 1, 0], [1, -1, 0], [1, 0, 1], [1, 0, -1], [0, 1, 1], [0, 1, -1],
                           [1, 1, 1], [1, 1, -1], [1, -1, 1], [1, -1, -1]], dtype=np.double)
DOP_DIRECTIONS /= np.linalg.norm(DOP_DIRECTIONS, axis=1)[:, None]


def read_obj(path):
    vertices, faces = [], []
    with open(path) as file:
        for line in file:
            fields = line.split()
            if fields and fields[0] == "v":
                vertices.append([float(value) for value in fields[1:4]])
            elif fields and fields[0] == "f":
                faces.append([int(value) - 1 for value in fields[1:4]])
    return np.array(vertices, dtype=np.double), np.array(faces, dtype=np.int32)


def compute_dop_extents(points):
    """The nearest and furthest reach of `points` along each direction: 13 pairs, the DOP that bounds them."""
    along = points @ DOP_DIRECTIONS.T
    return np.column_stack([along.min(axis=0), along.max(axis=0)])


def try_decompose_with_cuacd(vertices, faces, threshold):
    try:
        import cuacd
        context = cuacd.Context()
    except Exception as error:
        print(f"decompose_mesh: cuacd unavailable ({error}); falling back to CoACD")
        return None

    # As cuacd's own cli.py: remesh what is not a clean solid, decompose in a unit cube, scale the hulls back.
    try:
        if context.check_mesh(vertices, faces)["needs_remesh"]:
            vertices, faces = context.preprocess(vertices, faces, resolution=64)
        low, high = vertices.min(axis=0), vertices.max(axis=0)
        center = (low + high) / 2
        extent = float((high - low).max())
        scale = extent / 2 if extent > 0 else 1.0
        parts = context.lookahead_decompose(((vertices - center) / scale).astype(np.float32), faces,
                                            width=30, width2=5, depth=2, threshold=threshold,
                                            decompose_components=True, n_concave_edges=16, merge_hulls=True)
        return [hull_points.astype(np.double) * scale + center for _, _, hull_points, _ in parts]
    finally:
        context.close()


def decompose_with_coacd(vertices, faces, threshold):
    import coacd
    coacd.set_log_level("warn")
    parts = coacd.run_coacd(coacd.Mesh(vertices, faces), threshold=threshold, decimate=True, max_ch_vertex=64, seed=0)
    return [points for points, _ in parts]


def write_dop_sidecar(path, hulls):
    with open(path, "w", newline="\n") as file:
        for points in hulls:
            file.write("dop\n")
            for nearest, furthest in compute_dop_extents(points):
                file.write(f"  {nearest:.9g} {furthest:.9g}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mesh", help="project-relative mesh path, as a map names it")
    parser.add_argument("--threshold", type=float, default=0.05, help="concavity threshold, 0.01 to 1")
    parser.add_argument("--cpu", action="store_true", help="CoACD even when cuacd could run")
    parser.add_argument("--tool", default=os.path.join("cmake_build", "bin", "mesh_triangles"))
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory() as directory:
        obj_path = os.path.join(directory, "triangles.obj")
        subprocess.run([arguments.tool, arguments.mesh, obj_path], check=True)
        vertices, faces = read_obj(obj_path)

    hulls = None if arguments.cpu else try_decompose_with_cuacd(vertices, faces, arguments.threshold)
    decomposer = "cuacd"
    if hulls is None:
        hulls = decompose_with_coacd(vertices, faces, arguments.threshold)
        decomposer = "CoACD"
    if not hulls:
        sys.exit(f"decompose_mesh: {decomposer} found no pieces. Nothing written.")

    sidecar = os.path.splitext(arguments.mesh)[0] + ".collision"
    write_dop_sidecar(sidecar, hulls)
    print(f"{sidecar}: {len(hulls)} pieces by {decomposer}, each its 26-DOP")


if __name__ == "__main__":
    main()
