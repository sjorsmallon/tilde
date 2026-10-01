# Blender -> static .glb, for a mesh whose material is PROCEDURAL (a node group
# such as Clay Doh) and therefore means nothing outside Blender.
#
#   blender <file>.blend --background --python src/tools/blender_bake.py -- \
#       --object "Clay Text Object.001" --out resources/glb/mvmt.glb
#       [--size 2048] [--samples 16] [--textures <directory>]
#
# What it does, on a COPY (the .blend is never saved):
#   1. evaluates the object's modifiers into a plain mesh at the origin,
#   2. unwraps it if it has no UV map, then triangulates (the tangent basis of
#      the bake and of the exported triangles must be the same one),
#   3. bakes base colour, tangent-space normal and roughness with Cycles,
#   4. replaces the material with a plain Principled one reading those three
#      images, which is the only kind the glTF exporter understands,
#   5. exports that one object.
#
# Material displacement is baked as BUMP: it lands in the normal map and the
# silhouette stays the mesh's. Metallic is not baked; the result is a dielectric.
#
# --textures keeps the three PNGs for inspection. Never point it inside
# resources/: albedo.png beside normal.png is a material folder to asset_pack.

import bmesh
import bpy
import math
import numpy
import os
import sys
import tempfile
import traceback

REQUIRED_VERSION = (5, 1)

BAKE_MARGIN_PIXELS = 16
UNWRAP_ANGLE_LIMIT_DEGREES = 66.0
UNWRAP_ISLAND_MARGIN = 0.02

# name, Cycles bake type, pass filter, colour data (sRGB) or not
BAKE_PASSES = (
    ("base_color", "DIFFUSE", {"COLOR"}, True),
    ("normal", "NORMAL", set(), False),
    ("roughness", "ROUGHNESS", set(), False),
)


class BakeError(Exception):
    pass


def log(message):
    print(f"[bake] {message}")


def fail(message):
    raise BakeError(message)


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


def select_only(target):
    for scene_object in bpy.context.view_layer.objects:
        scene_object.select_set(False)
    target.select_set(True)
    bpy.context.view_layer.objects.active = target


def make_bake_object(source, name):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(source.evaluated_get(depsgraph),
                                           preserve_all_data_layers=True,
                                           depsgraph=depsgraph)
    if len(mesh.polygons) == 0:
        fail(f"'{source.name}' evaluates to no faces; if its Geometry Nodes "
             f"output is instances, end the group with Realize Instances")
    mesh.name = name
    bake_object = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(bake_object)
    return bake_object


def unwrap(bake_object):
    select_only(bake_object)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(UNWRAP_ANGLE_LIMIT_DEGREES),
                             island_margin=UNWRAP_ISLAND_MARGIN)
    bpy.ops.object.mode_set(mode="OBJECT")


def triangulate(mesh):
    work = bmesh.new()
    work.from_mesh(mesh)
    bmesh.ops.triangulate(work, faces=work.faces[:])
    work.to_mesh(mesh)
    work.free()


# Geometry Nodes leaves slots behind that no face uses (the base plane's, an
# empty one), and the bake refuses an object with a slot it cannot bake. Keep
# only what the faces name, each material once.
def compact_materials(mesh):
    slot_materials = list(mesh.materials)
    materials = []
    new_indices = []
    for polygon in mesh.polygons:
        slot = polygon.material_index
        material = slot_materials[slot] if slot < len(slot_materials) else None
        if material is None:
            fail(f"'{mesh.name}' has faces on material slot {slot}, which is empty")
        if material.node_tree is None:
            fail(f"material '{material.name}' has no node tree to bake")
        if material not in materials:
            materials.append(material)
        new_indices.append(materials.index(material))

    mesh.materials.clear()
    for material in materials:
        mesh.materials.append(material)
    mesh.polygons.foreach_set("material_index", new_indices)
    return materials


def add_bake_target_node(material):
    nodes = material.node_tree.nodes
    for node in nodes:
        node.select = False
    target_node = nodes.new("ShaderNodeTexImage")
    target_node.select = True
    nodes.active = target_node
    return target_node


def read_pixels(image):
    pixels = numpy.empty(image.size[0] * image.size[1] * 4, dtype=numpy.float32)
    image.pixels.foreach_get(pixels)
    return pixels.reshape(-1, 4)


