"""Decomposes a mesh asset into convex hulls and writes them as its .collision sidecar (geometry_def.md ss11.6).

  python scripts/decompose_mesh.py resources/glb/mvmt.glb [--threshold 0.05] [--cpu]

Run from the project root after building mesh_triangles (pip install -r scripts/decompose_mesh_requirements.txt).
The triangles are the game's own decode, so the hulls are in the asset's space. cuacd decomposes on the GPU;
CoACD on the CPU when cuacd cannot import, finds no GPU, or --cpu asks. Neither is bit reproducible across
machines; the sidecar is committed, never regenerated at build.
"""

import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np

MAX_SIDECAR_HULL_POINTS = 64  # map_geometry_asset.hpp; the loader refuses a block past it


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


def cap_hull_points(points, cap):
    """Quickhull stopped at `cap` vertices: the extremes along the 26 directions, then the vertex farthest
    outside the hull so far, until `cap`. A subset of the hull's own vertices, so it lies inside the original."""
    from scipy.spatial import ConvexHull
    if len(points) <= cap:
        return points
    directions = np.array([[x, y, z] for x in (-1, 0, 1) for y in (-1, 0, 1) for z in (-1, 0, 1) if (x, y, z) != (0, 0, 0)])
    kept = list(np.unique(np.argmax((points - points.mean(axis=0)) @ directions.T, axis=0)))
    while len(kept) < cap:
        planes = ConvexHull(points[kept]).equations
        outside = (points @ planes[:, :3].T + planes[:, 3]).max(axis=1)
        farthest = int(np.argmax(outside))
        if outside[farthest] <= 1e-6:
            break
        kept.append(farthest)
    return points[kept]


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
    parts = coacd.run_coacd(coacd.Mesh(vertices, faces), threshold=threshold, decimate=True,
                            max_ch_vertex=MAX_SIDECAR_HULL_POINTS, seed=0)
    return [points for points, _ in parts]


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
    else:
        hulls = [cap_hull_points(points, MAX_SIDECAR_HULL_POINTS) for points in hulls]

    for index, points in enumerate(hulls):
        if not 4 <= len(points) <= MAX_SIDECAR_HULL_POINTS:
            sys.exit(f"decompose_mesh: {decomposer} hull {index + 1} has {len(points)} points; the loader takes four "
                     f"to {MAX_SIDECAR_HULL_POINTS}. Nothing written.")

    sidecar = os.path.splitext(arguments.mesh)[0] + ".collision"
    with open(sidecar, "w", newline="\n") as file:
        for points in hulls:
            file.write("hull\n")
            for x, y, z in points:
                file.write(f"  {x:.9g} {y:.9g} {z:.9g}\n")

    print(f"{sidecar}: {len(hulls)} hulls by {decomposer}, at most {max(len(points) for points in hulls)} points each")


if __name__ == "__main__":
    main()
