# Blender -> engine exporter. Emits `.skeleton`, `.mesh` and `.animation`;
# `.hitboxes` follows in build-order step 4 (see animation_def.md).
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
#   animation_file:= "animation" ident nl "skeleton" ident hex nl "bones" int nl
#                    "fps" f32 nl "frames" int nl [ "stride" f32 nl ] { frame_block }
#   frame_block   := "f" int nl { channel_line }
#   channel_line  := "b" int f32{3} f32{4} f32{3} nl
#                    // bone translation rotation(x y z w) scale
#
# Matrices are written ROW-MAJOR: m[0][0] m[0][1] m[0][2] m[0][3] m[1][0] ...
# A pose in `.animation` is TRS, not a matrix -- it is the form that BLENDS.

import bpy
import glob
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

# Where Blender's pose library wrote its one-.blend-per-pose files. In the repo
# rather than in the user asset library, which was outside version control and
# invisible to a build machine.
DEFAULT_POSE_DIRECTORY = "resources/blender/asset_library"

# A pose has no cycle, so it has no frame rate either. Written anyway because the
# format has one field for both cases and the reader refuses a zero.
POSE_FPS = 30.0

# A pose bone is "off rest" if its basis differs from identity by more than this.
LOOSE_POSE_EPSILON = 1e-5


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


def to_engine_uv(uv):
    """Blender UV -> engine UV. The V axis flips; U does not.

    Blender's UV origin is BOTTOM-left with V increasing upward. The engine
    samples textures loaded by stb_image, which hands back the top row of the
    PNG first, and nothing calls stbi_set_flip_vertically_on_load -- so the
    sampler's V=0 is the TOP of the image and V increases downward. The two
    conventions are mirror images and something has to reconcile them.

    It happens HERE for the same reason the axis conversion does: this script is
    the only place Blender conventions get translated, and a flip in the shader
    or at load time would have to be repeated by every future consumer of a UV.

    This is not the axis conversion, despite arriving with it -- a UV has no
    third component to rotate, and AXIS_CONVERSION never touched it. It went
    unnoticed until the first thing sampled a texture with mesh UVs, because the
    lit shader bound inUV and ignored it, and the displacement path generates
    worldspace UVs of its own.
    """
    return (uv[0], 1.0 - uv[1])


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

    # An ACTIVE layer is not a POPULATED one. Saving in Edit Mode leaves the
    # layer present with zero entries (see the check in main), and indexing it
    # then dies with an IndexError forty lines from anything that explains why.
    if len(uv_data) != len(mesh.loops):
        fail(f"mesh '{mesh_object.name}' UV layer "
             f"'{mesh.uv_layers.active.name}' has {len(uv_data)} entries for "
             f"{len(mesh.loops)} loops -- the UVs are missing or out of sync")

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
            uv = to_engine_uv(uv_data[loop_index].uv)

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


class TextureWriter:
    """Copies every material's texture into `<out>/textures/` and remembers what
    it wrote.

    Two caches, for two different questions. `by_image` makes a repeated lookup
    for the same image a cache hit rather than a second file write -- two
    materials sharing one image share one output file, which is correct. Its key
    is the image POINTER, because two distinct images can carry one name.
    `by_filename` is the collision check below.
    """

    def __init__(self, output_directory):
        self.output_directory = output_directory
        self.by_image = {}     # image pointer -> project-relative path
        self.by_filename = {}  # filename -> (image pointer, material name)


