#pragma once

// Readers for the `.skeleton` and `.mesh` text formats written by
// src/tools/blender_export.py. See animation_def.md §1-§3.
//
// Text, line-oriented, in the family of the `.source` map format: diffable,
// greppable, debuggable without a tool. Float precision loss is irrelevant here
// (unlike entity field diffs, where it bit us). If these go binary later, the
// structs stay and only reader and writer change.
//
// GRAMMAR
//
//   skeleton_file := "skeleton" ident nl "hash" hex nl "bones" int nl { bone_line }
//   bone_line     := "b" int ident int f32{16} nl
//                    // index name parent inverse_bind
//                    // parent -1 = root; otherwise parent < index
//
//   mesh_file     := "mesh" ident nl [ "skeleton" ident hex nl ] "scale" f32 nl
//                    { material_line } vertex_block index_block { submesh_line }
//   material_line := "mat" int ident path nl
//   vertex_block  := "vertices" int nl { vertex_line }
//   vertex_line   := "v" f32{3} f32{3} f32{2} int{4} f32{4} nl
//                    // position normal uv bone_indices weights (weights sum to 1)
//   index_block   := "indices" int nl { "i" int int int nl }
//                    // the count is INDICES, so the line count is count/3
//   submesh_line  := "sub" int int int int nl
//                    // index offset count material
//
//   hitbox_file   := "hitboxes" ident nl "skeleton" ident hex nl { volume_line }
//   volume_line   := "v" ident shape volume_body nl
//   shape         := "Sphere" | "Capsule" | "Cylinder" | "Box"
//   volume_body   := ident region f32 [ f32 ]                    // Sphere
//                    // bone region radius [offset]
//                  | ident ident region f32 [ f32 ]              // Capsule,
//                    // start_bone end_bone region radius [offset]  Cylinder
//                  | ident ident region f32{3} [ f32 ]           // Box
//                    // start_bone end_bone region half_extents [offset]
//   region        := "Head" | "Torso" | "Legs"    // shared::hit_region_t
//
//   // There is deliberately NO volume count. The file is HANDWRITTEN, and a
//   // declared count in a handwritten file is a second thing to keep in step
//   // that only ever falls out of it -- unlike the mesh and skeleton counts,
//   // which are written by the exporter and let a truncated file be caught.
//   // Volumes are read until end of file.
//   //
//   // A Sphere takes ONE bone: it sits at that bone's head, and a span between
//   // a bone and itself would be the same shape with a name that lies.
//   // Box half-extents are in the volume's own frame -- right, up, and along
//   // the bone -- so the box turns with the pose.
//   // `offset` (default 0) slides the volume along the start bone's own
//   // direction. It is there for the head: the skull is one bone whose head
//   // sits at the jaw.
//
//   animation_file:= "animation" ident nl "skeleton" ident hex nl
//                    "bones" int nl "fps" f32 nl "frames" int nl
//                    [ "stride" f32 nl ] { frame_block }
//   frame_block   := "f" int nl { channel_line }
//   channel_line  := "b" int f32{3} f32{4} f32{3} nl
//                    // bone translation rotation(x y z w) scale
//
// Blank lines and `//` comments are skipped anywhere.
//
// Matrices are written ROW-MAJOR (m[0][0] m[0][1] ... m[3][3]); mat4f is
// column-major, so the reader transposes.
//
// A `.mesh` with no `skeleton` line is a static mesh: the skin array stays empty
// and nothing downstream changes.
//
// Everything here is a pure function over a file path. Resolving the skeleton a
// mesh names, and checking the two hashes agree, belongs to the asset layer that
// owns the pools -- not here.

#include "animation.hpp"
#include "asset.hpp"
#include "hitbox_rig.hpp"
#include "skeleton.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace models
{

// 1 engine unit == 1 inch (player_eye_height 64, gravity 800), so metres *
// 39.37. The exporter has ALREADY applied this to positions and bone matrices;
// the value rides in the file so the reader can assert agreement rather than
// guess. Must match blender_export.py::METRES_TO_UNITS.
constexpr float METRES_TO_UNITS = 39.37f;

// What a `.mesh` says about the skeleton it was skinned against. The asset layer
// resolves the name against the mesh's own directory and refuses the load if the
// hashes differ, reporting both.
struct skeleton_reference_t
{
  std::string skeleton_name;        // empty means "static mesh, no skin"
  uint64_t    skeleton_hash = 0;
  float       scale         = 1.0f; // as recorded in the file
};

// Both return false and log_error naming the file and line on any malformed or
// self-inconsistent input. Neither ever half-fills its output silently.
bool parse_skeleton_file(const char *path, assets::skeleton_t &out);
bool parse_mesh_file(const char *path, assets::mesh_asset_t &out,
                     skeleton_reference_t &out_reference);

// A clip carries its own skeleton name and hash (`animation_clip_t`), so unlike
// a mesh it needs no separate reference out-param. Checking that hash against a
// LOADED skeleton is still the asset layer's job, not this one's.
//
// The five authored aim poses come through here as single-frame clips. There is
// deliberately no `.pose` format: one format, one loader, one hash check.
bool parse_animation_file(const char *path, assets::animation_clip_t &out);

// The `.hitboxes` mapping, which is HANDWRITTEN rather than exported -- which
// bones are volumes and what they cost is game-design data, not model data
// (todo.md §2e). Resolving the bone names against a loaded skeleton is
// `assets::try_resolve_hitbox_rig`, not this; here the file is only checked for
// being well-formed.
[[nodiscard]] std::optional<assets::hitbox_rig_t> try_parse_hitbox_rig_file(const char *path);

// The writer half, so the Animation tool can emit a template for a rig that has
// no file yet and save a radius you filled from the derived column. The output
// is the same text a human writes -- there is no generated-file convention here,
// because this file is authored and the tool only ever seeds it.
[[nodiscard]] bool try_write_hitbox_rig_file(const char *path, const assets::hitbox_rig_t &rig);

} // namespace models
