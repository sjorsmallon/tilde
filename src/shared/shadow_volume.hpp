#pragma once

// A shadow volume: the convex region a caster piece keeps a cuts_geometry light from reaching, as
// collision reads it. The reveal cone's mechanism with a caster's silhouette for the cone's sides
// (shadow_volume_plan.md): geometry whose owner is `solid_only_in_shadow` is solid only where one
// touches, geometry whose owner is `erased_in_shadow` is passed wherever one holds the contact.

#include "aabb.hpp"
#include "array.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "movers.hpp"
#include "plane.hpp"
#include "span.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct Bounding_Volume_Hierarchy;

namespace entities
{
struct Entity;
struct Geometry_Owner_Entity;
}

namespace shared
{

struct Entity_System;

// A silhouette with more edges (plus the spot beam's planes that cut it), or a piece with more back-facing
// planes, than these casts nothing; the scene block carries exactly these slots per volume, side slots
// first (shadow_volume_plan.md ss2). The extra side slot is a point light's far cap.
inline constexpr uint32_t MAX_SHADOW_VOLUME_SIDES       = 24;
inline constexpr uint32_t SHADOW_VOLUME_SIDE_SLOTS      = MAX_SHADOW_VOLUME_SIDES + 1;
inline constexpr uint32_t MAX_SHADOW_VOLUME_BACK_PLANES = 12;
inline constexpr uint32_t MAX_SHADOW_VOLUME_PLANES      = SHADOW_VOLUME_SIDE_SLOTS + MAX_SHADOW_VOLUME_BACK_PLANES;
// How far the drawn far ring of a volume with no far cap is from the caster (cl_shadow_volume_debug).
inline constexpr float    SHADOW_VOLUME_DRAWN_REACH = 2048.f;

// Normals point OUT. A point is in shadow where it is inside every side plane (the pyramid the
// silhouette spans, and a point light's far cap) AND outside at least one back plane: the caster's
// own planes that face away from the light, which is where a ray through the caster leaves it. A
// point inside the pyramid but still in front of the caster's far surface is lit, which a near cap
// at one depth got wrong for anything flat seen at a grazing angle.
// The rings are the same volume as lines, for the debug draw only: side i runs near_ring[i], the
// caster corner on the silhouette, to far_ring[i] down its ray. The spot beam's clip is not in them.
struct shadow_volume_t
{
  Array<Plane, SHADOW_VOLUME_SIDE_SLOTS>        side_planes;
  uint32_t                                      side_plane_count = 0;
  Array<Plane, MAX_SHADOW_VOLUME_BACK_PLANES>   back_planes;
  uint32_t                                      back_plane_count = 0;
  Array<linalg::vec3f, MAX_SHADOW_VOLUME_SIDES> near_ring;
  Array<linalg::vec3f, MAX_SHADOW_VOLUME_SIDES> far_ring;
  uint32_t                                      side_count = 0;
  entity_uid_t                                  caster     = null_entity_uid;
  entity_uid_t                                  light      = null_entity_uid;
};

// What a light contributes: a point the rays leave, or a direction they all share.
struct shadow_light_t
{
  entity_uid_t  uid         = null_entity_uid;
  bool          directional = false;
  linalg::vec3f apex        = {0.f, 0.f, 0.f};
  linalg::vec3f direction   = {0.f, -1.f, 0.f};
  // How far a point light's volume reaches; 0 or less reaches forever. A directional light always does.
  float         range       = 0.f;
  // A spot's beam: a piece with no corner inside the cone about `direction` casts nothing. -2 is no beam.
  float         cosine_of_outer_angle = -2.f;
};

// The one fold from the three light types into a shadow light: a switched-on Point, Spot or
// Directional light whose `cuts_geometry` is set, at `pose`. Empty is "casts no volume".
[[nodiscard]] std::optional<shadow_light_t> try_shadow_light_from_entity(const entities::Entity& entity,
                                                                         const path_pose_t&      pose);

enum class shadow_cast_refusal_t : uint8_t
{
  None,
  // The light is beside or inside the piece: no bounded pyramid leaves it.
  Light_Beside_Caster,
  Too_Many_Planes,
  Outside_Spot_Beam,
};

struct shadow_cast_t
{
  shadow_cast_refusal_t refusal = shadow_cast_refusal_t::None;
  shadow_volume_t       volume;
};

// The volume one convex piece throws, from its planes and their polygons, or why it throws none.
[[nodiscard]] shadow_cast_t cast_shadow_volume(const shadow_light_t& light, Span<const Plane> piece_planes,
                                               Span<const std::vector<linalg::vec3>> corner_polygons,
                                               entity_uid_t                           caster);

[[nodiscard]] bool shadow_volume_contains_point(const shadow_volume_t& volume, const linalg::vec3f& point);

// Some of the box is inside: the solid receiver's test, conservative plane by plane as the reveal cone's.
[[nodiscard]] bool shadow_volume_touches_box(const shadow_volume_t& volume, const aabb_bounds_t& box);
[[nodiscard]] bool any_shadow_volume_touches_box(Span<const shadow_volume_t> volumes, const aabb_bounds_t& box);

// The whole box is inside: the hole's test, every corner as the erase cone's.
[[nodiscard]] bool shadow_volume_contains_box(const shadow_volume_t& volume, const aabb_bounds_t& box);
[[nodiscard]] bool any_shadow_volume_contains_box(Span<const shadow_volume_t> volumes, const aabb_bounds_t& box);

// A receiver is geometry whose owner is solid_only_in_shadow or erased_in_shadow. A receiver never casts.
[[nodiscard]] bool geometry_owner_receives_shadow(const entities::Geometry_Owner_Entity& owner);

// What one collect did, for shadow_volume_report / sv_shadow_volume_report: the collect runs every tick,
// so a refusal is counted here rather than logged.
struct shadow_volume_report_t
{
  uint32_t cutting_lights           = 0;
  uint32_t receiver_pieces          = 0;
  uint32_t caster_pieces            = 0;
  uint32_t cast                     = 0;
  uint32_t refused_beside_light     = 0;
  uint32_t refused_too_many_planes  = 0;
  uint32_t refused_outside_beam     = 0;
  uint32_t culled_reaching_nothing  = 0;
  uint32_t kept                     = 0;
};

// The counts, then one row per kept volume: which caster, which light, how many sides, where its near ring is.
[[nodiscard]] std::string describe_shadow_volume_report(const shadow_volume_report_t& report,
                                                        Span<const shadow_volume_t>   kept);

// Every cuts_geometry light against every caster piece: every static piece in the BVH whose owner is
// switched on and is not a receiver (a plain map brush has no owner and casts), and every piece of
// every mover, at the end-of-tick pose the movers were already cut at; a light that rides one is
// read at the same pose. A volume that touches no receiver's bounds is dropped: nothing would read
// it, and the scene block holds few.
shadow_volume_report_t collect_shadow_volumes(const Entity_System& system, const Bounding_Volume_Hierarchy& bvh,
                                              Span<const entity_uid_t> owner_of, Span<const mover_t> movers,
                                              const mover_rests_t& rests, std::vector<shadow_volume_t>& out);

} // namespace shared