def material_texture_path(material, textures):
    """Path to this material's texture, ALWAYS copied into `<out>/textures/`.

    Copying is unconditional, and that is the whole point. Blender images come
    two ways -- PACKED (pixels inside the .blend, `image.filepath` empty) or
    linked to a file on disk -- and this used to branch on which:

        packed -> copy to <out>/textures/<material>.png, reference the copy
        linked -> reference the file WHERE IT SITS

    That second branch is what wrote `resources/blender/textures/Image_8.png`
    into a shipped .mesh. Two things wrong with it. A runtime asset must not
    point into the Blender source tree -- that tree is authoring data, and a
    packaged build should not need it to render. And `Image_8` is a Blender
    datablock name, not an asset name.

    Worse than either: WHICH branch you got depended on an incidental property
    of the .blend, so the same model exported from two files produced two
    different path regimes, silently. Copying always makes the output a function
    of the material, full stop.

    The output name is DERIVED from the material rather than validated against
    the image's name -- same rule as mesh_asset ids from filenames. Once the
    name is derived the input name stops mattering, and asking the author to
    pre-match it is asking them to do the tool's job. Deriving creates exactly
    one new failure mode, which is the collision below.
    """
    # Not `material.use_nodes` -- deprecated, going away in Blender 6.0.
    if material is None:
        return "-"
    image = find_image(material.node_tree)
    if image is None:
        # A flat-colour material is legitimate. The engine draws "-" with the
        # material's diffuse colour and no texture; a material that names a
        # texture which then fails to load is the loud case, and it is loud in
        # the ENGINE (magenta/black checkerboard), not here.
        log(f"WARNING material '{material.name}' has no Image Texture node")
        return "-"

    key = image.as_pointer()
    if key in textures.by_image:
        return textures.by_image[key]

    if image.size[0] == 0 or image.size[1] == 0:
        # Reachable now that linked files are copied rather than referenced: a
        # broken link means Blender holds the datablock with no pixels behind it.
        source = bpy.path.abspath(image.filepath) if image.filepath else "<packed>"
        log(f"WARNING image '{image.name}' has no pixels (source: {source}); "
            f"material '{material.name}' gets no texture")
        return "-"

    filename = f"{sanitize(material.name)}.png"

    # The collision deriving the name creates, and the reason it is an ERROR
    # rather than a rename: `leet_hands` and `leet_hands.001` both sanitize to
    # `leet_hands`, so one image would silently overwrite the other and a
    # submesh would render wearing the wrong skin. There is no correct
    # derivation here -- only the author knows which material was meant -- which
    # is the same test def_gen applies to "names must be unique within a class".
    previous = textures.by_filename.get(filename)
    if previous is not None:
        fail(f"materials '{previous[1]}' and '{material.name}' both resolve to "
             f"textures/{filename} with DIFFERENT images; one would silently "
             f"overwrite the other. Rename one material in Blender -- note that "
             f"a .001 suffix is stripped, so 'x' and 'x.001' collide")
    textures.by_filename[filename] = (key, material.name)

    texture_directory = os.path.join(textures.output_directory, "textures")
    os.makedirs(texture_directory, exist_ok=True)
    absolute = os.path.join(texture_directory, filename)
    previous_format = image.file_format
    image.file_format = "PNG"
    image.save(filepath=absolute)
    image.file_format = previous_format

    relative = project_relative(os.path.abspath(absolute))
    log(f"copied '{image.name}' ({image.size[0]}x{image.size[1]}) -> {relative}")
    textures.by_image[key] = relative
    return relative


# --- Poses --------------------------------------------------------------
#
# Blender's pose library writes one .blend per pose, each holding a single
# Action with frame_range == (1, 1). They are appended into the scene that holds
# the rig, assigned, evaluated, and read back as local TRS -- exactly the path a
# real clip will take, which is why they are exported as single-frame
# `.animation` files and there is deliberately no `.pose` format.


def find_pose_files(pose_directory):
    # Absolute, because bpy.data.libraries.load resolves a relative path against
    # the BLEND's directory rather than the working directory -- which silently
    # became C:\resources\... the first time this was tried.
    #
    # NOT recursive, and that is the contract: the pose set is the files the
    # author put in this directory, nothing else. Blender's asset browser saves
    # into `Saved/Actions/` underneath it, so a recursive glob swept up every
    # pose ever marked as an asset -- including the four source poses of the
    # `Death` CLIP, which would then have exported as four extra `.animation`
    # files and, being near-rest, tripped verify_poses_differ and failed an
    # export that had nothing wrong with it.
    pattern = os.path.join(pose_directory, "*.blend")
    files = [os.path.abspath(p) for p in sorted(glob.glob(pattern))]

    # Named rather than ignored: a pose the author saved into a subdirectory is
    # invisible in the output either way, and silence is how they would go
    # looking for it in the exporter.
    nested = sorted(glob.glob(os.path.join(pose_directory, "*", "**", "*.blend"),
                              recursive=True))
    if nested:
        log(f"ignoring {len(nested)} .blend file(s) below '{pose_directory}'; "
            f"poses are the files directly IN it: "
            f"{[os.path.basename(p) for p in nested[:6]]}"
            f"{' ...' if len(nested) > 6 else ''}")
    return files