def bake_pass(bake_object, target_nodes, pass_name, bake_type, pass_filter,
              is_color, size, directory):
    image = bpy.data.images.new(f"{bake_object.name}_{pass_name}", size, size,
                                alpha=False, is_data=not is_color)
    for target_node in target_nodes:
        target_node.image = image

    select_only(bake_object)
    arguments = dict(type=bake_type, margin=BAKE_MARGIN_PIXELS,
                     use_selected_to_active=False, target="IMAGE_TEXTURES",
                     use_clear=True, normal_space="TANGENT")
    if pass_filter:
        arguments["pass_filter"] = pass_filter
    result = bpy.ops.object.bake(**arguments)
    if "FINISHED" not in result:
        fail(f"the {pass_name} bake did not finish: {result}")

    colour = read_pixels(image)[:, :3]
    lowest = float(colour.min())
    highest = float(colour.max())
    log(f"{pass_name}: range {lowest:.3f} .. {highest:.3f}, "
        f"mean {colour.mean(axis=0).round(3).tolist()}")
    if highest <= 1e-4:
        fail(f"the {pass_name} bake came out black")

    path = os.path.join(directory, f"{pass_name}.png")
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()
    return image


def build_baked_material(name, images):
    material = bpy.data.materials.new(name)
    if material.node_tree is None:
        material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()

    output = nodes.new("ShaderNodeOutputMaterial")
    principled = nodes.new("ShaderNodeBsdfPrincipled")
    links.new(principled.outputs["BSDF"], output.inputs["Surface"])

    base_color = nodes.new("ShaderNodeTexImage")
    base_color.image = images["base_color"]
    links.new(base_color.outputs["Color"], principled.inputs["Base Color"])

    roughness = nodes.new("ShaderNodeTexImage")
    roughness.image = images["roughness"]
    links.new(roughness.outputs["Color"], principled.inputs["Roughness"])

    normal = nodes.new("ShaderNodeTexImage")
    normal.image = images["normal"]
    normal_map = nodes.new("ShaderNodeNormalMap")
    links.new(normal.outputs["Color"], normal_map.inputs["Color"])
    links.new(normal_map.outputs["Normal"], principled.inputs["Normal"])

    principled.inputs["Metallic"].default_value = 0.0
    return material


def replace_materials(bake_object, material):
    mesh = bake_object.data
    mesh.materials.clear()
    mesh.materials.append(material)
    for polygon in mesh.polygons:
        polygon.material_index = 0


def main():
    if bpy.app.version[:2] != REQUIRED_VERSION:
        fail(f"this script targets Blender "
             f"{REQUIRED_VERSION[0]}.{REQUIRED_VERSION[1]}, found "
             f"{bpy.app.version_string}")

    object_name = parse_argument(sys.argv, "--object", None)
    output_path = parse_argument(sys.argv, "--out", None)
    size = int(parse_argument(sys.argv, "--size", "2048"))
    samples = int(parse_argument(sys.argv, "--samples", "16"))
    texture_directory = parse_argument(sys.argv, "--textures", None)
    if object_name is None or output_path is None:
        fail("usage: -- --object <name> --out <file.glb> "
             "[--size 2048] [--samples 16] [--textures <directory>]")

    source = bpy.data.objects.get(object_name)
    if source is None:
        fail(f"no object named '{object_name}'; the file holds "
             f"{[o.name for o in bpy.data.objects if o.type == 'MESH']}")

    output_path = os.path.abspath(output_path)
    name = os.path.splitext(os.path.basename(output_path))[0]
    if texture_directory is None:
        texture_directory = tempfile.mkdtemp(prefix=f"{name}_bake_")
    texture_directory = os.path.abspath(texture_directory)
    os.makedirs(texture_directory, exist_ok=True)
    log(f"blender {bpy.app.version_string}, '{object_name}' -> {output_path}")

    bake_object = make_bake_object(source, name)
    mesh = bake_object.data
    if not mesh.uv_layers:
        log("no UV map; unwrapping with Smart UV Project")
        unwrap(bake_object)
    triangulate(mesh)
    log(f"mesh: {len(mesh.vertices)} vertices, {len(mesh.polygons)} triangles")

    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = False

    materials = compact_materials(mesh)
    log(f"materials: {[material.name for material in materials]}")
    target_nodes = []
    for material in materials:
        material.displacement_method = "BUMP"
        target_nodes.append(add_bake_target_node(material))

    images = {}
    for pass_name, bake_type, pass_filter, is_color in BAKE_PASSES:
        images[pass_name] = bake_pass(bake_object, target_nodes, pass_name,
                                      bake_type, pass_filter, is_color, size,
                                      texture_directory)
    log(f"textures -> {texture_directory}")

    replace_materials(bake_object, build_baked_material(name, images))

    select_only(bake_object)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=output_path, export_format="GLB",
                              use_selection=True, export_animations=False,
                              export_yup=True)
    log(f"wrote {output_path} ({os.path.getsize(output_path) / 1e6:.1f} MB)")
    log("done")


# Everything is reported on stdout: the Clay Doh text group makes Blender print
# hundreds of dependency-graph complaints to stderr, so the wrapper drops stderr.
try:
    main()
except BakeError as error:
    print(f"[bake] ERROR {error}")
    sys.exit(1)
except Exception:
    print("[bake] ERROR unexpected failure")
    traceback.print_exc(file=sys.stdout)
    sys.exit(1)
