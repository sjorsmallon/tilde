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
  // The light's cuts_geometry: only such a volume is read by collision; one thrown for a beam alone cuts the beam.
  bool                                          cuts_geometry = true;
  // Which piece cast it, in the collect's visiting order (static pieces, then every mover's), so the drawn
  // volume's occluders can leave the caster itself out; an owner uid cannot, two unowned brushes share one.
  uint32_t                                      caster_piece = 0;
  // The caster's corners: the drawn body runs from here to what it lands on, which bounds it for beam.frag.
  aabb_bounds_t                                 caster_bounds = {};
  // Where the rays come from, so the pyramid's edges can be followed back from the near ring.
  linalg::vec3f                                 light_apex        = {0.f, 0.f, 0.f};
  linalg::vec3f                                 light_direction   = {0.f, -1.f, 0.f};
  bool                                          light_directional = false;
};

// What a DRAWN volume lands on or is stopped by (shadow_volume_plan.md ss5): a piece the volume touches, as
// the pyramid it spans from the light (its silhouette sides and the spot's clip, never the range: the rays
// that hit it) and its planes that face the light, behind all of which is behind the piece. The drawn body
// is the volume's part inside the pyramid of some piece that `receives` and behind no piece, for every
// drawn volume whose bit is set in `volume_bits`, bit v for the v-th drawn volume. Collision never reads
// one: behind the wall a shadow lands on is still the caster's shadow, only not one worth a picture.
struct shadow_occluder_t
{
  Array<Plane, SHADOW_VOLUME_SIDE_SLOTS>      pyramid_planes;
  uint32_t                                    pyramid_plane_count = 0;
  Array<Plane, MAX_SHADOW_VOLUME_BACK_PLANES> front_planes;
  uint32_t                                    front_plane_count = 0;
  uint32_t                                    volume_bits       = 0;
  // A receiver: the shadow on it is a platform or a hole, so the body is drawn down to it. A plain piece
  // only stops the body.
  bool                                        receives          = false;
  entity_uid_t                                light             = null_entity_uid;
  // The piece's corners, cut to a spot's reach as the pyramid is: with the caster's, the drawn body's bounds.
  aabb_bounds_t                               piece_bounds      = {};
};
// Bit of the scene block's occluder word that carries `receives`; the volume bits are below it.
inline constexpr uint32_t SHADOW_OCCLUDER_RECEIVES_BIT = 1u << 31;

// Inside every pyramid plane: on a ray from the light that hits the piece.
[[nodiscard]] bool shadow_occluder_pyramid_contains_point(const shadow_occluder_t& occluder, const linalg::vec3f& point);
// In the pyramid and inside every front plane: from the piece's lit surface onward.
[[nodiscard]] bool shadow_occluder_is_behind_point(const shadow_occluder_t& occluder, const linalg::vec3f& point);

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
  // The light's cuts_geometry, carried onto every volume it throws.
  bool          cuts_geometry         = true;
  // The spot draws its beam (spot_beam_plan.md ss5): its volumes are kept whether or not one reaches a receiver.
  bool          draws_beam            = false;
};

// Where a light's reach ends, as the plane that caps its volumes: a spot's is square to its direction, a
// point light's to the ray through `center` (the caster's), and a directional light or one with no range
// has none. Normal points away from the light.
[[nodiscard]] std::optional<Plane> try_shadow_light_far_cap(const shadow_light_t& light, const linalg::vec3f& center);

// The one fold from the three light types into a shadow light: a switched-on Point, Spot or
// Directional light whose `cuts_geometry` is set, or a Spot whose `beam` is, at `pose`. Empty is "casts no volume".
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

// The occluder one convex piece is under `light`, with `volume_bits` set, or empty when the piece casts no
// volume (the same refusals as cast_shadow_volume), its planes outrun the slots, or a spot's reach ends
// before it. A spot's piece is first cut to the spot's far cap, so the pyramid holds only the rays that hit
// it within reach: the part of a floor past the range is landed on by nothing. A point light's cap is per
// caster, so its pieces are not cut.
[[nodiscard]] std::optional<shadow_occluder_t> try_cast_shadow_occluder(const shadow_light_t& light,
                                                                        Span<const Plane>     piece_planes,
                                                                        Span<const std::vector<linalg::vec3>> corner_polygons,
                                                                        uint32_t volume_bits, bool receives);

// Where drawn volume `volume`'s body can be: inside its sides and inside the box around its caster and every
// receiver it lands on (the occluders that `receives` with `volume_bit` set), as the bounds of that polytope.
// A floor the shadow lands on is room-sized; the pyramid cut to the floor's slab is the shadow on it. Empty
// when the volume lands on nothing, which draws nothing.
[[nodiscard]] std::optional<aabb_bounds_t> try_compute_drawn_shadow_body_bounds(const shadow_volume_t&       volume,
                                                                                Span<const shadow_occluder_t> occluders,
                                                                                uint32_t                      volume_bit);

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
// read at the same pose. A volume that touches no receiver's bounds is dropped unless its light draws
// a beam the volume cuts: nothing else would read it, and the scene block holds few.
shadow_volume_report_t collect_shadow_volumes(const Entity_System& system, const Bounding_Volume_Hierarchy& bvh,
                                              Span<const entity_uid_t> owner_of, Span<const mover_t> movers,
                                              const mover_rests_t& rests, std::vector<shadow_volume_t>& out);

// What the drawn volumes `drawn` (at most 31, in the order the scene block holds them) land on and are
// stopped by: every switched-on piece a volume touches other than its own caster becomes one occluder per
// light, the bits of that light's volumes it touches set, `receives` when the piece is a receiver. Returns
// how many pieces were skipped for outrunning the plane slots.
uint32_t collect_shadow_occluders(const Entity_System& system, const Bounding_Volume_Hierarchy& bvh,
                                  Span<const entity_uid_t> owner_of, Span<const mover_t> movers,
                                  const mover_rests_t& rests, Span<const shadow_volume_t> drawn,
                                  std::vector<shadow_occluder_t>& out);

} // namespace shared