def check_pose_position(armature_object):
    """Rest Position makes every pose export as the bind pose.

    This is the first export that reads `pose_bone.matrix`, and in Rest Position
    that accessor silently returns the REST matrix. The result is a well-formed
    file full of plausible numbers in which every pose is identical, which is
    about the worst failure mode an asset pipeline has.
    """
    if armature_object.data.pose_position != "POSE":
        fail(f"armature '{armature_object.name}' is in "
             f"{armature_object.data.pose_position} position. pose_bone.matrix "
             f"returns the REST matrix there, so every pose would export as the "
             f"bind pose. Set the armature to Pose Position in Blender")


def warn_on_loose_pose(armature_object):
    """Pose bones moved off rest with no Action driving them.

    Such a bone contaminates every pose that does not key it -- and unlike a
    keyed value it is invisible in the exported file, because the number it
    produces is perfectly well-formed. Named here so the .blend gets fixed;
    `reset_pose_to_rest` below is what stops it changing the OUTPUT in the
    meantime, so the export is reproducible either way.
    """
    animation_data = armature_object.animation_data
    if animation_data and animation_data.action:
        return  # an Action is driving the rig; nothing is loose by definition

    loose = []
    for pose_bone in armature_object.pose.bones:
        basis = pose_bone.matrix_basis
        if any(abs(basis[row][column] - (1.0 if row == column else 0.0)) > LOOSE_POSE_EPSILON
               for row in range(4) for column in range(4)):
            loose.append(pose_bone.name)

    if loose:
        log(f"WARNING '{armature_object.name}' has {len(loose)} pose bones moved "
            f"off rest with no Action assigned: {loose[:8]}"
            f"{' ...' if len(loose) > 8 else ''}. They are reset before each pose "
            f"is read, so this export is reproducible -- but fix the .blend, "
            f"because in Blender they are still what you are looking at")


def reset_pose_to_rest(armature_object):
    """Back to rest, which means UNASSIGNING the action as well as zeroing the
    bones -- rest is "nothing is driving this rig", and clearing only half of it
    does not survive the next depsgraph evaluation.

    Zeroing alone was enough while poses were the only thing exported, because
    every pose reassigns an action immediately afterwards. It stopped being
    enough the moment a clip ran first: `export_clips` left `Death` assigned, so
    the pose path's rest baseline re-evaluated straight back into Death's last
    frame and `verify_poses_differ` was comparing every pose against that
    instead of against rest -- still a check, but of the wrong thing.
    """
    if armature_object.animation_data:
        armature_object.animation_data.action = None
    for pose_bone in armature_object.pose.bones:
        pose_bone.matrix_basis.identity()


def prepare_pose_evaluation(armature_object):
    """Everything that must be true before ANY pose is read back.

    Shared by the clip and pose paths rather than done by each: both read
    `pose_bone.matrix`, and every trap below produces a well-formed file full of
    plausible numbers instead of an error. Call it once, after the skeleton and
    meshes are written -- `enter_pose_mode` invalidates the `Bone` datablocks
    those need (see read_pose_locals).
    """
    check_pose_position(armature_object)
    warn_on_loose_pose(armature_object)
    enter_pose_mode(armature_object)


def append_pose_actions(path):
    """Append every Action out of a pose .blend. Returns the new datablocks."""
    with bpy.data.libraries.load(path, link=False) as (source, destination):
        destination.actions = list(source.actions)
    return [a for a in destination.actions if a is not None]


