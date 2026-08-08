# Reports which bpy accessors the installed Blender actually exposes, and what
# the file's rig looks like once Rigify noise is filtered out, so the exporter
# is written against a verified API surface rather than a remembered one.
#
#   blender <file>.blend --background --python src/tools/blender_api_probe.py
#
# Or paste into the Scripting workspace's Text Editor and hit Run Script --
# output goes to the System Console (Window > Toggle System Console on Windows).

import bpy


def report(label, present, detail=""):
    mark = "yes" if present else "NO "
    print(f"  [{mark}] {label}{('  -- ' + detail) if detail else ''}")


def probe_attributes(owner, names, label):
    print(f"\n{label}  ({type(owner).__name__})")
    for name in names:
        has = hasattr(owner, name)
        detail = ""
        if has:
            try:
                value = getattr(owner, name)
                detail = type(value).__name__
                if hasattr(value, "__len__"):
                    detail += f", len={len(value)}"
            except Exception as error:
                detail = f"raised {type(error).__name__}: {error}"
        report(name, has, detail)


print("=" * 70)
print(f"blender version : {bpy.app.version_string}  {bpy.app.version}")
print(f"file            : {bpy.data.filepath or '<none>'}")
print("=" * 70)

# --- Object selection ---------------------------------------------------
#
# Rigify generates one WGT-* mesh per control widget -- pure UI, never
# exported. And a Rigify file holds TWO armatures: the hand-authored `metarig`
# and the generated `rig` that the mesh is actually bound to. Pick the armature
# the mesh's Armature modifier names rather than whichever sorts first.

widget_meshes = [o for o in bpy.data.objects
                 if o.type == "MESH" and o.name.startswith("WGT-")]
mesh_objects = [o for o in bpy.data.objects
                if o.type == "MESH" and not o.name.startswith("WGT-")]
armature_objects = [o for o in bpy.data.objects if o.type == "ARMATURE"]

print(f"\nrenderable meshes : {[o.name for o in mesh_objects]}")
print(f"WGT- widgets      : {len(widget_meshes)} (skipped)")
print(f"armatures         : {[o.name for o in armature_objects]}")

mesh_object = mesh_objects[0] if mesh_objects else None
armature_object = None
if mesh_object:
    for modifier in mesh_object.modifiers:
        if modifier.type == "ARMATURE" and modifier.object:
            armature_object = modifier.object
            print(f"\n'{mesh_object.name}' Armature modifier -> '{armature_object.name}'")
            break
if armature_object is None and armature_objects:
    armature_object = armature_objects[-1]
    print(f"\nno Armature modifier found; falling back to '{armature_object.name}'")

# --- Mesh ---------------------------------------------------------------
if mesh_object:
    mesh = mesh_object.data
    probe_attributes(
        mesh,
        ["vertices", "loops", "loop_triangles", "calc_loop_triangles",
         "corner_normals", "calc_normals_split", "uv_layers", "attributes"],
        f"mesh data '{mesh_object.name}'",
    )

    if hasattr(mesh, "calc_loop_triangles"):
        mesh.calc_loop_triangles()
    print(f"\n  triangles={len(mesh.loop_triangles)} loops={len(mesh.loops)} "
          f"vertices={len(mesh.vertices)}")
    try:
        print(f"  corner_normals[0] = {mesh.corner_normals[0].vector}")
    except Exception as error:
        print(f"  corner_normals unavailable: {type(error).__name__}: {error}")
    if mesh.uv_layers.active:
        print(f"  uv_layers.active.data[0].uv = {mesh.uv_layers.active.data[0].uv}")
    else:
        print("  uv_layers.active is None -- mesh has no UVs")

    # influence-count histogram: the exporter caps at 4
    histogram = {}
    over_cap = 0
    for vertex in mesh.vertices:
        count = len(vertex.groups)
        histogram[count] = histogram.get(count, 0) + 1
        if count > 4:
            weights = sorted((element.weight for element in vertex.groups), reverse=True)
            if sum(weights[4:]) > 0.05:
                over_cap += 1
    print(f"\n  influences per vertex: {dict(sorted(histogram.items()))}")
    print(f"  vertices losing >0.05 total weight at a 4-influence cap: {over_cap}")

    print(f"\n  vertex_groups ({len(mesh_object.vertex_groups)}): "
          f"{[g.name for g in mesh_object.vertex_groups]}")

