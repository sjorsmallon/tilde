#pragma once

// The dark of a projector's picture made SOLID (projector_plan.md ss2c): the picture's dark as convex quads of
// uv, each a caster piece on the projector's GLASS, cast through compute_shadow_volumes as any caster. The
// volume is the pyramid from the lens through the quad, cut to the frame and capped at the range, so the shadow
// map draws nothing new (the picture is already dark there) and ss1 makes it solid. The picture is a
// Projected_Image_Entity's texture and text (projected_image.hpp), replicated, so both ends cover the same
// quads; an image's mask is kept per session and recomputed when its picture changes.

#include "array.hpp"
#include "assets/asset_id.hpp"
#include "assets/asset_types.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "map/map_geometry.hpp"
#include "span.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace shared
{

struct shadow_light_t;
struct shadow_volume_t;
struct scene_light_t;

// A texel whose mean of r, g and b is below this fraction of white is dark; the shader multiplies the same
// bytes into the light, so the solid dark is where the drawn light is below a quarter.
inline constexpr float    PROJECTOR_MASK_DARK_THRESHOLD = 0.25f;
// The picture is a grid of at most this many cells a side: the long side of the frame takes them all and the
// short side as many as the aspect leaves (compute_projector_mask_grid_for_aspect); the footprint of the things
// in the light reads a square grid of them. A cell is dark when its texels' mean is.
inline constexpr uint32_t PROJECTOR_MASK_GRID_LIMIT = 64;
// More dark rectangles than this and the rest of the picture is dark but not solid, logged naming the image.
inline constexpr uint32_t MAX_PROJECTOR_MASK_RECTANGLES = 48;
// Where the glass is: the distance along the axis at which the frame is this tall from its centre, so a grid
// cell is a few units wide whatever the angle, well above the hull's weld epsilon. Capped at half the range.
inline constexpr float PROJECTOR_MASK_GLASS_HALF_HEIGHT = 16.f;

// A convex quad in picture uv: u runs left to right, v runs top to bottom, as the texture's rows do and as the
// frame's matrix maps them (projector_shadow_projection). The corners go round the quad in either order; a
// triangle repeats one. The one shape the ball bounces off and the glass casts; an axis-aligned one through
// make_projector_mask_quad, whose corners run top-left, top-right, bottom-right, bottom-left.
struct projector_mask_quad_t
{
  Array<linalg::vec2f, 4> corners;
};

[[nodiscard]] constexpr projector_mask_quad_t make_projector_mask_quad(float u0, float v0, float u1, float v1)
{
  return projector_mask_quad_t{.corners = {{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}}};
}

// The cells a projector's frame is read on: PROJECTOR_MASK_GRID_LIMIT along its long side, the short side
// rounded from the aspect (width over height), never below one.
struct projector_mask_grid_t
{
  uint32_t columns = 0;
  uint32_t rows    = 0;
};
[[nodiscard]] projector_mask_grid_t compute_projector_mask_grid_for_aspect(float aspect);

// The mask of one image's picture (projected_image.hpp): `image` and `picture_hash` are what the cache keys on.
struct projector_mask_t
{
  entity_uid_t                            image        = null_entity_uid;
  uint64_t                                picture_hash = 0;
  uint32_t                                columns      = 0;
  uint32_t                           rows         = 0;
  std::vector<projector_mask_quad_t> quads;
  // The texture's grid held more dark than MAX_PROJECTOR_MASK_RECTANGLES covered.
  bool                               capped = false;
};

// A grid's dark cells covered greedily, each rectangle the widest run from the first uncovered dark cell in
// row order, grown down while every cell of the next row's span is dark and uncovered, appended as quads;
// `dark` is cleared where covered. True when MAX_PROJECTOR_MASK_RECTANGLES ran out with dark left.
[[nodiscard]] bool cover_dark_cells_with_rectangles(std::vector<bool>& dark, uint32_t columns, uint32_t rows,
                                                    std::vector<projector_mask_quad_t>& out);

// `picture` read on a `columns` by `rows` grid, a cell dark when the mean of its texels' r, g and b is below
// `dark_threshold` of white. Row-major, row 0 the texture's top row.
[[nodiscard]] std::vector<bool> compute_dark_cells_of_picture(const assets::texture_asset_t& picture,
                                                              uint32_t columns, uint32_t rows,
                                                              float dark_threshold);

// The dark cells of a `columns` by `rows` grid as rectangles, covered as above; `image` and `picture_hash` are
// left for the caller.
[[nodiscard]] projector_mask_t compute_projector_mask_from_cells(std::vector<bool> dark, uint32_t columns,
                                                                 uint32_t rows);

// The shadow of the THINGS in a projector's light on its picture (projector_plan.md ss3b): the footprint of each
// of its shadow volumes whose caster casts from the lens (an owner marked casts_solid_from_lens, a mover, a
// player), appended to `out` as rectangles of the frame's grid (compute_projector_mask_grid_for_aspect). A
// volume's silhouette planes pass through the lens, so each is a half-plane of picture uv and a cell is dark
// when its centre is inside every one; where the volume begins is not read. The room, an unmarked caster,
// throws nothing: it is what the picture lands on. `cells` is the caller's grid scratch. True when the cover
// was capped.
[[nodiscard]] bool compute_projector_shadow_footprint(entity_uid_t projector, const scene_light_t& light,
                                                      const linalg::mat4f&                picture_matrix,
                                                      Span<const shadow_volume_t>         volumes,
                                                      std::vector<bool>&                  cells,
                                                      std::vector<projector_mask_quad_t>& out);

// One mask per image, found and recomputed by find_or_compute_projector_mask_for_image (projected_image.hpp).
struct projector_mask_cache_t
{
  std::vector<projector_mask_t> masks;
};

// The point of the frame at picture uv (u, v), `distance` along the axis from the lens.
[[nodiscard]] linalg::vec3f projector_frame_point(const shadow_light_t& light, float u, float v, float distance);

// Where a projector's glass is (PROJECTOR_MASK_GLASS_HALF_HEIGHT), along its axis.
[[nodiscard]] float projector_mask_glass_distance(const shadow_light_t& light);

// The quad as a caster: a slab of the frame's pyramid from the glass a quarter further out, convex, or empty
// when the quad is too thin to close a hull.
[[nodiscard]] std::optional<collision_piece_t> try_build_projector_mask_piece(const shadow_light_t&        light,
                                                                              const projector_mask_quad_t& quad);

} // namespace shared