def enter_pose_mode(armature_object):
    """Make the rig active and enter Pose mode. NOT optional, and not cosmetic.

    Headless, an armature's POSE IS NEVER EVALUATED while the rig sits in Object
    mode. Verified on 5.1: set `pose_bone.location = (0, 0, 0.5)`, then
    `update_tag()`, `view_layer.update()`, `frame_set()` and
    `depsgraph.update()` in every combination -- `pose_bone.matrix` does not
    move, on the original OR on `evaluated_get()`, even though `matrix_basis`
    plainly carries the change and the rig is in the depsgraph. Toggling through
    Pose mode once is what makes the recompute happen.

    The failure this causes is the reason it is worth this many lines: assigning
    the five aim actions in turn produced five .animation files that differed
    only in their name line, every one of them a well-formed file full of
    plausible numbers. Same shape as the Rest Position trap, through a different
    door -- so `verify_poses_differ` below refuses to let it ship silently a
    second time.
    """
    bpy.context.view_layer.objects.active = armature_object
    try:
        bpy.ops.object.mode_set(mode="POSE")
    except RuntimeError as error:
        fail(f"could not enter Pose mode on '{armature_object.name}' ({error}); "
             f"without it the pose never evaluates and every pose exports "
             f"identical")


def apply_action(armature_object, action, frame):
    """Assign an Action and evaluate the rig at `frame`.

    Assigning `animation_data.action` is NOT sufficient in Blender 5.x: actions
    are slotted, and without `action_slot` nothing evaluates -- the rig stays in
    whatever pose it was already in.
    """
    if armature_object.animation_data is None:
        armature_object.animation_data_create()

    armature_object.animation_data.action = action
    if not action.slots:
        fail(f"action '{action.name}' has no slots; nothing would evaluate")
    armature_object.animation_data.action_slot = action.slots[0]

    bpy.context.scene.frame_set(int(frame))


def read_pose_locals(armature_object, deform_bone_names, parents):
    """Evaluated pose -> parent-relative TRS per exported bone, in engine space.

    Takes NAMES rather than the `Bone` objects the rest of the exporter passes
    around, and that is not a style choice: entering Pose mode rebuilds the
    armature's bone datablocks, so a `Bone` collected beforehand is a dangling
    pointer afterwards. It does not fail as one -- reading `.name` off it comes
    back as a UnicodeDecodeError on whatever now occupies that memory. A name is
    the stable identity across every one of these operations, which is already
    the position `skeleton_hash` takes.

    Two more things here are not obvious.

    `pose_bone.matrix` is ARMATURE-object space, and blending needs
    parent-relative, so each bone is divided by its parent. The parent is the
    RECONSTRUCTED one (`parents`), not Blender's -- the DEF- bones are parented
    into the control tree, and using Blender's parent would make a local
    transform relative to a bone that is not in the file.

    The engine conversion applies to the LOCAL matrix rather than to each
    armature-space matrix, which is the same result for one fewer conversion:
    E(inv(P) @ M) == inv(E(P)) @ E(M), because E is a conjugation.
    """
    armature_space = [armature_object.pose.bones[name].matrix for name in deform_bone_names]

    locals_out = []
    for index in range(len(deform_bone_names)):
        parent = parents[index]
        local = (armature_space[index] if parent < 0
                 else armature_space[parent].inverted() @ armature_space[index])
        translation, rotation, scale = to_engine_matrix(local).decompose()
        # mathutils orders a quaternion w x y z; the file and the engine order it
        # x y z w. Getting this wrong is a rig that looks almost right.
        locals_out.append((translation, (rotation.x, rotation.y, rotation.z, rotation.w), scale))
    return locals_out


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
               vertices, triangles_by_material, textures):
    mesh = mesh_object.data
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(f"mesh {name}\n")
        handle.write(f"skeleton {skeleton_name} {skeleton_hash:016x}\n")
        handle.write(f"scale {METRES_TO_UNITS:.5f}\n")

        material_slots = list(mesh.materials) or [None]
        for index, material in enumerate(material_slots):
            material_name = material.name if material else "default"
            texture_path = material_texture_path(material, textures)
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

    # The material binding, spelled out. It is carried entirely by INDEX --
    # Blender's polygon material_index buckets the triangles, each bucket becomes
    # one contiguous index range, and the `mat` lines are written in slot order
    # so mat index == slot index. No name matching anywhere in that chain, which
    # is why a messy datablock name cannot affect correctness. Logging it means
    # the binding is legible in every export rather than only when somebody
    # thinks to go checking.
    for index, (offset, count, material_index) in enumerate(submeshes):
        material = material_slots[material_index] if material_index < len(material_slots) else None
        material_name = material.name if material else "default"
        log(f"  submesh {index}: material {material_index} '{material_name}' "
            f"-> {count // 3} triangles, indices [{offset}, {offset + count}), "
            f"texture {material_texture_path(material, textures)}")

    # A vertex whose UV differs across a seam MUST split into two, or the texture
    # smears across the seam. The dedup key is (vertex_index, normal, uv), so the
    # gap between these two numbers is that splitting having happened.
    log(f"  {len(mesh.vertices)} mesh vertices -> {len(vertices)} exported "
        f"(split by UV seam and normal)")