# --- Armature -----------------------------------------------------------
if armature_object:
    armature = armature_object.data
    deform_bones = [b for b in armature.bones if b.use_deform]
    print(f"\narmature '{armature_object.name}': {len(armature.bones)} bones total, "
          f"{len(deform_bones)} with use_deform")

    if mesh_object:
        group_names = {g.name for g in mesh_object.vertex_groups}
        deform_names = {b.name for b in deform_bones}
        missing = sorted(group_names - deform_names)
        unused = sorted(deform_names - group_names)
        print(f"  vertex groups with no matching deform bone: {missing or 'none'}")
        print(f"  deform bones with no vertex group:          {unused or 'none'}")

    # --- Parent reconstruction ------------------------------------------
    #
    # Rigify drives DEF- bones by CONSTRAINT, not by parenting, so within the
    # exported subset many DEF bones have no deform ancestor at all -- the real
    # chain runs through ORG-/MCH-/control bones we discard. Walk up the full
    # tree and, at each ancestor, also try that ancestor's DEF- twin by name.
    # Without this the exported skeleton is a set of disconnected roots, which
    # renders fine but makes runtime pose edits (aim, layer masks) impossible.

    deform_names = {b.name for b in deform_bones}

    def resolve_parent(bone):
        ancestor = bone.parent
        hops = []
        while ancestor is not None:
            hops.append(ancestor.name)
            if ancestor.name in deform_names:
                return ancestor.name, hops, "direct"
            bare = ancestor.name
            for prefix in ("ORG-", "MCH-", "DEF-"):
                if bare.startswith(prefix):
                    bare = bare[len(prefix):]
                    break
            twin = "DEF-" + bare
            if twin in deform_names and twin != bone.name:
                return twin, hops, f"mapped {ancestor.name} -> {twin}"
            ancestor = ancestor.parent
        return None, hops, "no deform ancestor"

    print(f"\n  raw parent chains (what Rigify actually gives us):")
    for bone in deform_bones:
        chain = []
        walker = bone.parent
        while walker is not None and len(chain) < 6:
            chain.append(walker.name)
            walker = walker.parent
        print(f"    {bone.name:<24} <- {' <- '.join(chain) if chain else '(no parent)'}")

    print(f"\n  reconstructed hierarchy:")
    index_of = {b.name: i for i, b in enumerate(deform_bones)}
    roots = 0
    out_of_order = 0
    mapped = 0
    for index, bone in enumerate(deform_bones):
        parent_name, hops, how = resolve_parent(bone)
        parent_index = index_of[parent_name] if parent_name else -1
        if parent_index < 0:
            roots += 1
        if parent_index >= index:
            out_of_order += 1
        if how.startswith("mapped"):
            mapped += 1
        note = "" if how == "direct" else f"   [{how}]"
        flag = "" if parent_index < index else "   <-- OUT OF ORDER"
        print(f"    {index:3d} {bone.name:<24} parent={parent_index:3d} "
              f"{(parent_name or 'ROOT'):<24}{note}{flag}")

    print(f"\n  -> roots={roots} (want 1)  remapped={mapped}  "
          f"out_of_order={out_of_order} (want 0)")
    if roots == 1 and out_of_order == 0:
        print("  -> RECONSTRUCTION OK: single-rooted, parent-before-child")
    else:
        print("  -> RECONSTRUCTION FAILED: exporter must sort, or re-parent in Blender")

    if len(armature.bones):
        probe_attributes(armature.bones[0],
                         ["matrix_local", "head_local", "tail_local", "use_deform"],
                         "bone[0]")
    if armature_object.pose and len(armature_object.pose.bones):
        probe_attributes(armature_object.pose.bones[0],
                         ["matrix", "matrix_basis", "parent", "bone"],
                         "pose_bone[0]")

# --- Actions / poses ----------------------------------------------------
scene = bpy.context.scene
print(f"\nscene frame_start={scene.frame_start} frame_end={scene.frame_end} "
      f"fps={scene.render.fps}/{scene.render.fps_base}")
print(f"\nactions ({len(bpy.data.actions)}):")
for action in bpy.data.actions:
    start, end = action.frame_range
    kind = "POSE (single frame)" if abs(end - start) < 0.5 else "CLIP"
    is_asset = getattr(action, "asset_data", None) is not None
    print(f"  '{action.name}'  frames {start:.0f}..{end:.0f}  {kind}"
          f"{'  [asset/pose-library]' if is_asset else ''}"
          f"  fcurves={len(action.fcurves)}")

if bpy.data.actions:
    probe_attributes(bpy.data.actions[0],
                     ["frame_range", "fcurves", "slots", "layers", "asset_data",
                      "groups"],
                     "action[0]")

if armature_object:
    animation_data = armature_object.animation_data
    print(f"\narmature animation_data: "
          f"{'present' if animation_data else 'None'}")
    if animation_data:
        print(f"  action      = {animation_data.action.name if animation_data.action else None}")
        print(f"  action_slot = {getattr(animation_data, 'action_slot', '<no attr>')}")
        print(f"  nla_tracks  = {[t.name for t in animation_data.nla_tracks]}")

    # evaluated local transform at the current frame -- the path the exporter uses
    scene.frame_set(scene.frame_start)
    if armature_object.pose and len(armature_object.pose.bones):
        pose_bone = armature_object.pose.bones[0]
        local = (pose_bone.parent.matrix.inverted() @ pose_bone.matrix
                 if pose_bone.parent else pose_bone.matrix)
        translation, rotation, scale = local.decompose()
        print(f"\n  frame {scene.frame_start} '{pose_bone.name}' local decompose:")
        print(f"    translation={tuple(round(v, 4) for v in translation)}")
        print(f"    rotation   ={tuple(round(v, 4) for v in rotation)}  (w,x,y,z)")
        print(f"    scale      ={tuple(round(v, 4) for v in scale)}")

print("\n" + "=" * 70)
