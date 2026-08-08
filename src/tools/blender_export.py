# Blender -> engine exporter. Emits `.skeleton` and `.mesh`; `.animation` and
# `.hitboxes` follow in build-order steps 3 and 4 (see animation_def.md).
#
#   blender <file>.blend --background --python src/tools/blender_export.py -- --out resources/models
#
# This is the ONLY place Blender concepts are translated. Nothing downstream
# knows what a vertex group, a pose bone or a Rigify ORG- bone is.
#
# Grammar of what this writes (the full set is in animation_def.md §2):
#
#   skeleton_file := "skeleton" ident nl "hash" hex nl "bones" int nl { bone_line }
#   bone_line     := "b" int ident int f32{16} nl   // index name parent inverse_bind
#                                                   // parent -1 = root; parent < index
#   mesh_file     := "mesh" ident nl "skeleton" ident hex nl "scale" f32 nl
#                    { material_line } vertex_block index_block { submesh_line }
#   material_line := "mat" int ident path nl
#   vertex_block  := "vertices" int nl { vertex_line }
#   vertex_line   := "v" f32{3} f32{3} f32{2} int{4} f32{4} nl
#                    // position normal uv bone_indices weights (weights sum to 1)
#   index_block   := "indices" int nl { "i" int int int nl }
#   submesh_line  := "sub" int int int int nl       // offset count material
#
# Matrices are written ROW-MAJOR: m[0][0] m[0][1] m[0][2] m[0][3] m[1][0] ...

import bpy
import mathutils
import os
import sys

REQUIRED_VERSION = (5, 1)

# 1 engine unit == 1 inch (player_eye_height 64, gravity 800), so metres * 39.37.
METRES_TO_UNITS = 39.37

# Blender is Z-up right-handed; the engine is Y-up. (x, y, z) -> (x, z, -y),
# matching Blender's own OBJ exporter at Up:Y Forward:-Z -- which is what the
# existing resources/obj assets already are, since asset.cpp applies no flip.
# The 3x3 part is a proper rotation (determinant 1), so normals need no
# transpose-inverse and stay unit length.
AXIS_CONVERSION = mathutils.Matrix(((1, 0, 0, 0),
                                    (0, 0, 1, 0),
                                    (0, -1, 0, 0),
                                    (0, 0, 0, 1)))

MAX_INFLUENCES = 4
MAX_BONES = 128
WEIGHT_LOSS_THRESHOLD = 0.05
DEDUP_QUANTIZE = 1e-5


class ExportError(Exception):
    pass


def log(message):
    print(f"[export] {message}")


def fail(message):
    raise ExportError(message)


# --- Naming -------------------------------------------------------------
#
# Rigify generates several bones per joint from one source name, distinguished
# only by prefix: ORG-shoulder.L, MCH-shoulder.L and DEF-shoulder.L are the
# same joint. The suffix is therefore a stable identity across families, which
# is what makes parent reconstruction below possible.

RIGIFY_PREFIXES = ("ORG-", "MCH-", "DEF-")


def strip_rigify_prefix(name):
    for prefix in RIGIFY_PREFIXES:
        if name.startswith(prefix):
            return name[len(prefix):]
    return name


def engine_bone_name(bone_name):
    # The DEF- prefix is Rigify bookkeeping; the engine should not carry it into
    # mask and hitbox authoring.
    return strip_rigify_prefix(bone_name)