def write_animation(path, name, skeleton_name, skeleton_hash, fps, frames,
                    stride_distance=None):
    """frames is a list of per-bone (translation, (x,y,z,w), scale) lists."""
    bone_count = len(frames[0])
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(f"animation {name}\n")
        handle.write(f"skeleton {skeleton_name} {skeleton_hash:016x}\n")
        handle.write(f"bones {bone_count}\n")
        handle.write(f"fps {fps:.5f}\n")
        handle.write(f"frames {len(frames)}\n")
        if stride_distance is not None:
            handle.write(f"stride {stride_distance:.6f}\n")
        for frame_index, channels in enumerate(frames):
            handle.write(f"f {frame_index}\n")
            for bone_index, (translation, rotation, scale) in enumerate(channels):
                handle.write(f"b {bone_index} {format_floats(translation)} "
                             f"{format_floats(rotation)} {format_floats(scale)}\n")
    log(f"wrote {path}  ({len(frames)} frame(s), {bone_count} bones)")


def channels_equal(left, right, epsilon=1e-4):
    for (lt, lr, ls), (rt, rr, rs) in zip(left, right):
        for a, b in zip(list(lt) + list(lr) + list(ls), list(rt) + list(rr) + list(rs)):
            if abs(a - b) > epsilon:
                return False
    return True


def frames_equal(left, right):
    return (len(left) == len(right)
            and all(channels_equal(a, b) for a, b in zip(left, right)))


# --- Clips --------------------------------------------------------------
#
# A clip is an Action that lives in the .blend BEING EXPORTED, sampled at every
# integer frame of its range. Poses come from separate .blend files, so the two
# are disjoint by SOURCE and no flag has to distinguish them -- the clip list is
# taken before any pose file is appended, and appending is the only thing that
# adds an action.
#
# Two things about this are worth stating, because both look like choices and
# neither is.
#
# EVERY frame is sampled, not just the keyed ones. Blender interpolates between
# keys with bezier handles; the engine lerps linearly between file frames
# (sample_animation_clip_at). Writing only the keys would replay as straight
# lines between them -- a visibly different curve from the authored one. The
# file is a fixed-rate resampling of Blender's curve, which is exactly the shape
# the engine's sampler expects.
#
# The read is `pose_bone.matrix`, not the fcurves. On a Rigify rig an action
# keys the CONTROL bones and the DEF- bones follow through constraints:
# `Death` keys 72 bones, of which zero are DEF-. Reading the curves would export
# a clip in which nothing moves.


def scene_frame_rate():
    render = bpy.context.scene.render
    return render.fps / render.fps_base


