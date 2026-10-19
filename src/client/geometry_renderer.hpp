#pragma once

#include <optional>

#include "../shared/entity_uid.hpp"
#include "../shared/map/map_geometry.hpp"
#include "frame_builder.hpp"

#include <string>

namespace client
{

// Draw one piece of map geometry into a pass.
//
// One implementation for both regimes that used to draw geometry: the in-game
// static pass in Play_State and the editor's per-entity traits. Both had grown
// their own copy of "resolve the mesh path, else fall back to the kind's
// primitive", and they had already drifted (the editor cached generated
// generated brush meshes, the game regenerated one every frame).
//
// `uid` only names the cache slot for a generated mesh, so the same object
// doesn't get rebuilt once per view per frame.
//
// `materials` is the material table the object's faces index into --
// map_t::materials in the editor, game_session_t::materials in game. Passed at
// the call site rather than held here, because module state that must be set
// before the first draw is one more thing to forget; the caller holding the
// geometry already holds the table it belongs to.
//
// `moved_by` is a mover's world-from-rest matrix for geometry a mover owns, null for everything else;
// moved geometry casts into the dynamic shadow maps, since its baked shadow stayed at rest.
//
// How a TEAM WALL is drawn: in its team's colour, and as a ghost -- a fresnel rim,
// alpha-blended, casting no shadow, in place of its materials -- when the viewer
// is the team that walks through it. Solid and tinted when the viewer is not,
// so the wall says whose it is either way and is hidden from nobody.
struct team_wall_tint_t
{
  color_t color    = colors::white;
  bool    passable = false;
};

// The frame a timed shape over a piece of geometry is drawn in: its bounds centre, the owner's
// right and forward. Unarmed and unwiped; a caller with a timer sets those. A locked_wall face
// reads the frame for its keyhole whether or not a timer is wired, which is why it is built here
// and not only where the timer is.
renderer::clock_wipe_t geometry_wipe_frame(const shared::geometry_value_t& geometry,
                                           const linalg::quatf& orientation);

// Whether a draw compares the generated mesh's cache record against the live
// value. The editor's map_t has many writers (gizmo, undo, inspector, file), so
// it compares on every draw; a session's geometry is frozen for the session's
// lifetime, so it is compared once by refresh_stale_generated_geometry_meshes
// at build and trusted after.
enum class generated_mesh_check_t
{
  Compare_Every_Draw,
  Trusted_Since_Load,
};

// `map_geometry` is the list the object sits in, so a brush's seam with its
// neighbours is read the way collision reads it.
void draw_geometry(pass_builder_t &draws, const shared::geometry_value_t &geometry,
                   shared::entity_uid_t uid, Span<const std::string> materials,
                   const shared::lightmap_t &lightmap,
                   Span<const shared::map_geometry_t> map_geometry,
                   const linalg::mat4f* moved_by = nullptr,
                   const renderer::clock_wipe_t& clock_wipe = {},
                   std::optional<team_wall_tint_t> team_wall = {},
                   renderer::light_cut_t light_cut = renderer::light_cut_t::none,
                   bool casts_dynamic_shadow = false,
                   generated_mesh_check_t check = generated_mesh_check_t::Compare_Every_Draw);

// The one compare a frozen list gets: every entry that draws from a generated
// mesh is checked against its cache record and rebuilt where stale, so draws
// may then pass Trusted_Since_Load.
void refresh_stale_generated_geometry_meshes(Span<const shared::map_geometry_t> geometry,
                                             Span<const std::string> materials,
                                             const shared::lightmap_t &lightmap);

// Rebuild the cached mesh for an object whose GENERATED form just changed -- a
// brush point set or one of its face grids -- and re-upload it. Registers the
// mesh if this is the first time. The cache is keyed by uid only, so it cannot
// notice an edit by itself: call this after one. The draw path checks anyway --
// see the cache record in the .cpp -- so a missed call costs a frame, not a
// stale solid that never catches up.
//
// Takes the whole geometry rather than one kind, because both kinds that
// generate a mesh need exactly this, and a per-kind entry point is two things
// to remember instead of one.
//
// A brush's seam and its smoothed normals depend on its neighbours, so a changed
// brush also rebuilds every subdivided or smoothed brush of `map_geometry`
// whose bounds touch it: their values
// did not change, which is exactly why the draw path's check would miss them.
void refresh_generated_geometry_mesh(const shared::geometry_value_t &geometry,
                                     shared::entity_uid_t uid,
                                     Span<const std::string> materials,
                                     const shared::lightmap_t &lightmap,
                                     Span<const shared::map_geometry_t> map_geometry);

// The hull of a brush as the cache last built it, refreshed first when the brush
// moved, exactly as draw_geometry refreshes the mesh. Empty when the points do
// not form a solid. The one way a per-frame draw gets a hull: building one is
// O(n^4) in the point count.
const std::optional<shared::brush_polyhedron_t> &
get_cached_brush_hull(const shared::geometry_value_t &geometry, shared::entity_uid_t uid,
                      Span<const std::string> materials, const shared::lightmap_t &lightmap,
                      Span<const shared::map_geometry_t> map_geometry,
                      generated_mesh_check_t check = generated_mesh_check_t::Compare_Every_Draw);

} // namespace client
