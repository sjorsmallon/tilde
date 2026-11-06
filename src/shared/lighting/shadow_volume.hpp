#pragma once

// A shadow volume: the convex region a caster piece keeps a cuts_geometry light from reaching, as
// collision reads it. The reveal cone's mechanism with a caster's silhouette for the cone's sides
// (shadow_volume_plan.md): geometry whose owner is `solid_only_in_shadow` is solid only where one
// touches, geometry whose owner is `erased_in_shadow` is passed wherever one holds the contact.

#include "aabb.hpp"
#include "array.hpp"
#include "assets/asset_id.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "collision/movers.hpp"
#include "plane.hpp"
#include "reveal_light.hpp"
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
struct projector_mask_cache_t;
struct player_shadow_caster_t;

// A silhouette with more edges (plus the spot beam's planes that cut it), or a piece with more faces toward
// the light, than these casts nothing; the scene block carries exactly these slots per volume, side slots
// first (shadow_volume_plan.md ss2). The extra side slot is a point light's far cap.
inline constexpr uint32_t MAX_SHADOW_VOLUME_SIDES       = 28;
inline constexpr uint32_t SHADOW_VOLUME_SIDE_SLOTS      = MAX_SHADOW_VOLUME_SIDES + 1;
inline constexpr uint32_t MAX_SHADOW_VOLUME_LIGHT_FACING_PLANES = 14;
inline constexpr uint32_t MAX_SHADOW_VOLUME_PLANES      = SHADOW_VOLUME_SIDE_SLOTS + MAX_SHADOW_VOLUME_LIGHT_FACING_PLANES;
// How far past the caster a volume with no far cap is drawn (cl_shadow_volume_debug).
inline constexpr float    SHADOW_VOLUME_DRAWN_REACH = 2048.f;

struct shadow_volume_edge_t
{
  linalg::vec3f a;
  linalg::vec3f b;
};

// Normals point OUT. A point is in shadow where it is inside every side plane (the pyramid the
// silhouette spans, and a point light's far cap) AND inside every front plane: the caster's own
// planes that face the light, which is where a ray from the light enters it. From there on, through
// the caster and behind it, is shadow, so the shadow is one convex region and nothing inside a caster
// is lit. A near cap at one depth got the entry wrong for anything flat seen at a grazing angle; the
// front planes are the entry exactly.
// The rings are the silhouette's pyramid as lines: side i runs near_ring[i], the caster corner on the
// silhouette, to far_ring[i] down its ray. The spot beam's clip is not in them.
// The debug draw asks collect_shadow_volume_edges for the volume as it is, from the planes.
struct shadow_volume_t
{
  Array<Plane, SHADOW_VOLUME_SIDE_SLOTS>        side_planes;
  uint32_t                                      side_plane_count = 0;
  Array<Plane, MAX_SHADOW_VOLUME_LIGHT_FACING_PLANES>   light_facing_planes;
  uint32_t                                      light_facing_plane_count = 0;
  Array<linalg::vec3f, MAX_SHADOW_VOLUME_SIDES> near_ring;
  Array<linalg::vec3f, MAX_SHADOW_VOLUME_SIDES> far_ring;
  uint32_t                                      side_count = 0;
  entity_uid_t                                  caster     = null_entity_uid;
  entity_uid_t                                  light      = null_entity_uid;
  // The light's cuts_geometry: only such a volume is read by collision; one thrown for a beam alone cuts the beam.
  bool                                          cuts_geometry = true;
  // The light's solid_shadows: which of this volume is a mover (solid_shadows.hpp), carried for the piece build.
  entities::Solid_Shadows                       solid_shadows = entities::Solid_Shadows::None;
  // Some receiver's bounds are within it. The collect puts the volumes that cut geometry AND reach one first,
  // so the renderer's few slots hold what changes the floor before what only darkens a beam.
  bool                                          reaches_receiver = false;
  // Which piece cast it, in the collect's visiting order (static pieces, then every mover's): the solid shadow
  // cache's key beside the light (solid_shadows.hpp); an owner uid cannot be, two unowned brushes share one.
  uint32_t                                      caster_piece = 0;
  // The caster's corners: the solid shadow's reach box is around them (solid_shadows.hpp).
  aabb_bounds_t                                 caster_bounds = {};
  // Where the rays come from, so the pyramid's edges can be followed back from the near ring.
  linalg::vec3f                                 light_apex        = {0.f, 0.f, 0.f};
  linalg::vec3f                                 light_direction   = {0.f, -1.f, 0.f};
  bool                                          light_directional = false;
  // The side slots are the silhouette's planes [0, ring_plane_count), then the far cap when `has_far_cap`,
  // then a spot beam's cone planes that cut the pyramid, which the beam pass leaves out of its margins.
  uint32_t                                      ring_plane_count = 0;
  bool                                          has_far_cap      = false;
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
  // A projector's frame (lighting.hpp's compute_projector_frame_planes): the four planes through the apex,
  // outward, which cut its volumes as a spot's cone does; a piece outside them casts nothing. 0 is no frame.
  Array<Plane, 4> frame_planes;
  uint32_t        frame_plane_count = 0;
  // The frame's corners one unit along the axis (compute_projector_frame_corners: bottom-left, bottom-right,
  // top-right, top-left) and what it shows: the picture's dark is cast as pieces on the glass
  // (projector_mask.hpp). Missing is a plain white frame, casting nothing of its own.
  Array<linalg::vec3f, 4> frame_corners_at_unit_distance;
  assets::texture_asset   picture = assets::texture_asset::Missing;
  // The light's cuts_geometry, carried onto every volume it throws.
  bool          cuts_geometry         = true;
  // The spot draws its beam (spot_beam_plan.md ss5) or is solid as one (solid_beams.hpp): its volumes are kept
  // whether or not one reaches a receiver, since the beam itself reads them.
  bool          keeps_every_volume    = false;
  // The light's solid_shadows, carried onto every volume it throws (solid_shadows.hpp).
  entities::Solid_Shadows solid_shadows = entities::Solid_Shadows::None;
};