def collect_stashed_actions(armature_object):
    """Actions Blender is holding onto, as opposed to actions the author wrote.

    Unlinking an action does not drop it: Blender STASHES it into a MUTED NLA
    track called `[Action Stash]` so the work is not lost. actual_with_poses.blend
    holds `Death` assigned and its stashed predecessor `rigAction` beside it,
    byte-identical -- so exporting every action in the file writes a junk clip
    that nobody would ever notice was junk.

    Filtered on the same terms as `WGT-` widget meshes and `.001` name suffixes:
    it is bookkeeping, and the exporter drops bookkeeping without asking. But on
    the RELATIONSHIP rather than on the name -- an action is stashed when
    something MUTED refers to it and nothing live does. Matching `<object>Action`
    instead would be a guess, and would eat a clip that happened to be named that.

    An action with no NLA strip at all is NOT stashed and is exported. That is
    the ordinary way a file keeps several clips, and it must keep working.
    """
    animation_data = armature_object.animation_data
    if animation_data is None:
        return set()

    live = set()
    muted = set()
    if animation_data.action:
        live.add(animation_data.action.name)
    for track in animation_data.nla_tracks:
        for strip in track.strips:
            if strip.action is None:
                continue
            destination = muted if (track.mute or strip.mute) else live
            destination.add(strip.action.name)
    return muted - live


def read_clip_frames(armature_object, action, deform_bone_names, parents):
    first, last = (int(round(v)) for v in action.frame_range)
    if last < first:
        fail(f"action '{action.name}' has an inverted frame range "
             f"{first}..{last}")

    # Reset FIRST, same reason as a pose: bones the action does not key would
    # otherwise hold whatever the previous action left behind. Once, not per
    # frame -- the action re-evaluates its own channels on every frame_set, and
    # the bones it does not key must stay at rest for the whole clip rather than
    # being re-zeroed underneath an evaluation.
    reset_pose_to_rest(armature_object)
    apply_action(armature_object, action, first)

    frames = []
    for frame in range(first, last + 1):
        bpy.context.scene.frame_set(frame)
        frames.append(read_pose_locals(armature_object, deform_bone_names, parents))
    return frames


def verify_clip_animates(name, frames):
    """A clip whose frames are all identical did not evaluate.

    This is verify_poses_differ's job on the clip side, and it cannot be the
    same check: a pose is compared against REST, but a clip may legitimately
    start at rest and is only wrong if it never leaves. What an unevaluated
    action produces is a CONSTANT clip -- well-formed, plausible, and silent.
    """
    if len(frames) < 2:
        return
    if all(channels_equal(frames[0], channels) for channels in frames[1:]):
        fail(f"clip '{name}' exported {len(frames)} identical frames. Either the "
             f"rig did not re-evaluate per frame -- see enter_pose_mode -- or "
             f"nothing the action drives reaches a deform bone")


def verify_clips_differ(exported):
    for index in range(len(exported)):
        for other in range(index + 1, len(exported)):
            if frames_equal(exported[index][1], exported[other][1]):
                fail(f"clips '{exported[index][0]}' and '{exported[other][0]}' "
                     f"exported identical, so one would be a junk asset nobody "
                     f"notices. Blender's own stashed copies are already filtered "
                     f"(collect_stashed_actions), so this is two real actions "
                     f"holding the same animation -- delete one in Blender")


def export_clips(output_directory, actions, armature_object, deform_bone_names,
                 parents, skeleton_name, skeleton_hash):
    if not actions:
        log("no actions in this .blend; skipping clip export")
        return

    frame_rate = scene_frame_rate()

    # BUILD EVERYTHING BEFORE WRITING ANYTHING, same rule as the meshes and the
    # poses: a set that fails a check below must not leave half its clips on disk.
    exported = []
    for action in actions:
        frames = read_clip_frames(armature_object, action, deform_bone_names, parents)
        verify_clip_animates(action.name, frames)
        exported.append((sanitize(action.name), frames))

    verify_clips_differ(exported)

    for name, frames in exported:
        # No `stride`: it is the forward travel of the planted foot over one
        # cycle, and it exists to drive a locomotion clip's phase from SPEED
        # instead of from time. Nothing here measures it yet, and a clip without
        # it is time-driven, which is right for everything authored so far.
        write_animation(os.path.join(output_directory, f"{name}.animation"),
                        name, skeleton_name, skeleton_hash, frame_rate, frames)

    reset_pose_to_rest(armature_object)
    log(f"exported {len(exported)} clip(s) at {frame_rate:g} fps")


