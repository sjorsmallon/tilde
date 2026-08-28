#include "hitbox_rig.hpp"

#include "log.hpp"
#include "skinning.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace assets
{
namespace
{

linalg::vec3f matrix_translation(const linalg::mat4f &matrix)
{
  const linalg::vec4 &column = matrix[3];
  return linalg::vec3f{column.x, column.y, column.z};
}

// Where a volume's two endpoints sit, given the matrices of the pose it is being
// evaluated in, and the axes a Box's extents are read in. One function so the
// runtime path and derivation cannot disagree about where a volume is.
void volume_placement(const rigged_hitbox_volume_t &rigged, Span<const linalg::mat4f> model_space,
                      linalg::vec3f &out_start, linalg::vec3f &out_end, hitbox_frame_t &out_frame)
{
  const linalg::mat4f &start_matrix = model_space[rigged.start_bone];

  out_frame.forward = bone_direction(start_matrix);
  const linalg::vec4 &x_column = start_matrix[0];
  out_frame.right              = linalg::normalize(linalg::vec3f{x_column.x, x_column.y, x_column.z});
  out_frame.up                 = linalg::cross(out_frame.forward, out_frame.right);

  const linalg::vec3f slide = out_frame.forward * rigged.volume.offset;
  out_start                 = matrix_translation(start_matrix) + slide;
  out_end                   = matrix_translation(model_space[rigged.end_bone]) + slide;
}

// Distance from a point to the SEGMENT, not to the infinite line: past an
// endpoint a capsule is a hemisphere, and measuring against the line there would
// size a volume from a vertex it already covers.
float distance_to_segment(const linalg::vec3f &point, const linalg::vec3f &start,
                          const linalg::vec3f &end)
{
  const linalg::vec3f axis          = end - start;
  const float         axis_length_2 = linalg::dot(axis, axis);
  if (axis_length_2 <= 1e-8f)
    return linalg::length(point - start); // a sphere volume

  float parameter = linalg::dot(point - start, axis) / axis_length_2;
  parameter       = std::clamp(parameter, 0.0f, 1.0f);
  return linalg::length(point - (start + axis * parameter));
}

// --- Ray casting, one function per surface -------------------------------
//
// Every one of these reports the ENTRY point and rejects a negative distance,
// so "the ray starts inside" is a miss everywhere rather than in three of four
// shapes. They are written as surfaces (a sphere, a cylinder's side, one disc,
// a box) and composed into the four hitbox kinds by intersect_ray_hitbox --
// which is why a capsule is not a fourth copy of the quadratic.

// The NEAR root, unlike linalg::intersect_ray_sphere, which hands back the far
// one when the origin is inside. Here that is a miss, not an exit point.
std::optional<hitbox_ray_hit_t> intersect_ray_sphere_entry(const linalg::vec3f &origin,
                                                           const linalg::vec3f &direction,
                                                           const linalg::vec3f &center,
                                                           float                radius)
{
  const linalg::vec3f to_origin = origin - center;
  const float         half_b    = linalg::dot(to_origin, direction); // |direction| == 1
  const float         c         = linalg::dot(to_origin, to_origin) - radius * radius;

  const float discriminant = half_b * half_b - c;
  if (discriminant < 0.0f)
    return std::nullopt;

  const float distance = -half_b - std::sqrt(discriminant);
  if (distance < 0.0f)
    return std::nullopt;

  const linalg::vec3f point = origin + direction * distance;
  return hitbox_ray_hit_t{distance, (point - center) * (1.0f / radius)};
}

// The SIDE only -- the tube between the two endpoints, open at both ends. A
// capsule caps it with hemispheres and a cylinder with discs; neither wants the
// other's ends, so neither is in here.
std::optional<hitbox_ray_hit_t> intersect_ray_cylinder_side(const linalg::vec3f &origin,
                                                            const linalg::vec3f &direction,
                                                            const linalg::vec3f &start,
                                                            const linalg::vec3f &end, float radius)
{
  const linalg::vec3f axis          = end - start;
  const float         axis_length_2 = linalg::dot(axis, axis);
  if (axis_length_2 <= 1e-8f)
    return std::nullopt; // degenerate span: it is all caps

  const linalg::vec3f to_origin = origin - start;

  const float axis_dot_direction = linalg::dot(axis, direction);
  const float axis_dot_origin    = linalg::dot(axis, to_origin);

  // The quadratic for the components PERPENDICULAR to the axis, scaled through
  // by axis_length_2 so there is no division and no normalize.
  const float a = axis_length_2 - axis_dot_direction * axis_dot_direction;
  const float b = axis_length_2 * linalg::dot(to_origin, direction) -
                  axis_dot_origin * axis_dot_direction;
  const float c = axis_length_2 * linalg::dot(to_origin, to_origin) -
                  axis_dot_origin * axis_dot_origin - radius * radius * axis_length_2;

  if (a <= 1e-8f)
    return std::nullopt; // the ray runs along the axis; only the ends can be hit

  const float discriminant = b * b - a * c;
  if (discriminant < 0.0f)
    return std::nullopt;

  const float distance = (-b - std::sqrt(discriminant)) / a;
  if (distance < 0.0f)
    return std::nullopt;

  // Where along the span the hit landed, in units of axis_length_2. Outside
  // [0, 1] the ray passed the open tube and whatever caps it has has to answer.
  const float along = (axis_dot_origin + distance * axis_dot_direction) / axis_length_2;
  if (along < 0.0f || along > 1.0f)
    return std::nullopt;

  const linalg::vec3f point  = origin + direction * distance;
  const linalg::vec3f normal = (point - (start + axis * along)) * (1.0f / radius);
  return hitbox_ray_hit_t{distance, normal};
}

// One flat end of a cylinder: the plane at that endpoint, cropped to the
// radius.
std::optional<hitbox_ray_hit_t> intersect_ray_cylinder_cap(const linalg::vec3f &origin,
                                                           const linalg::vec3f &direction,
                                                           const linalg::vec3f &start,
                                                           const linalg::vec3f &end, float radius,
                                                           bool at_start)
{
  const linalg::vec3f axis        = end - start;
  const float         axis_length = linalg::length(axis);
  if (axis_length <= 1e-4f)
    return std::nullopt;

  const linalg::vec3f unit_axis = axis * (1.0f / axis_length);
  const linalg::vec3f normal    = at_start ? unit_axis * -1.0f : unit_axis;
  const linalg::vec3f center    = at_start ? start : end;

  // FRONT-facing only. A cylinder is convex, so the ray enters through the disc
  // it meets head-on; accepting the far one would let a ray fired from inside,
  // straight down the axis, exit through the back cap and report that as a hit
  // (the side test cannot catch it -- a ray along the axis never meets the
  // tube). Parallel rays fall out of the same test.
  const float denominator = linalg::dot(direction, normal);
  if (denominator >= -1e-6f)
    return std::nullopt;

  const float distance = linalg::dot(center - origin, normal) / denominator;
  if (distance < 0.0f)
    return std::nullopt;

  const linalg::vec3f offset = (origin + direction * distance) - center;
  if (linalg::dot(offset, offset) > radius * radius)
    return std::nullopt;

  return hitbox_ray_hit_t{distance, normal};
}

// The slab test, run in the box's OWN frame. A box hitbox turns with its bone,
// so testing it as an AABB would mean re-deriving a world-axis box every pose --
// bigger than the volume it stands for, and bigger by a different amount every
// frame.
std::optional<hitbox_ray_hit_t> intersect_ray_oriented_box(const linalg::vec3f  &origin,
                                                           const linalg::vec3f  &direction,
                                                           const linalg::vec3f  &center,
                                                           const hitbox_frame_t &frame,
                                                           const linalg::vec3f  &half_extents)
{
  const linalg::vec3f relative = origin - center;
  const linalg::vec3f axes[3]  = {frame.right, frame.up, frame.forward};

  const float local_origin[3]    = {linalg::dot(relative, axes[0]), linalg::dot(relative, axes[1]),
                                    linalg::dot(relative, axes[2])};
  const float local_direction[3] = {linalg::dot(direction, axes[0]),
                                    linalg::dot(direction, axes[1]),
                                    linalg::dot(direction, axes[2])};
  const float extent[3] = {half_extents.x, half_extents.y, half_extents.z};

  float nearest       = -std::numeric_limits<float>::infinity();
  float furthest      = std::numeric_limits<float>::infinity();
  int   entry_axis    = 0;
  float entry_sign    = 1.0f;

  for (int axis = 0; axis < 3; ++axis)
  {
    if (std::fabs(local_direction[axis]) <= 1e-6f)
    {
      // Parallel to this slab: either already between its planes forever, or
      // never.
      if (std::fabs(local_origin[axis]) > extent[axis])
        return std::nullopt;
      continue;
    }

    const float inverse = 1.0f / local_direction[axis];
    float       low     = (-extent[axis] - local_origin[axis]) * inverse;
    float       high    = (extent[axis] - local_origin[axis]) * inverse;
    float       sign    = -1.0f;
    if (low > high)
    {
      std::swap(low, high);
      sign = 1.0f;
    }

    if (low > nearest)
    {
      nearest    = low;
      entry_axis = axis;
      entry_sign = sign;
    }
    furthest = std::min(furthest, high);

    if (nearest > furthest)
      return std::nullopt;
  }

  if (nearest < 0.0f)
    return std::nullopt; // entry behind the origin, or the origin is inside

  return hitbox_ray_hit_t{nearest, axes[entry_axis] * entry_sign};
}

void keep_nearer(std::optional<hitbox_ray_hit_t> &best, std::optional<hitbox_ray_hit_t> candidate)
{
  if (candidate && (!best || candidate->distance < best->distance))
    best = candidate;
}

// The influence that decides which volume a vertex belongs to. Dominant rather
// than any-influence: a vertex on the elbow is weighted to both bones, and
// counting it for both sizes each volume from the other's flesh.
int32_t dominant_bone(const vertex_skin_t &skin)
{
  int32_t best_bone   = -1;
  float   best_weight = 0.0f;
  for (uint32_t influence = 0; influence < MAX_BONE_INFLUENCES_PER_VERTEX; ++influence)
  {
    if (skin.bone_weights[influence] > best_weight)
    {
      best_weight = skin.bone_weights[influence];
      best_bone   = (int32_t)skin.bone_indices[influence];
    }
  }
  return best_bone;
}

int32_t bone_index_of(const skeleton_t &skeleton, const std::string &name)
{
  for (size_t index = 0; index < skeleton.bones.size(); ++index)
    if (skeleton.bones[index].name == name)
      return (int32_t)index;
  return -1;
}

// The template's region guess, which exists to be corrected. Region is a BALANCE
// decision (a forearm counting as Torso is one), so the only thing this has to
// get right is being obviously wrong when it is wrong.
shared::hit_region_t guess_region(const std::string &bone_name)
{
  auto contains = [&bone_name](const char *needle)
  { return bone_name.find(needle) != std::string::npos; };

  if (contains("head") || contains("neck"))
    return shared::hit_region_t::Head;
  if (contains("thigh") || contains("shin") || contains("foot") || contains("toe") ||
      contains("pelvis"))
    return shared::hit_region_t::Legs;
  return shared::hit_region_t::Torso;
}

// The percentile of a scratch list, by nth_element rather than a full sort --
// derivation runs over every vertex of every volume and this is the inner loop.
float percentile_of(std::vector<float> &values, float percentile)
{
  if (values.empty())
    return 0.0f;
  const size_t index = (size_t)(percentile * (float)(values.size() - 1));
  std::nth_element(values.begin(), values.begin() + index, values.end());
  return values[index];
}

// The model-space axis-aligned bounds of a posed volume, which is all the hull
// excursion check needs and is the one place per-shape extents are turned into
// a box.
void hitbox_bounds(const posed_hitbox_t &hitbox, linalg::vec3f &out_minimum,
                   linalg::vec3f &out_maximum)
{
  if (hitbox.shape == hitbox_shape_t::Box)
  {
    // The extent of an oriented box on each world axis is the sum of its
    // half-extents projected onto that axis.
    const linalg::vec3f center = hitbox.center();
    const linalg::vec3f reach{
        std::fabs(hitbox.frame.right.x) * hitbox.half_extents.x +
            std::fabs(hitbox.frame.up.x) * hitbox.half_extents.y +
            std::fabs(hitbox.frame.forward.x) * hitbox.half_extents.z,
        std::fabs(hitbox.frame.right.y) * hitbox.half_extents.x +
            std::fabs(hitbox.frame.up.y) * hitbox.half_extents.y +
            std::fabs(hitbox.frame.forward.y) * hitbox.half_extents.z,
        std::fabs(hitbox.frame.right.z) * hitbox.half_extents.x +
            std::fabs(hitbox.frame.up.z) * hitbox.half_extents.y +
            std::fabs(hitbox.frame.forward.z) * hitbox.half_extents.z};

    out_minimum = center - reach;
    out_maximum = center + reach;
    return;
  }

  // Round shapes: the two endpoints, grown by the radius. A cylinder's flat cap
  // is inside that, which is close enough for a bounds test.
  const linalg::vec3f radius{hitbox.radius, hitbox.radius, hitbox.radius};
  out_minimum = linalg::vec3f{std::min(hitbox.start.x, hitbox.end.x),
                              std::min(hitbox.start.y, hitbox.end.y),
                              std::min(hitbox.start.z, hitbox.end.z)} -
                radius;
  out_maximum = linalg::vec3f{std::max(hitbox.start.x, hitbox.end.x),
                              std::max(hitbox.start.y, hitbox.end.y),
                              std::max(hitbox.start.z, hitbox.end.z)} +
                radius;
}

} // namespace

const char *to_string(hitbox_shape_t shape)
{
  switch (shape)
  {
    case hitbox_shape_t::Sphere:   return "Sphere";
    case hitbox_shape_t::Capsule:  return "Capsule";
    case hitbox_shape_t::Cylinder: return "Cylinder";
    case hitbox_shape_t::Box:      return "Box";
    default:                       return "Unknown";
  }
}

std::optional<hitbox_shape_t> try_hitbox_shape_from_string(const char *text)
{
  for (uint32_t index = 0; index < (uint32_t)hitbox_shape_t::Count; ++index)
  {
    const hitbox_shape_t shape = (hitbox_shape_t)index;
    if (std::strcmp(to_string(shape), text) == 0)
      return shape;
  }
  return std::nullopt;
}

std::optional<hitbox_rig_t> try_resolve_hitbox_rig(const hitbox_rig_file_t &file,
                                                   const skeleton_t       &skeleton)
{
  if (file.skeleton_hash != 0 && file.skeleton_hash != skeleton.hash)
  {
    log_error("hitbox rig '{}' was authored against skeleton hash {:016x}, but '{}' hashes to "
              "{:016x}; the two disagree about which bones exist",
              file.name, file.skeleton_hash, skeleton.name, skeleton.hash);
    return std::nullopt;
  }

  hitbox_rig_t rig;
  rig.name          = file.name;
  rig.skeleton_name = file.skeleton_name;
  rig.skeleton_hash = file.skeleton_hash;
  rig.skeleton      = &skeleton;
  rig.volumes.reserve(file.volumes.size());

  for (const hitbox_volume_t &volume : file.volumes)
  {
    const int32_t start_bone = bone_index_of(skeleton, volume.start_bone);
    const int32_t end_bone   = bone_index_of(skeleton, volume.end_bone);
    if (start_bone < 0 || end_bone < 0)
    {
      log_error("hitbox volume '{}' names bone(s) skeleton '{}' does not have: '{}' -> '{}'",
                volume.name, skeleton.name, volume.start_bone, volume.end_bone);
      return std::nullopt;
    }

    rigged_hitbox_volume_t entry;
    entry.volume     = volume;
    entry.start_bone = (uint32_t)start_bone;
    entry.end_bone   = (uint32_t)end_bone;

    // Walk up from the end bone. The chain has to REACH the start bone: two
    // bones on different limbs would otherwise produce a volume through the
    // middle of the character, which is a plausible-looking wrong answer.
    int32_t walker = end_bone;
    while (walker >= 0)
    {
      const bool is_end_bone = walker == end_bone;
      if (!is_end_bone || start_bone == end_bone)
        entry.span_bones.push_back((uint32_t)walker);

      if (walker == start_bone)
        break;
      walker = skeleton.bones[walker].parent_index;
    }

    if (walker != start_bone)
    {
      log_error("hitbox volume '{}': '{}' is not an ancestor of '{}', so the span is not a bone "
                "chain",
                volume.name, volume.start_bone, volume.end_bone);
      return std::nullopt;
    }

    rig.volumes.push_back(std::move(entry));
  }

  return rig;
}

void compute_posed_hitboxes(const hitbox_rig_t &rig, Span<const linalg::mat4f> model_space,
                            Span<posed_hitbox_t> out)
{
  if (out.size() != rig.volumes.size())
    fatal_error("compute_posed_hitboxes: {} volumes, {} outputs", rig.volumes.size(), out.size());

  for (uint32_t index = 0; index < (uint32_t)rig.volumes.size(); ++index)
  {
    const rigged_hitbox_volume_t &entry  = rig.volumes[index];
    const hitbox_volume_t        &volume = entry.volume;

    if (entry.start_bone >= model_space.size() || entry.end_bone >= model_space.size())
      fatal_error("hitbox volume '{}' indexes bone {}/{} of a {}-matrix pose", volume.name,
                  entry.start_bone, entry.end_bone, model_space.size());

    posed_hitbox_t hitbox;
    hitbox.shape        = volume.shape;
    hitbox.radius       = volume.radius;
    hitbox.half_extents = volume.half_extents;
    hitbox.region       = volume.region;
    volume_placement(entry, model_space, hitbox.start, hitbox.end, hitbox.frame);

    // A sphere is one bone by construction, so its endpoints must not be able to
    // disagree even if a file names two.
    if (hitbox.shape == hitbox_shape_t::Sphere)
      hitbox.end = hitbox.start;

    out[index] = hitbox;
  }
}

void compute_bind_model_matrices(const skeleton_t &skeleton, Span<linalg::mat4f> out)
{
  if (out.size() != skeleton.bones.size())
    fatal_error("compute_bind_model_matrices: {} bones into {} matrices", skeleton.bones.size(),
                out.size());

  // inverse_bind[i] is by definition the inverse of bone i's bind-pose model
  // matrix, so the walk skinning.hpp does is not needed here -- one inverse per
  // bone is the whole answer.
  for (uint32_t index = 0; index < out.size(); ++index)
    out[index] = linalg::inverse_affine(skeleton.bones[index].inverse_bind);
}

float distance_outside_hitbox(const posed_hitbox_t &hitbox, const linalg::vec3f &point)
{
  switch (hitbox.shape)
  {
    case hitbox_shape_t::Sphere:
      return std::max(0.0f, linalg::length(point - hitbox.start) - hitbox.radius);

    case hitbox_shape_t::Capsule:
      return std::max(0.0f, distance_to_segment(point, hitbox.start, hitbox.end) - hitbox.radius);

    case hitbox_shape_t::Cylinder:
    {
      // Split into along-axis and perpendicular, clamp each to the surface, then
      // take the length of what is left -- which rounds the RIM rather than the
      // cap, and that is what makes it a cylinder and not a capsule.
      const linalg::vec3f axis   = hitbox.end - hitbox.start;
      const float         length = linalg::length(axis);
      if (length <= 1e-6f)
        return std::max(0.0f, linalg::length(point - hitbox.start) - hitbox.radius);

      const linalg::vec3f direction = axis * (1.0f / length);
      const linalg::vec3f relative  = point - hitbox.center();

      const float along        = linalg::dot(relative, direction);
      const float perpendicular = linalg::length(relative - direction * along);

      const float outside_along = std::max(0.0f, std::fabs(along) - length * 0.5f);
      const float outside_side  = std::max(0.0f, perpendicular - hitbox.radius);
      return std::sqrt(outside_along * outside_along + outside_side * outside_side);
    }

    case hitbox_shape_t::Box:
    {
      const linalg::vec3f relative = point - hitbox.center();
      const linalg::vec3f local{linalg::dot(relative, hitbox.frame.right),
                                linalg::dot(relative, hitbox.frame.up),
                                linalg::dot(relative, hitbox.frame.forward)};

      const linalg::vec3f outside{std::max(0.0f, std::fabs(local.x) - hitbox.half_extents.x),
                                  std::max(0.0f, std::fabs(local.y) - hitbox.half_extents.y),
                                  std::max(0.0f, std::fabs(local.z) - hitbox.half_extents.z)};
      return linalg::length(outside);
    }

    default:
      fatal_error("distance_outside_hitbox: shape {} is not a shape", (uint32_t)hitbox.shape);
  }
}

std::optional<hitbox_ray_hit_t> intersect_ray_hitbox(const posed_hitbox_t &hitbox,
                                                     const linalg::vec3f  &origin,
                                                     const linalg::vec3f  &direction)
{
  switch (hitbox.shape)
  {
    case hitbox_shape_t::Sphere:
      return intersect_ray_sphere_entry(origin, direction, hitbox.start, hitbox.radius);

    case hitbox_shape_t::Capsule:
    {
      // A capsule is the infinite cylinder cropped to the span, plus a
      // hemisphere at each end -- so it is literally those three tests, nearest
      // wins. The cropped body is tested first only because it is the common
      // case, not because it is nearer.
      std::optional<hitbox_ray_hit_t> nearest =
          intersect_ray_cylinder_side(origin, direction, hitbox.start, hitbox.end, hitbox.radius);
      keep_nearer(nearest,
                  intersect_ray_sphere_entry(origin, direction, hitbox.start, hitbox.radius));
      keep_nearer(nearest,
                  intersect_ray_sphere_entry(origin, direction, hitbox.end, hitbox.radius));
      return nearest;
    }

    case hitbox_shape_t::Cylinder:
    {
      std::optional<hitbox_ray_hit_t> nearest =
          intersect_ray_cylinder_side(origin, direction, hitbox.start, hitbox.end, hitbox.radius);
      keep_nearer(nearest, intersect_ray_cylinder_cap(origin, direction, hitbox.start, hitbox.end,
                                                      hitbox.radius, /*at_start=*/true));
      keep_nearer(nearest, intersect_ray_cylinder_cap(origin, direction, hitbox.start, hitbox.end,
                                                      hitbox.radius, /*at_start=*/false));
      return nearest;
    }

    case hitbox_shape_t::Box:
      return intersect_ray_oriented_box(origin, direction, hitbox.center(), hitbox.frame,
                                        hitbox.half_extents);

    default:
      fatal_error("intersect_ray_hitbox: shape {} is not a shape", (uint32_t)hitbox.shape);
  }
}

guesstimated_hitbox_from_bone_t guesstimate_hitbox_size(const mesh_asset_t &mesh, const skeleton_t &skeleton,
                                 const rigged_hitbox_volume_t &rigged)
{
  if (!mesh.is_skinned() || skeleton.bones.empty())
    return {};

  std::vector<linalg::mat4f> bind_model(skeleton.bones.size());
  compute_bind_model_matrices(skeleton, bind_model);

  linalg::vec3f  start;
  linalg::vec3f  end;
  hitbox_frame_t frame;
  volume_placement(rigged, bind_model, start, end, frame);

  const linalg::vec3f center = (start + end) * 0.5f;

  std::vector<float> from_axis;
  std::vector<float> along_right;
  std::vector<float> along_up;
  std::vector<float> along_forward;
  from_axis.reserve(mesh.vertices.size() / 4);

  for (size_t vertex_index = 0; vertex_index < mesh.vertices.size(); ++vertex_index)
  {
    const int32_t bone = dominant_bone(mesh.skin[vertex_index]);
    if (bone < 0)
      continue;

    const bool in_span = std::find(rigged.span_bones.begin(), rigged.span_bones.end(),
                                   (uint32_t)bone) != rigged.span_bones.end();
    if (!in_span)
      continue;

    const linalg::vec3f position = mesh.vertices[vertex_index].position;
    from_axis.push_back(distance_to_segment(position, start, end));

    const linalg::vec3f relative = position - center;
    along_right.push_back(std::fabs(linalg::dot(relative, frame.right)));
    along_up.push_back(std::fabs(linalg::dot(relative, frame.up)));
    along_forward.push_back(std::fabs(linalg::dot(relative, frame.forward)));
  }

  guesstimated_hitbox_from_bone_t guesstimated;
  guesstimated.radius       = percentile_of(from_axis, HITBOX_SIZE_PERCENTILE);
  guesstimated.half_extents = {percentile_of(along_right, HITBOX_SIZE_PERCENTILE),
                               percentile_of(along_up, HITBOX_SIZE_PERCENTILE),
                               percentile_of(along_forward, HITBOX_SIZE_PERCENTILE)};
  return guesstimated;
}

void guesstimate_hitbox_sizes(const mesh_asset_t &mesh, const skeleton_t &skeleton,
                              const hitbox_rig_t &rig, Span<guesstimated_hitbox_from_bone_t> out)
{
  if (out.size() != rig.volumes.size())
    fatal_error("guesstimate_hitbox_sizes: {} volumes, {} outputs", rig.volumes.size(), out.size());

  printf("[hitbox] guesstimated sizes for '%s' against mesh of %zu vertices\n", rig.name.c_str(),
         mesh.vertices.size());
  for (uint32_t index = 0; index < out.size(); ++index)
  {
    const hitbox_volume_t &volume = rig.volumes[index].volume;
    out[index] = guesstimate_hitbox_size(mesh, skeleton, rig.volumes[index]);

    printf("[hitbox]   %-12s %-8s %-12s -> %-12s  radius %6.2f (authored %6.2f)  extents "
           "%5.2f %5.2f %5.2f%s\n",
           volume.name.c_str(), to_string(volume.shape), volume.start_bone.c_str(),
           volume.end_bone.c_str(), out[index].radius, volume.radius, out[index].half_extents.x,
           out[index].half_extents.y, out[index].half_extents.z,
           out[index].radius == 0.0f ? "  (NO VERTICES)" : "");
  }
}

hitbox_rig_t make_hitbox_rig_template(const mesh_asset_t &mesh, const skeleton_t &skeleton)
{
  hitbox_rig_t rig;
  rig.name          = skeleton.name;
  rig.skeleton_name = skeleton.name;
  rig.skeleton_hash = skeleton.hash;
  rig.skeleton      = &skeleton;

  for (uint32_t index = 0; index < (uint32_t)skeleton.bones.size(); ++index)
  {
    const bone_t &bone = skeleton.bones[index];

    // A one-bone span has no length, so the template offers spheres: a capsule
    // between a bone and itself is the same shape with a name that lies.
    rigged_hitbox_volume_t entry;
    entry.volume     = {.name       = bone.name,
                        .shape      = hitbox_shape_t::Sphere,
                        .start_bone = bone.name,
                        .end_bone   = bone.name,
                        .region     = guess_region(bone.name)};
    entry.start_bone = index;
    entry.end_bone   = index;
    entry.span_bones = {index};

    const guesstimated_hitbox_from_bone_t guesstimated =
        guesstimate_hitbox_size(mesh, skeleton, entry);
    entry.volume.radius       = guesstimated.radius;
    entry.volume.half_extents = guesstimated.half_extents;
    rig.volumes.push_back(std::move(entry));
  }
  return rig;
}

hitbox_coverage_t compute_hitbox_coverage(const mesh_asset_t &mesh, const skeleton_t &skeleton,
                                          Span<const posed_hitbox_t> hitboxes, float tolerance)
{
  hitbox_coverage_t coverage;
  coverage.vertex_count = (uint32_t)mesh.vertices.size();
  coverage.uncovered_by_bone.assign(skeleton.bones.size(), 0);
  if (hitboxes.empty() || mesh.vertices.empty())
    return coverage;

  for (size_t vertex_index = 0; vertex_index < mesh.vertices.size(); ++vertex_index)
  {
    const linalg::vec3f position = mesh.vertices[vertex_index].position;

    float nearest_surface = std::numeric_limits<float>::max();
    for (const posed_hitbox_t &hitbox : hitboxes)
      nearest_surface = std::min(nearest_surface, distance_outside_hitbox(hitbox, position));

    if (nearest_surface <= tolerance)
      continue;

    const int32_t bone = mesh.is_skinned() ? dominant_bone(mesh.skin[vertex_index]) : -1;

    coverage.uncovered_vertex_count += 1;
    if (bone >= 0 && (size_t)bone < coverage.uncovered_by_bone.size())
      coverage.uncovered_by_bone[(size_t)bone] += 1;

    if (nearest_surface > coverage.worst_distance)
    {
      coverage.worst_distance = nearest_surface;
      coverage.worst_bone     = bone;
    }
  }

  return coverage;
}

hull_excursion_t compute_hull_excursion(Span<const posed_hitbox_t> hitboxes, float half_width,
                                        float height)
{
  hull_excursion_t worst;

  for (uint32_t index = 0; index < hitboxes.size(); ++index)
  {
    linalg::vec3f minimum;
    linalg::vec3f maximum;
    hitbox_bounds(hitboxes[index], minimum, maximum);

    // Y is one-sided at each end rather than symmetric: the hull stands ON the
    // feet, so 0 is the floor and `height` is the crown.
    const float distance = std::max({-half_width - minimum.x, maximum.x - half_width,
                                     -half_width - minimum.z, maximum.z - half_width,
                                     -minimum.y, maximum.y - height, 0.0f});

    if (distance > worst.distance)
    {
      worst.distance     = distance;
      worst.volume_index = (int32_t)index;
    }
  }
  return worst;
}

} // namespace assets