// A directed light's sides, built once per light and read for every piece: a spot's cone
// (planes_of_reveal_cone) or a projector's frame. A point or directional light has none.
struct shadow_light_clip_t
{
  std::optional<reveal_cone_planes_t>  beam;
  Array<Plane, REVEAL_CONE_SIDE_COUNT> sides;
  uint32_t                             side_count = 0;
};
static_assert(REVEAL_CONE_SIDE_COUNT >= 4, "a projector's four frame planes must fit the clip's sides");

[[nodiscard]] shadow_light_clip_t compute_shadow_light_clip(const shadow_light_t& light);

// Within the light's reach and not wholly outside any of its sides, conservatively, plane by plane: the
// broad phase every piece passes before it is cast. A light with no sides touches every box.
[[nodiscard]] bool shadow_light_touches_box(const shadow_light_t& light, const shadow_light_clip_t& clip,
                                            const aabb_bounds_t& box);

// Where a light's reach ends, as the plane that caps its volumes: a spot's or a projector's is square to its
// direction, a point light's to the ray through `center` (the caster's), and a directional light or one with
// no range has none. Normal points away from the light.
[[nodiscard]] std::optional<Plane> try_shadow_light_far_cap(const shadow_light_t& light, const linalg::vec3f& center);

// The one fold from the four light types into a shadow light: a switched-on Point, Spot, Projector or
// Directional light whose `cuts_geometry` or `solid_shadows` is set, or a Spot whose `beam` or `solid_beam`
// is, at `pose`.
// Empty is "casts no volume". A `solid_shadows` light keeps every volume: each is a mover (solid_shadows.hpp).
[[nodiscard]] std::optional<shadow_light_t> try_shadow_light_from_entity(const entities::Entity& entity,
                                                                         const path_pose_t&      pose);

enum class shadow_cast_refusal_t : uint8_t
{
  None,
  // Every face of the piece faces away from the light, or none does: the light is inside it.
  Light_Inside_Caster,
  // The faces away share fewer than three edges with the faces toward: the piece's face polygons do
  // not meet at their corners, so no silhouette closes.
  Silhouette_Open,
  // A silhouette corner is at the light, or its edge runs through the light: no side plane.
  Light_On_Silhouette,
  Too_Many_Planes,
  // No corner of the piece is inside the spot's cone or the projector's frame: nothing of it is lit.
  Outside_Spot_Beam,
};

[[nodiscard]] const char* describe_shadow_cast_refusal(shadow_cast_refusal_t refusal);

// One refusal, for the readout: which piece (its key in the collect's order), under which light, why.
struct refused_piece_t
{
  uint32_t              piece  = 0;
  entity_uid_t          light  = null_entity_uid;
  linalg::vec3f         center = {0.f, 0.f, 0.f};
  shadow_cast_refusal_t reason = shadow_cast_refusal_t::None;
};

struct shadow_cast_t
{
  shadow_cast_refusal_t refusal = shadow_cast_refusal_t::None;
  shadow_volume_t       volume;
};

// The volume one convex piece throws, from its planes and their polygons, or why it throws none. The
// shadow begins at the piece's lit faces, so the piece's own body is in it and a beam ends on its face.
[[nodiscard]] shadow_cast_t cast_shadow_volume(const shadow_light_t& light, Span<const Plane> piece_planes,
                                               Span<const std::vector<linalg::vec3>> corner_polygons,
                                               entity_uid_t                           caster);