def verify_poses_differ(rest_channels, exported):
    """Every pose must differ from rest, and no two may be identical.

    This is the assert for `enter_pose_mode`, placed where the symptom is rather
    than where the cause is: an unevaluated pose does not raise, it silently
    equals rest. Comparing exported DEFORM channels is the only check that
    actually witnesses evaluation having happened -- the action assignment
    succeeds either way, and `matrix_basis` moves either way.
    """
    for name, channels in exported:
        if channels_equal(rest_channels, channels):
            fail(f"pose '{name}' exported identical to the REST pose. The rig's "
                 f"pose did not evaluate -- see enter_pose_mode -- or the action "
                 f"keys only bones outside the deform set")

    for index in range(len(exported)):
        for other in range(index + 1, len(exported)):
            if channels_equal(exported[index][1], exported[other][1]):
                fail(f"poses '{exported[index][0]}' and '{exported[other][0]}' "
                     f"exported identical. Either the rig's pose did not "
                     f"re-evaluate between them, or the two .blend files hold "
                     f"the same pose")


def export_poses(output_directory, pose_directory, armature_object,
                 deform_bone_names, parents, skeleton_name, skeleton_hash):
    pose_files = find_pose_files(pose_directory)
    if not pose_files:
        log(f"no pose .blend files in '{pose_directory}'; skipping pose export")
        return

    # The baseline every exported pose is checked against. Read AFTER the reset
    # and through the same path, so it is rest as this exporter sees it rather
    # than as bone.matrix_local claims it.
    reset_pose_to_rest(armature_object)
    bpy.context.scene.frame_set(bpy.context.scene.frame_current)
    rest_channels = read_pose_locals(armature_object, deform_bone_names, parents)

    exported = []
    for pose_file in pose_files:
        actions = append_pose_actions(pose_file)
        if not actions:
            log(f"WARNING '{os.path.basename(pose_file)}' holds no Action; skipped")
            continue

        for action in actions:
            first, last = action.frame_range
            if int(first) != int(last):
                log(f"WARNING action '{action.name}' spans frames "
                    f"{int(first)}..{int(last)}; only frame {int(first)} is "
                    f"exported. A multi-frame clip is build-order step 3, not a "
                    f"pose")

            # Reset FIRST. A pose action that does not key every control bone
            # would otherwise inherit whatever the previous pose left behind --
            # and with 18 bones already off rest in the .blend, the very first
            # pose would inherit those.
            reset_pose_to_rest(armature_object)
            apply_action(armature_object, action, first)

            # The ACTION name is the pose's identity, not the filename: the file
            # `right_holding_gun_1.asset.blend` holds the action
            # `right_holding_gun`, and the `_1` is Blender's, not the author's.
            exported.append((sanitize(action.name),
                             read_pose_locals(armature_object, deform_bone_names,
                                              parents)))

    # BUILD EVERYTHING BEFORE WRITING ANYTHING, same rule as the meshes: a set
    # that fails the check below must not leave half its poses on disk.
    verify_poses_differ(rest_channels, exported)

    for name, channels in exported:
        write_animation(os.path.join(output_directory, f"{name}.animation"),
                        name, skeleton_name, skeleton_hash, POSE_FPS, [channels])

    reset_pose_to_rest(armature_object)
    log(f"exported {len(exported)} pose(s) from {len(pose_files)} file(s)")


# --- Entry point --------------------------------------------------------


def parse_argument(argv, flag, default):
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []
    if flag in argv:
        position = argv.index(flag)
        if position + 1 >= len(argv):
            fail(f"{flag} given with no value")
        return argv[position + 1]
    return default


