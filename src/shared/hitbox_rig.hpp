#pragma once

#include "asset_types.hpp"
#include "linalg.hpp"
#include "hit_region.hpp"
#include "skeleton.hpp"
#include "span.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace assets
{

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

inline bool hitbox_shape_uses_radius(hitbox_shape_t shape)
{
  return shape != hitbox_shape_t::Box;
}
constexpr float HITBOX_SIZE_PERCENTILE = 0.9f;
constexpr float HITBOX_COVERAGE_TOLERANCE = 3.0f;
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

struct hitbox_rig_file_t
{
  std::string                  name;
  std::string                  skeleton_name;
  uint64_t                     skeleton_hash = 0;
  std::vector<hitbox_volume_t> volumes;
};

// hitboxes that are attached to a rig.
struct rigged_hitbox_volume_t
{
  hitbox_volume_t volume;

  uint32_t start_bone = 0; // indices into skeleton_t::bones
  uint32_t end_bone   = 0;

  // The bones whose flesh this volume is responsible for: the parent chain from
  // `end_bone` up to `start_bone`, with the end bone dropped unless the span is
  // a single bone. The end bone is the NEXT volume's start, so counting its
  // vertices here would inflate this size and derive the elbow twice.
  std::vector<uint32_t> span_bones;
};

// the actual runtime type.
struct hitbox_rig_t
{
  std::string                         name;
  std::string                         skeleton_name;
  uint64_t                            skeleton_hash = 0;
  std::vector<rigged_hitbox_volume_t> volumes;

  // The skeleton the bone indices above are indices INTO. A resolved rig only
  // exists against one skeleton, so carrying it here is what stops a caller
  // pairing it with a second one it built its own path to. Pool storage is a
  // deque, so this outlives every rig resolved from it.
  const skeleton_t* skeleton = nullptr;
};

// Every bone name must exist and `start_bone` must be an ancestor of (or equal
// to) `end_bone`; either failure is an empty optional and a logged error naming
// the volume, never a volume quietly dropped from the set.
[[nodiscard]] std::optional<hitbox_rig_t> try_resolve_hitbox_rig(const hitbox_rig_file_t &file,
                                                                 const skeleton_t &skeleton);

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

// hit detection takes in a span of these.
// this allows that to be free of knowing about skeletons and whatever.
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
void compute_posed_hitboxes(const hitbox_rig_t &rig, Span<const linalg::mat4f> model_space,
                            Span<posed_hitbox_t> out);

// The bind pose's model-space matrices, which is the frame the mesh's vertices
// are already in -- so derivation and coverage both compare skin against bones
// with no skinning step. `out` must be skeleton.bones.size() long.
void compute_bind_model_matrices(const skeleton_t &skeleton, Span<linalg::mat4f> out);

// How far OUTSIDE the volume a point is; 0 anywhere inside it. The one place a
// shape's geometry is written down -- coverage, derivation and the hit test all
// ask this rather than each switching over the kinds.
float distance_outside_hitbox(const posed_hitbox_t &hitbox, const linalg::vec3f &point);

struct hitbox_ray_hit_t
{
  float         distance = 0.0f;  // along `direction`, which is unit length
  linalg::vec3f normal{};         // outward surface normal at the impact point
};


[[nodiscard]] std::optional<hitbox_ray_hit_t> intersect_ray_hitbox(const posed_hitbox_t &hitbox,
                                                                   const linalg::vec3f  &origin,
                                                                   const linalg::vec3f &direction);

// --- Tool-time: seeding and auditing ---------------------------------------

// What derivation offers for a volume: the radius a round shape would take and
// the half-extents a Box would, both from the same vertices. Both are filled
// regardless of the volume's current shape, so switching shape in the tool has
// a seed waiting rather than a zero.
struct guesstimated_hitbox_from_bone_t
{
  float         radius       = 0.0f;
  linalg::vec3f half_extents = {0, 0, 0};
};

guesstimated_hitbox_from_bone_t derive_hitbox_size(const mesh_asset_t &mesh, const skeleton_t &skeleton,
                                 const rigged_hitbox_volume_t &rigged);

void derive_hitbox_sizes(const mesh_asset_t &mesh, const skeleton_t &skeleton,
                         const hitbox_rig_t &rig, Span<guesstimated_hitbox_from_bone_t> out);

hitbox_rig_t make_hitbox_rig_template(const mesh_asset_t &mesh, const skeleton_t &skeleton);

// how many vertices in the bind pose are unreached, and what's the worstg bone?
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