def fnv1a_64(text):
    digest = 0xCBF29CE484222325
    for byte in text.encode("utf-8"):
        digest ^= byte
        digest = (digest * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return digest


# --- Scene selection ----------------------------------------------------


def find_mesh_objects():
    # Rigify emits one WGT- mesh per control widget -- pure viewport UI.
    meshes = [o for o in bpy.data.objects
              if o.type == "MESH" and not o.name.startswith("WGT-")]
    if not meshes:
        fail("no renderable mesh objects (all were WGT- widgets or none exist)")
    return meshes


def find_armature_for(mesh_object):
    # A Rigify file holds TWO armatures, `metarig` and the generated `rig`.
    # Take the one the mesh is actually bound to, never whichever sorts first.
    for modifier in mesh_object.modifiers:
        if modifier.type == "ARMATURE" and modifier.object:
            return modifier.object
    return None


def collect_deform_bones(armature_object):
    bones = [b for b in armature_object.data.bones if b.use_deform]
    if not bones:
        fail(f"armature '{armature_object.name}' has no use_deform bones")
    if len(bones) > MAX_BONES:
        fail(f"{len(bones)} deform bones exceeds MAX_BONES={MAX_BONES}")
    return bones


def reconstruct_parents(deform_bones):
    """Rebuild parent links within the exported subset.

    DEF- bones are parented into the CONTROL tree, not into each other, so
    between limbs the chain runs through ORG-/MCH-/control bones that the
    use_deform filter discards and the chain snaps at the deletion. Walk up the
    full tree and, at each ancestor, also try that ancestor's DEF- twin by name.

    Verified on actual_with_poses.blend: 10 of 35 bones need the remap, and the
    result is single-rooted and parent-before-child. See animation_def.md.
    """
    deform_names = {b.name for b in deform_bones}
    index_of = {b.name: i for i, b in enumerate(deform_bones)}
    parents = []
    remapped = 0

    for bone in deform_bones:
        resolved = None
        ancestor = bone.parent
        while ancestor is not None:
            if ancestor.name in deform_names and ancestor.name != bone.name:
                resolved = ancestor.name
                break
            twin = "DEF-" + strip_rigify_prefix(ancestor.name)
            # A bone must not resolve to itself: DEF-shoulder.L's parent is
            # ORG-shoulder.L, whose twin is DEF-shoulder.L. Skip and keep
            # climbing, or the arm chain collapses into a self-loop.
            if twin in deform_names and twin != bone.name:
                resolved = twin
                remapped += 1
                break
            ancestor = ancestor.parent
        parents.append(index_of[resolved] if resolved else -1)

    root_count = sum(1 for p in parents if p < 0)
    out_of_order = sum(1 for i, p in enumerate(parents) if p >= i)
    log(f"hierarchy: {len(deform_bones)} bones, {remapped} remapped, "
        f"{root_count} root(s), {out_of_order} out of order")
    if root_count != 1:
        fail(f"expected exactly 1 root, got {root_count} -- the DEF- naming is "
             f"irregular; re-parent the deform bones in Blender")
    if out_of_order:
        fail(f"{out_of_order} bones precede their parent; the loader's "
             f"parent-before-child assert would fire")
    return parents


# --- Geometry -----------------------------------------------------------


def to_engine_matrix(matrix):
    """Blender bone-space matrix -> engine space, including scale."""
    scale = mathutils.Matrix.Scale(METRES_TO_UNITS, 4)
    basis = scale @ AXIS_CONVERSION
    return basis @ matrix @ basis.inverted()


def to_engine_position(vector):
    return (AXIS_CONVERSION @ vector) * METRES_TO_UNITS


def to_engine_normal(vector):
    # 3x3 part is a proper rotation, so no transpose-inverse and no scale.
    return (AXIS_CONVERSION.to_3x3() @ vector).normalized()


def gather_influences(vertex, group_index_to_bone_index, statistics):
    """Top MAX_INFLUENCES weights, renormalized. Never drops silently."""
    entries = []
    for element in vertex.groups:
        bone_index = group_index_to_bone_index.get(element.group)
        if bone_index is None or element.weight <= 0.0:
            continue
        entries.append((element.weight, bone_index))
    entries.sort(reverse=True)

    dropped_weight = sum(weight for weight, _ in entries[MAX_INFLUENCES:])
    if dropped_weight > WEIGHT_LOSS_THRESHOLD:
        statistics["vertices_over_cap"] += 1
    statistics["histogram"][len(entries)] = \
        statistics["histogram"].get(len(entries), 0) + 1

    entries = entries[:MAX_INFLUENCES]
    if not entries:
        statistics["unweighted"] += 1
        return [0, 0, 0, 0], [1.0, 0.0, 0.0, 0.0]

    total = sum(weight for weight, _ in entries)
    indices = [0, 0, 0, 0]
    weights = [0.0, 0.0, 0.0, 0.0]
    for slot, (weight, bone_index) in enumerate(entries):
        indices[slot] = bone_index
        weights[slot] = weight / total
    return indices, weights


def build_vertex_buffer(mesh_object, armature_object, group_index_to_bone_index):
    """Deduplicated vertices in bind pose, grouped into submeshes by material.

    Uses the ORIGINAL mesh data, not the evaluated one: the vertex buffer must
    hold the REST shape, and evaluating would apply the Armature modifier and
    bake the current pose into it.
    """
    mesh = mesh_object.data
    non_armature = [m.name for m in mesh_object.modifiers if m.type != "ARMATURE"]
    if non_armature:
        log(f"WARNING '{mesh_object.name}' has non-armature modifiers that are "
            f"NOT applied on export: {non_armature}")

    mesh.calc_loop_triangles()
    if not mesh.uv_layers.active:
        fail(f"mesh '{mesh_object.name}' has no active UV layer")
    uv_data = mesh.uv_layers.active.data

    # Vertices are written in ARMATURE space so the inverse binds, which come
    # from bone.matrix_local (also armature space), compose correctly.
    to_armature_space = (armature_object.matrix_world.inverted()
                         @ mesh_object.matrix_world)
    normal_basis = to_armature_space.to_3x3().inverted().transposed()

    statistics = {"histogram": {}, "vertices_over_cap": 0, "unweighted": 0}
    influence_cache = {}

    vertices = []
    key_to_index = {}
    triangles_by_material = {}

    for triangle in mesh.loop_triangles:
        corner_indices = []
        for loop_index in triangle.loops:
            loop = mesh.loops[loop_index]
            vertex_index = loop.vertex_index

            position = to_engine_position(
                to_armature_space @ mesh.vertices[vertex_index].co)
            normal = to_engine_normal(
                normal_basis @ mesh.corner_normals[loop_index].vector)
            uv = uv_data[loop_index].uv

            if vertex_index not in influence_cache:
                influence_cache[vertex_index] = gather_influences(
                    mesh.vertices[vertex_index], group_index_to_bone_index,
                    statistics)
            bone_indices, bone_weights = influence_cache[vertex_index]

            key = (vertex_index,
                   round(normal.x / DEDUP_QUANTIZE), round(normal.y / DEDUP_QUANTIZE),
                   round(normal.z / DEDUP_QUANTIZE),
                   round(uv[0] / DEDUP_QUANTIZE), round(uv[1] / DEDUP_QUANTIZE))
            index = key_to_index.get(key)
            if index is None:
                index = len(vertices)
                key_to_index[key] = index
                vertices.append((position, normal, (uv[0], uv[1]),
                                 bone_indices, bone_weights))
            corner_indices.append(index)

        triangles_by_material.setdefault(triangle.material_index, []).append(
            corner_indices)

    log(f"influences per vertex: {dict(sorted(statistics['histogram'].items()))}")
    if statistics["vertices_over_cap"]:
        log(f"WARNING {statistics['vertices_over_cap']} vertices lost more than "
            f"{WEIGHT_LOSS_THRESHOLD} total weight at the {MAX_INFLUENCES}-influence cap")
    if statistics["unweighted"]:
        log(f"WARNING {statistics['unweighted']} vertices have no weight to any "
            f"deform bone; they are pinned to bone 0")

    return vertices, triangles_by_material


def find_image(node_tree, seen=None):
    """First Image Texture in the tree, descending into node groups."""
    if node_tree is None:
        return None
    if seen is None:
        seen = set()
    if node_tree.as_pointer() in seen:
        return None
    seen.add(node_tree.as_pointer())
    for node in node_tree.nodes:
        if node.type == "TEX_IMAGE" and node.image:
            return node.image
        if node.type == "GROUP":
            found = find_image(node.node_tree, seen)
            if found:
                return found
    return None


def project_relative(absolute_path):
    """Paths in asset files are project-relative -- never C:\\Users\\... ."""
    try:
        relative = os.path.relpath(absolute_path, os.getcwd())
    except ValueError:      # different drive on Windows
        relative = absolute_path
    if relative.startswith(".."):
        log(f"WARNING {absolute_path} is outside the project; writing an "
            f"absolute path that will not port to another machine")
        return absolute_path.replace("\\", "/")
    return relative.replace("\\", "/")


def sanitize(name):
    cleaned = "".join(c if c.isalnum() or c in "-_" else "_" for c in name)
    # Blender's duplicate suffix (.001) is bookkeeping, not identity.
    while len(cleaned) > 4 and cleaned[-4] == "_" and cleaned[-3:].isdigit():
        cleaned = cleaned[:-4]
    return cleaned or "unnamed"


def material_texture_path(material, output_directory, written_images):
    """Path to this material's texture, writing the pixels out if they are packed.

    Textures imported from another format arrive PACKED: the pixels live inside
    the .blend and `image.filepath` is empty, so they render in Blender but have
    no file for the engine to load. Rather than require a manual
    File > External Data > Unpack, write them here -- the exporter already owns
    translating Blender's world into the engine's.
    """
    # Not `material.use_nodes` -- deprecated, going away in Blender 6.0.
    if material is None:
        return "-"
    image = find_image(material.node_tree)
    if image is None:
        log(f"WARNING material '{material.name}' has no Image Texture node")
        return "-"

    key = image.as_pointer()
    if key in written_images:
        return written_images[key]

    if image.filepath:
        resolved = bpy.path.abspath(image.filepath)
        if os.path.isfile(resolved):
            relative = project_relative(resolved)
            written_images[key] = relative
            return relative
        log(f"WARNING image '{image.name}' points at a missing file: {resolved}")

    if image.size[0] == 0 or image.size[1] == 0:
        log(f"WARNING image '{image.name}' has no pixels; skipping")
        return "-"

    texture_directory = os.path.join(output_directory, "textures")
    os.makedirs(texture_directory, exist_ok=True)
    # Named after the MATERIAL, not the image: packed images arrive with
    # generated names like 'Image_8'.
    filename = f"{sanitize(material.name)}.png"
    absolute = os.path.join(texture_directory, filename)
    previous_format = image.file_format
    image.file_format = "PNG"
    image.save(filepath=absolute)
    image.file_format = previous_format

    relative = project_relative(os.path.abspath(absolute))
    log(f"unpacked '{image.name}' ({image.size[0]}x{image.size[1]}) -> {relative}")
    written_images[key] = relative
    return relative


# --- Writing ------------------------------------------------------------


def format_floats(values, precision=6):
    return " ".join(f"{v:.{precision}f}" for v in values)


def write_skeleton(path, name, bones, parents, skeleton_hash):
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(f"skeleton {name}\n")
        handle.write(f"hash {skeleton_hash:016x}\n")
        handle.write(f"bones {len(bones)}\n")
        for index, bone in enumerate(bones):
            inverse_bind = to_engine_matrix(bone.matrix_local).inverted()
            flat = [inverse_bind[row][column]
                    for row in range(4) for column in range(4)]
            handle.write(f"b {index} {engine_bone_name(bone.name)} "
                         f"{parents[index]} {format_floats(flat)}\n")
    log(f"wrote {path}  ({len(bones)} bones)")


def write_mesh(path, name, skeleton_name, skeleton_hash, mesh_object,
               vertices, triangles_by_material, output_directory,
               written_images):
    mesh = mesh_object.data
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(f"mesh {name}\n")
        handle.write(f"skeleton {skeleton_name} {skeleton_hash:016x}\n")
        handle.write(f"scale {METRES_TO_UNITS:.5f}\n")

        material_slots = list(mesh.materials) or [None]
        for index, material in enumerate(material_slots):
            material_name = material.name if material else "default"
            texture_path = material_texture_path(material, output_directory,
                                                 written_images)
            handle.write(f"mat {index} {sanitize(material_name)} "
                         f"{texture_path}\n")

        handle.write(f"vertices {len(vertices)}\n")
        for position, normal, uv, bone_indices, bone_weights in vertices:
            handle.write("v "
                         f"{format_floats(position)} "
                         f"{format_floats(normal)} "
                         f"{format_floats(uv)} "
                         f"{' '.join(str(i) for i in bone_indices)} "
                         f"{format_floats(bone_weights)}\n")

        total_triangles = sum(len(t) for t in triangles_by_material.values())
        handle.write(f"indices {total_triangles * 3}\n")
        submeshes = []
        emitted = 0
        for material_index in sorted(triangles_by_material):
            triangles = triangles_by_material[material_index]
            for corner_indices in triangles:
                handle.write(f"i {corner_indices[0]} {corner_indices[1]} "
                             f"{corner_indices[2]}\n")
            submeshes.append((emitted * 3, len(triangles) * 3, material_index))
            emitted += len(triangles)

        for index, (offset, count, material_index) in enumerate(submeshes):
            handle.write(f"sub {index} {offset} {count} {material_index}\n")

    log(f"wrote {path}  ({len(vertices)} vertices, {total_triangles} triangles, "
        f"{len(submeshes)} submesh(es))")


# --- Entry point --------------------------------------------------------


def parse_output_directory(argv):
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []
    if "--out" in argv:
        position = argv.index("--out")
        if position + 1 >= len(argv):
            fail("--out given with no directory")
        return argv[position + 1]
    return "resources/models"


def main():
    if bpy.app.version[:2] != REQUIRED_VERSION:
        fail(f"this exporter targets Blender "
             f"{REQUIRED_VERSION[0]}.{REQUIRED_VERSION[1]}, found "
             f"{bpy.app.version_string}")

    output_directory = parse_output_directory(sys.argv)
    os.makedirs(output_directory, exist_ok=True)
    log(f"blender {bpy.app.version_string}, out -> {output_directory}")

    mesh_objects = find_mesh_objects()
    log(f"meshes: {[o.name for o in mesh_objects]}")

    armature_object = None
    for candidate in mesh_objects:
        armature_object = find_armature_for(candidate)
        if armature_object:
            break
    if armature_object is None:
        fail("no mesh has an Armature modifier; nothing to bind against")
    log(f"armature: '{armature_object.name}'")

    deform_bones = collect_deform_bones(armature_object)
    parents = reconstruct_parents(deform_bones)

    skeleton_name = armature_object.name
    ordered_names = [engine_bone_name(b.name) for b in deform_bones]
    skeleton_hash = fnv1a_64("\n".join(ordered_names))
    write_skeleton(os.path.join(output_directory, f"{skeleton_name}.skeleton"),
                   skeleton_name, deform_bones, parents, skeleton_hash)

    bone_index_of_name = {b.name: i for i, b in enumerate(deform_bones)}
    written_images = {}
    for mesh_object in mesh_objects:
        if find_armature_for(mesh_object) is not armature_object:
            log(f"skipping '{mesh_object.name}': not bound to "
                f"'{armature_object.name}'")
            continue

        group_index_to_bone_index = {}
        unmatched = []
        for group in mesh_object.vertex_groups:
            if group.name in bone_index_of_name:
                group_index_to_bone_index[group.index] = \
                    bone_index_of_name[group.name]
            else:
                unmatched.append(group.name)
        if unmatched:
            log(f"WARNING '{mesh_object.name}' has vertex groups with no deform "
                f"bone, their weights are discarded: {unmatched}")

        vertices, triangles_by_material = build_vertex_buffer(
            mesh_object, armature_object, group_index_to_bone_index)
        write_mesh(os.path.join(output_directory, f"{mesh_object.name}.mesh"),
                   mesh_object.name, skeleton_name, skeleton_hash,
                   mesh_object, vertices, triangles_by_material,
                   output_directory, written_images)

    log("done")


try:
    main()
except ExportError as error:
    print(f"[export] ERROR {error}", file=sys.stderr)
    sys.exit(1)
