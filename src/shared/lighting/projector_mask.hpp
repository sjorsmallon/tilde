#pragma once

// The dark of a projector's picture made SOLID (projector_plan.md ss2c): the picture's dark texels, as
// rectangles of uv, each a caster piece on the projector's GLASS, cast through compute_shadow_volumes as any
// caster. The volume is the pyramid from the lens through the rectangle, cut to the frame and capped at the
// range, so the shadow map draws nothing new (the picture is already dark there) and ss1 makes it solid.
// A texture's mask is computed once and kept per session; the texture is an asset, hashed in the handshake,
// so both ends compute the same rectangles from the same texels.

#include "array.hpp"
#include "assets/asset_id.hpp"
#include "assets/asset_types.hpp"
#include "linalg.hpp"
#include "map/map_geometry.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace shared
{

struct shadow_light_t;

// A texel whose mean of r, g and b is below this fraction of white is dark; the shader multiplies the same
// bytes into the light, so the solid dark is where the drawn light is below a quarter.
inline constexpr float    PROJECTOR_MASK_DARK_THRESHOLD = 0.25f;
// The picture is read on a grid of at most this many cells a side (a smaller texture on its own texels); a
// cell is dark when its texels' mean is.
inline constexpr uint32_t PROJECTOR_MASK_GRID_LIMIT = 64;
// More dark rectangles than this and the rest of the picture is dark but not solid, logged naming the texture.
inline constexpr uint32_t MAX_PROJECTOR_MASK_RECTANGLES = 48;
// Where the glass is: the distance along the axis at which the frame is this tall from its centre, so a grid
// cell is a few units wide whatever the angle, well above the hull's weld epsilon. Capped at half the range.
inline constexpr float PROJECTOR_MASK_GLASS_HALF_HEIGHT = 16.f;

// In picture uv: u runs left to right, v runs top to bottom, as the texture's rows do and as the frame's
// matrix maps them (projector_shadow_projection). Half open on the grid, so neighbouring rectangles abut.
struct projector_mask_rectangle_t
{
  float u0 = 0.f;
  float v0 = 0.f;
  float u1 = 0.f;
  float v1 = 0.f;
};

struct projector_mask_t
{
  assets::texture_asset                   texture = assets::texture_asset::Missing;
  uint32_t                                columns = 0;
  uint32_t                                rows    = 0;
  std::vector<projector_mask_rectangle_t> rectangles;
  // The grid held more dark than MAX_PROJECTOR_MASK_RECTANGLES covered.
  bool                                    capped = false;
};

// The dark of `picture` as rectangles: the grid's dark cells covered greedily, each rectangle the widest run
// from the first uncovered dark cell in row order, grown down while every cell of the next row's span is dark
// and uncovered.
[[nodiscard]] projector_mask_t compute_projector_mask_from_picture(const assets::texture_asset_t& picture,
                                                                   assets::texture_asset          texture,
                                                                   float dark_threshold);

struct projector_mask_cache_t
{
  std::vector<projector_mask_t> masks;
};

// The mask of `texture`, computed from the asset on the first ask and kept. Missing has an empty mask.
[[nodiscard]] const projector_mask_t& find_or_compute_projector_mask(projector_mask_cache_t& cache,
                                                                     assets::texture_asset   texture);

// The point of the frame at picture uv (u, v), `distance` along the axis from the lens.
[[nodiscard]] linalg::vec3f projector_frame_point(const shadow_light_t& light, float u, float v, float distance);

// Where a projector's glass is (PROJECTOR_MASK_GLASS_HALF_HEIGHT), along its axis.
[[nodiscard]] float projector_mask_glass_distance(const shadow_light_t& light);

// The rectangle as a caster: a slab of the frame's pyramid from the glass a quarter further out, convex, or
// empty when the rectangle is too thin to close a hull.
[[nodiscard]] std::optional<collision_piece_t> try_build_projector_mask_piece(const shadow_light_t&             light,
                                                                              const projector_mask_rectangle_t& rectangle);

} // namespace shared