def main():
    if bpy.app.version[:2] != REQUIRED_VERSION:
        fail(f"this exporter targets Blender "
             f"{REQUIRED_VERSION[0]}.{REQUIRED_VERSION[1]}, found "
             f"{bpy.app.version_string}")

    output_directory = parse_argument(sys.argv, "--out", "resources/models")
    pose_directory = parse_argument(sys.argv, "--poses", DEFAULT_POSE_DIRECTORY)
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

    # Saving a .blend while in Edit Mode is the one authoring mistake that
    # produces a file which looks fine everywhere: Blender keeps the edits in a
    # BMesh and does not sync the derived arrays until you leave the mode, so
    # `mesh.vertices` can be stale and `uv_layers.active.data` can be EMPTY
    # while loops and polygons are fully populated. Every downstream symptom
    # (missing UVs, vertex positions that disagree with what is on screen) is
    # unrecognisable as this cause, so name it here, once, before anything reads
    # a vertex. Refusing rather than calling mode_set is deliberate: the fix
    # belongs in the .blend, and an exporter that silently repairs its input
    # hides the fact that the saved file is not what the author sees.
    in_edit_mode = [o.name for o in mesh_objects if o.data.is_editmode]
    if in_edit_mode:
        fail(f"these meshes were saved in EDIT MODE: {in_edit_mode}. Their UVs "
             f"and vertex data are not synced. Tab back to Object Mode in "
             f"Blender, save, and re-export")

    deform_bones = collect_deform_bones(armature_object)
    parents = reconstruct_parents(deform_bones)

    # The clip list, taken HERE and not at the point of use: appending a pose
    # file adds its Action to bpy.data.actions, so after export_poses has run
    # there is no longer any way to tell an authored clip from an appended pose.
    # This is also the entire mechanism that keeps the two families disjoint.
    stashed_actions = collect_stashed_actions(armature_object)
    if stashed_actions:
        log(f"skipping {len(stashed_actions)} stashed action(s) -- Blender's "
            f"muted [Action Stash], not authored clips: {sorted(stashed_actions)}")
    scene_actions = [a for a in bpy.data.actions if a.name not in stashed_actions]
    log(f"clips: {[a.name for a in scene_actions]}")

    # By NAME, because entering Pose mode below rebuilds the armature's bone
    # datablocks and a `Bone` collected beforehand dangles (read_pose_locals).
    deform_bone_names = [b.name for b in deform_bones]

    skeleton_name = armature_object.name
    ordered_names = [engine_bone_name(b.name) for b in deform_bones]
    skeleton_hash = fnv1a_64("\n".join(ordered_names))

    bone_index_of_name = {b.name: i for i, b in enumerate(deform_bones)}
    textures = TextureWriter(output_directory)

    # BUILD EVERYTHING BEFORE WRITING ANYTHING. The skeleton used to be written
    # here, before the meshes were built, so a mesh that failed to build left a
    # NEW .skeleton beside a STALE .mesh -- and since the hash is over bone
    # names, an unrelated failure produces a pair the loader's hash check
    # cannot tell apart from a good one. A half-written export must not be
    # loadable.
    built_meshes = []
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
        built_meshes.append((mesh_object, vertices, triangles_by_material))

    if not built_meshes:
        fail(f"no mesh is bound to '{armature_object.name}'; nothing to write")

    write_skeleton(os.path.join(output_directory, f"{skeleton_name}.skeleton"),
                   skeleton_name, deform_bones, parents, skeleton_hash)
    for mesh_object, vertices, triangles_by_material in built_meshes:
        write_mesh(os.path.join(output_directory, f"{mesh_object.name}.mesh"),
                   mesh_object.name, skeleton_name, skeleton_hash,
                   mesh_object, vertices, triangles_by_material, textures)

    # Clips and poses come LAST because they are the only steps that mutate the
    # scene -- assigning actions, entering Pose mode and moving pose bones. The
    # vertex buffer is read from the original mesh data rather than the evaluated
    # one, so it would survive either order; doing it last means nothing has to
    # rely on that. Nothing above may touch `deform_bones` after this point.
    prepare_pose_evaluation(armature_object)
    export_clips(output_directory, scene_actions, armature_object,
                 deform_bone_names, parents, skeleton_name, skeleton_hash)
    export_poses(output_directory, pose_directory, armature_object,
                 deform_bone_names, parents, skeleton_name, skeleton_hash)

    log("done")


try:
    main()
except ExportError as error:
    print(f"[export] ERROR {error}", file=sys.stderr)
    sys.exit(1)