// The shadow of a piece that is itself erased in shadow, APPENDED to `out`: its own volume must not erase
// it, and where it is gone the light passes, so the shadow begins BEHIND it. Behind a convex piece is not
// convex (a ray leaves through whichever face turned away it reaches), so it is one volume per face turned
// away, the pyramid beyond that face, whose union is exactly the shadow and holds no point of the piece.
[[nodiscard]] shadow_cast_refusal_t cast_shadow_volumes_behind_caster(const shadow_light_t& light,
                                                                     Span<const Plane>     piece_planes,
                                                                     Span<const std::vector<linalg::vec3>> corner_polygons,
                                                                     entity_uid_t                  caster,
                                                                     std::vector<shadow_volume_t>& out);

[[nodiscard]] bool shadow_volume_contains_point(const shadow_volume_t& volume, const linalg::vec3f& point);

// Some of the box is inside: the solid receiver's test, conservative plane by plane as the reveal cone's.
[[nodiscard]] bool shadow_volume_touches_box(const shadow_volume_t& volume, const aabb_bounds_t& box);
[[nodiscard]] bool any_shadow_volume_touches_box(Span<const shadow_volume_t> volumes, const aabb_bounds_t& box);

// The whole box is inside: the hole's test, every corner as the erase cone's.
[[nodiscard]] bool shadow_volume_contains_box(const shadow_volume_t& volume, const aabb_bounds_t& box);
[[nodiscard]] bool any_shadow_volume_contains_box(Span<const shadow_volume_t> volumes, const aabb_bounds_t& box);

// A receiver is geometry whose owner is solid_only_in_shadow or erased_in_shadow. A solid_only_in_shadow
// receiver never casts: it is there only where no light is. An erased_in_shadow receiver casts from behind
// itself (cast_shadow_volumes_behind_caster): where it is lit it is there and blocks the light.
[[nodiscard]] bool geometry_owner_receives_shadow(const entities::Geometry_Owner_Entity& owner);

// What one collect did, for shadow_volume_report / sv_shadow_volume_report: the collect runs every tick,
// so a refusal is counted here rather than logged.
struct shadow_volume_report_t
{
  uint32_t cutting_lights           = 0;
  uint32_t receiver_pieces          = 0;
  uint32_t caster_pieces            = 0;
  // Dark rectangles of projector pictures, cast after the pieces (projector_mask.hpp).
  uint32_t mask_pieces              = 0;
  // Players, cast into the solid_shadows lights alone (player_shadow_casters.hpp).
  uint32_t player_pieces            = 0;
  uint32_t cast                     = 0;
  uint32_t refused_inside_caster    = 0;
  uint32_t refused_open_silhouette  = 0;
  uint32_t refused_on_silhouette    = 0;
  uint32_t refused_too_many_planes  = 0;
  uint32_t refused_outside_beam     = 0;
  uint32_t culled_reaching_nothing  = 0;
  uint32_t kept                     = 0;
  // Every refusal but the expected one (outside the spot's beam), by piece.
  std::vector<refused_piece_t> refused;
};

// The counts, then one row per kept volume: which caster, which light, how many sides, where its near ring is.
[[nodiscard]] std::string describe_shadow_volume_report(const shadow_volume_report_t& report,
                                                        Span<const shadow_volume_t>   kept);

// Every shadow light against every caster piece: every static piece in the BVH whose owner is
// switched on and is not a receiver (a plain map brush has no owner and casts), and every piece of
// every mover but a light's own (a solid beam, a solid shadow: light and dark cast no shadow), at the end-of-tick pose the movers were already cut at; a light that rides one is
// read at the same pose. After the movers, every player's piece (`player_casters`), into the
// solid_shadows lights alone: a player darkens no shadow platform.
// A volume that touches no receiver's bounds is dropped unless its light draws
// a beam the volume cuts: nothing else would read it, and the scene block holds few. After every piece, the
// dark of each projector's picture casts as pieces on its glass, read from `projector_masks` and computed
// into it on the first ask (projector_mask.hpp). The kept list is
// ordered, stably, with the volumes that cut geometry and reach a receiver first: the renderer's cut draws
// (reveal.glsl) read the first MAX_SCENE_SHADOW_VOLUMES, and a beam's own volumes must never push a hole or a
// platform past that.
// APPENDS the volume's side polytope as lines (its silhouette pyramid cut by the beam's cone and its far
// cap, exactly the planes collision reads), bounded `reach` past the caster where it is
// open. The back planes are not drawn: they are the caster's own faces.
void collect_shadow_volume_edges(const shadow_volume_t& volume, float reach, std::vector<shadow_volume_edge_t>& out);

shadow_volume_report_t compute_shadow_volumes(const Entity_System& system, const Bounding_Volume_Hierarchy& bvh,
                                              Span<const entity_uid_t> owner_of, Span<const mover_t> movers,
                                              Span<const player_shadow_caster_t> player_casters,
                                              const mover_rests_t& rests, projector_mask_cache_t& projector_masks,
                                              std::vector<shadow_volume_t>& out);

} // namespace shared
