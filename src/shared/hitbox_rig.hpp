#pragma once

// The declarative bone -> hit volume mapping, and the tool-time math that seeds
// and audits it. See animation_def.md §4 and todo.md §2e.
//
// A volume is a SHAPE over a bone SPAN: a named shape, two named deform bones,
// and the shape's own size. A span rather than one bone because Rigify splits a
// torso into seven `spine*` bones, any one of which is a seventh of a torso,
// while §4 wants ONE torso volume. Endpoints are the two named bones' HEADS --
// the skeleton stores no tail, and picking the bone whose head is the far end is
// a rule with no reconstruction in it.
//
// This lives in game_shared, not in the exporter and not in the client, because
// both sides evaluate it: the client draws the volumes, the server hit-tests
// against them, and both walk the same pose out of the same skeleton (the bake
// reversal at the top of animation_def.md §4).
//
// Sizes are AUTHORED. `derive_hitbox_size` seeds them from the skin weights and
// re-running shows drift against a mesh that has moved, but the numbers that
// ship are the ones in the file -- deriving at runtime would need the mesh, and
// the server has no meshes. Derivation is tool-time only; nothing in the runtime
// path calls it.

#include "asset.hpp"
#include "linalg.hpp"
#include "player_hitboxes.hpp"
#include "skeleton.hpp"
#include "span.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace assets
{

// Deliberately NOT `entities::Shape_Kind`, which is the vocabulary of entity
// hitbox components and Jolt physics bodies. Two reasons, and they point the
// same way: this set needs a Cylinder (a limb with flat caps, which Jolt cannot
// spawn and the entity collision tests do not implement), and that enum is a
// networked, saveable entity field whose values are switched over exhaustively
// in the physics and collision paths. Adding a member there to serve the rig
// would oblige four unrelated systems to handle a shape they cannot make.
enum class hitbox_shape_t : uint8_t
{
  Sphere,   // one bone; a ball at its head
  Capsule,  // the span, round caps -- the default limb
  Cylinder, // the span, flat caps
  Box,      // an oriented box centred on the span, in the volume's own frame
  Count
};

const char *to_string(hitbox_shape_t shape);
[[nodiscard]] std::optional<hitbox_shape_t> try_hitbox_shape_from_string(const char *text);

// True for the shapes sized by a single radius. The rest is Box, which is sized
// by three half-extents; every "which number does this shape read" test in the
// code goes through here rather than listing kinds again.
inline bool hitbox_shape_uses_radius(hitbox_shape_t shape)
{
  return shape != hitbox_shape_t::Box;
}

// The percentile of distance that becomes a derived size. Not the maximum: one
// stray vertex -- a belt buckle, a hair card -- would otherwise size a whole
// limb.
constexpr float HITBOX_SIZE_PERCENTILE = 0.9f;

// How far a vertex may sit outside every volume before coverage reports it. A
// hit that misses by less than this is inside the noise of where a convex shape
// approximates a surface anyway.
constexpr float HITBOX_COVERAGE_TOLERANCE = 3.0f;

// animation_def.md §4: the absolute hull invariant becomes a bounded, checked
// one, because an extended arm cannot stay inside a 32-wide column.
constexpr float HITBOX_MAX_HULL_EXCURSION = 6.0f;

struct hitbox_volume_t
{
  std::string          name;
  hitbox_shape_t       shape = hitbox_shape_t::Capsule;
  std::string          start_bone;
  std::string          end_bone; // == start_bone for a Sphere
  shared::hit_region_t region    = shared::hit_region_t::Torso;

  // Sphere, Capsule, Cylinder.
  float radius = 0.0f;

  // Box, in the volume's own frame (see hitbox_frame_t): right, up, and along
  // the bone. Ignored by the other three shapes, and vice versa -- one struct
  // rather than a variant because a volume is a row in a text file that a human
  // retypes when they change its shape, and half-filled is the normal state.
  linalg::vec3f half_extents = {0, 0, 0};

  // Slides BOTH endpoints along the start bone's own direction. It exists for
  // the head: the skull is one bone whose head sits at the base of it, so a
  // sphere at that point covers the jaw and misses the crown. Bone space, so it
  // rotates with the pose and the server derives it from the pose alone --
  // unlike a model-space offset, which would be a different volume every time
  // the head turns. 0 for everything else.
  float offset = 0.0f;
};

struct hitbox_rig_t
{
  std::string                  name;
  std::string                  skeleton_name;
  uint64_t                     skeleton_hash = 0;
  std::vector<hitbox_volume_t> volumes;
};

// A volume's bone names resolved against one loaded skeleton. Parallel to
// `hitbox_rig_t::volumes`, never reordered -- the two are indexed together.
struct resolved_hitbox_volume_t
{
  uint32_t start_bone = 0;
  uint32_t end_bone   = 0;

  // The bones whose flesh this volume is responsible for: the parent chain from
  // `end_bone` up to `start_bone`, with the end bone dropped unless the span is
  // a single bone. The end bone is the NEXT volume's start, so counting its
  // vertices here would inflate this size and derive the elbow twice.
  std::vector<uint32_t> span_bones;
};

using resolved_hitbox_rig_t = std::vector<resolved_hitbox_volume_t>;

// Every bone name must exist and `start_bone` must be an ancestor of (or equal
// to) `end_bone`; either failure is an empty optional and a logged error naming
// the volume, never a volume quietly dropped from the set.
[[nodiscard]] std::optional<resolved_hitbox_rig_t>
try_resolve_hitbox_rig(const hitbox_rig_t &rig, const skeleton_t &skeleton);

// The axes a Box's half-extents are written in, derived from the START bone's
// model-space matrix. `forward` is where the bone points, so the third extent is
// the one along the limb; `right` is the bone's own X. Authoring in the bone's
// frame rather than in model space is what makes a box rotate with the pose.
struct hitbox_frame_t
{
  linalg::vec3f right   = {1, 0, 0};
  linalg::vec3f up      = {0, 1, 0};
  linalg::vec3f forward = {0, 0, 1};
};

// One posed volume, in model space. This is what the client draws and what the
// server will ray-test.
struct posed_hitbox_t
{
  hitbox_shape_t       shape  = hitbox_shape_t::Capsule;
  linalg::vec3f        start  = {0, 0, 0};
  linalg::vec3f        end    = {0, 0, 0};
  float                radius = 0.0f;
  linalg::vec3f        half_extents = {0, 0, 0};
  hitbox_frame_t       frame;  // Box only; the other shapes are frame-free
  shared::hit_region_t region = shared::hit_region_t::Torso;

  linalg::vec3f center() const { return (start + end) * 0.5f; }
};

// `model_space` is what compute_model_space_matrices produced for the pose being
// drawn -- NOT the skinning matrices, which carry the inverse bind and would put
// every volume at the origin. `out` must be rig.volumes.size() long; a wrong
// length is fatal.
void compute_posed_hitboxes(const hitbox_rig_t &rig, const resolved_hitbox_rig_t &resolved,
                            Span<const linalg::mat4f> model_space, Span<posed_hitbox_t> out);

// The bind pose's model-space matrices, which is the frame the mesh's vertices
// are already in -- so derivation and coverage both compare skin against bones
// with no skinning step. `out` must be skeleton.bones.size() long.
void compute_bind_model_matrices(const skeleton_t &skeleton, Span<linalg::mat4f> out);

// How far OUTSIDE the volume a point is; 0 anywhere inside it. The one place a
// shape's geometry is written down -- coverage, derivation and (later) the hit
// test all ask this rather than each switching over the kinds.
float distance_outside_hitbox(const posed_hitbox_t &hitbox, const linalg::vec3f &point);

// --- Tool-time: seeding and auditing ---------------------------------------

// What derivation offers for a volume: the radius a round shape would take and
// the half-extents a Box would, both from the same vertices. Both are filled
// regardless of the volume's current shape, so switching shape in the tool has
// a seed waiting rather than a zero.
struct hitbox_seed_t
{
  float         radius       = 0.0f;
  linalg::vec3f half_extents = {0, 0, 0};
};

// A high percentile of the distance from the volume's axis (radius) and of the
// absolute projection onto each of its own axes (half-extents), over the
// vertices whose DOMINANT influence is one of `span_bones`. A volume no vertex
// is dominated by derives zeros, which the caller reports -- a zero-sized volume
// is one that can never be hit.
hitbox_seed_t derive_hitbox_size(const mesh_asset_t &mesh, const skeleton_t &skeleton,
                                 const hitbox_volume_t          &volume,
                                 const resolved_hitbox_volume_t &resolved);

// Every volume's seed, in rig order. `out` must be rig.volumes.size() long.
// Logs the table -- an override only reads as a correction when the number it
// corrects is visible.
void derive_hitbox_sizes(const mesh_asset_t &mesh, const skeleton_t &skeleton,
                         const hitbox_rig_t &rig, const resolved_hitbox_rig_t &resolved,
                         Span<hitbox_seed_t> out);

// Every deform bone as a one-bone capsule with its derived radius, so authoring
// a rig is deleting lines rather than knowing which bone names exist. Regions
// are guessed from the bone name and are meant to be corrected -- the guess is
// there to be edited, not trusted.
hitbox_rig_t make_hitbox_rig_template(const mesh_asset_t &mesh, const skeleton_t &skeleton);

// Vertices no volume reaches, in the bind pose. Bind rather than posed on
// purpose: skinning moves the skin and the volumes together, so a gap is a
// property of the rig, not of the frame you happen to be looking at.
struct hitbox_coverage_t
{
  uint32_t vertex_count           = 0;
  uint32_t uncovered_vertex_count = 0;
  float    worst_distance         = 0.0f;
  int32_t  worst_bone             = -1; // dominant bone of the worst vertex

  // Uncovered vertices per dominant bone, indexed by bone. A total is only a
  // number; this is what says WHICH limb lost its volume, which is the thing
  // worth acting on.
  std::vector<uint32_t> uncovered_by_bone;
};

hitbox_coverage_t compute_hitbox_coverage(const mesh_asset_t &mesh, const skeleton_t &skeleton,
                                          Span<const posed_hitbox_t> hitboxes, float tolerance);

// How far the posed volumes reach outside the movement hull, which is the
// bounded-and-checked replacement for the old absolute invariant (§4). The hull
// is the 32 x 32 x 72 column standing on the feet at the origin.
struct hull_excursion_t
{
  float   distance     = 0.0f;
  int32_t volume_index = -1;
};

hull_excursion_t compute_hull_excursion(Span<const posed_hitbox_t> hitboxes, float half_width,
                                        float height);

} // namespace assets
