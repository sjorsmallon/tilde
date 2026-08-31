#pragma once

#include "entity_uid.hpp"
#include "linalg.hpp"
#include "plane.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace shared
{
// Building one is six steps, and build_lightmap_charts runs them in this order:
//
//   1. basis      brush_face_grid_tangents(normal) -> orthonormal u, v in the
//                 face's plane. Derived from the normal alone, so a rebake
//                 reproduces it.
//   2. project    (dot(p - reference, u), dot(p - reference, v)) per vertex.
//                 Two coordinates rather than three because two basis vectors
//                 span the plane every vertex already lies in; the third would
//                 be dot(p - reference, normal), which is 0 by construction.
//                 u and v are unit length, so the result is in WORLD UNITS and
//                 the flattening is an isometry -- texel density stays uniform.
//   3. bounds     the 2D min and max over those vertices.
//   4. anchor     subtract the min, so the chart starts at (0, 0) and its size
//                 is the face's extent rather than its distance from the world
//                 origin. The min is first snapped DOWN to a texel boundary, so
//                 sample positions land at fixed world locations and an edit
//                 elsewhere cannot shift this face's texels by a fraction.
//   5. size       extent * density, rounded up, plus a gutter on all four
//                 sides. The gutter keeps charts APART: bilinear filtering
//                 reads past a chart edge, and what it must find there is this
//                 face's own colour dilated outward rather than whatever the
//                 packer put next door.
//   6. record     the chart, with the world-space origin and axes a bake needs
//                 to turn a texel back into a world position.
// A chart's allocation on its page, in TEXELS. `min_x` / `min_y` name the corner
// the packer placed it at, and width/height include the gutter on all four
// sides -- so the covered region is inset from this by gutter_in_texels.
struct atlas_rect_t
{
  int min_x = 0;
  int min_y = 0;
  int width = 0;
  int height = 0;
};

struct lightmap_chart_t
{
  entity_uid_t object_uid = 0;

  // IDENTITY, and it is the face's plane -- never an index into the derived
  // face list, which is rebuilt on every edit. Same rule as face_surface_t.
  Plane plane;

  linalg::vec3 tangent_u{0.f, 0.f, 0.f};
  linalg::vec3 tangent_v{0.f, 0.f, 0.f};

  // World position of the chart's (0, 0) texel corner, gutter excluded.
  linalg::vec3 origin{0.f, 0.f, 0.f};

  float world_units_per_texel = 0.f;

  // Gutter INCLUDED -- this is the allocation, and the covered region starts one
  // gutter in from the min corner.
  atlas_rect_t atlas_rect;

  // The face outline in CHART space: world units along tangent_u / tangent_v
  // measured from `origin`, wound as the face is. Retained rather than
  // recomputed because the bake needs it to know which texels fall INSIDE the
  // face -- the rect is the allocation, the polygon is the coverage.
  std::vector<linalg::vec2> polygon;

  // Which atlas LAYER this chart landed on. Filled by pack_lightmap_charts; -1
  // is unpacked, which after a successful pack is a bug rather than a state.
  int page = -1;
};

// What the pack produced. Pages are all the same square size, because the
// packer works one page at a time and a per-page size would be a second thing
// every sampler has to look up.
struct lightmap_atlas_t
{
  int size_in_texels = 0;
  int page_count = 0;
};

struct lightmap_bake_settings_t
{
  float texels_per_world_unit = 0.25f;
  int gutter_in_texels = 2;
  int max_chart_extent_in_texels = 512;
  int atlas_size_in_texels = 1024;
};



// The number of texels a chart covers, gutter excluded -- the range the two
// functions below take their coordinates in.
[[nodiscard]] inline int chart_covered_width(const lightmap_chart_t &chart,
                                             const lightmap_bake_settings_t &settings)
{
  return chart.atlas_rect.width - 2 * settings.gutter_in_texels;
}
[[nodiscard]] inline int chart_covered_height(const lightmap_chart_t &chart,
                                              const lightmap_bake_settings_t &settings)
{
  return chart.atlas_rect.height - 2 * settings.gutter_in_texels;
}

// build_lightmap_charts' projection run backwards, and it is exact -- the
// flattening is an isometry. Chart space is world units along tangent_u /
// tangent_v measured from `origin`, which is the same space `polygon` is in.
[[nodiscard]] linalg::vec3 chart_space_to_world(const lightmap_chart_t &chart,
                                                const linalg::vec2 &chart_space);

// Whether a point has any surface under it. A chart's RECT is its allocation and
// its POLYGON is its coverage, and on anything but a rectangular face the two
// differ -- a point between them belongs to no surface and must not be lit, only
// dilated into.
[[nodiscard]] bool chart_space_is_inside_face(const lightmap_chart_t &chart,
                                              const linalg::vec2 &chart_space);

// The two above, asked about a whole texel through its CENTRE and not its
// corner: a texel is an area, and sampling the corner biases every lookup half a
// texel toward the chart origin. A supersampling solve asks about the sub-sample
// positions instead and goes through the pair above.
[[nodiscard]] linalg::vec2 texel_center_in_chart_space(const lightmap_chart_t &chart,
                                                       int texel_x, int texel_y);
[[nodiscard]] linalg::vec3 texel_world_position(const lightmap_chart_t &chart,
                                                int texel_x, int texel_y);
[[nodiscard]] bool texel_is_inside_face(const lightmap_chart_t &chart, int texel_x,
                                        int texel_y);

// --- Chart space to the atlas ------------------------------------------------
//
// The ONE place a position on a face becomes a position in the atlas, so the
// bake, the debug image and the mesh generator cannot disagree about where a
// texel is. Everything above is per-face and needs no other face; everything
// here needs the PLACEMENT, which only exists once every chart has been packed.

// Chart-space world units -> floating-point texel on the chart's page, gutter
// and packed origin applied.
[[nodiscard]] linalg::vec2 chart_space_to_atlas_texel(const lightmap_chart_t &chart,
                                                      const lightmap_bake_settings_t &settings,
                                                      const linalg::vec2 &chart_space);

// A world position on the face -> the value a VERTEX carries: (u, v, layer),
// normalized for u and v.
//
// THREE components because the atlas is a texture ARRAY. Pages are not
// avoidable -- Vulkan guarantees only 4096 texels per dimension, and a level's
// lightmap outgrows one page long before it outgrows memory -- and binding N
// separate textures means sorting draws by page, which is the cost old engines
// paid. The layer rides per VERTEX rather than per draw because a submesh
// groups faces by MATERIAL, and two faces sharing a material routinely land on
// different pages; a chart never spans one, so per-vertex cannot disagree.
[[nodiscard]] linalg::vec3 lightmap_uv_for(const lightmap_chart_t &chart,
                                           const lightmap_bake_settings_t &settings,
                                           const lightmap_atlas_t &atlas,
                                           const linalg::vec3 &world_position);


// --- What a baked map carries ------------------------------------------------

// How the pages are laid out, and the ONE thing every consumer switches on. The
// value is STORED in the sidecar rather than implied by its version, so adding a
// format is a new enumerator and an arm rather than a rewrite of every reader.
//
// RGB9E5 is the only one, and there is deliberately no 8-bit visibility arm
// beside it: the visibility solve is a MODE of the one bake path (colour forced
// to white, falloff forced to 1), not a second implementation with a second
// format to keep working. A format with no writer is exactly the arm that rots.
enum class lightmap_pixel_format_t : uint32_t
{
  // E5B9G9R9: three 9-bit mantissas sharing one 5-bit exponent, one 32-bit word
  // per texel, matching VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 bit for bit -- so the
  // sampler decodes it and the shader needs no unpack math.
  //
  // The exponent picks the brightness window from the brightest channel and the
  // mantissas give 512 relative steps inside it, so a dark corner and a floodlit
  // wall each get full precision at their own scale. That is the property RGB8
  // lacks: baked colour is HDR, and a bright light beside a dim one clips
  // immediately at eight bits a channel.
  //
  // UNSIGNED, which is fine for irradiance and wrong for anything signed -- an
  // SH L1 direction layer, if one ever lands, needs bias-encoding or a format of
  // its own.
  Rgb9e5 = 0,
};

[[nodiscard]] inline int bytes_per_texel(lightmap_pixel_format_t format)
{
  switch (format)
  {
  case lightmap_pixel_format_t::Rgb9e5:
    return 4;
  }
  return 0;
}

// --- RGB9E5 ------------------------------------------------------------------
//
// EXT_texture_shared_exponent's encoding, unchanged: clamp each channel into the
// representable range, take the exponent from the BRIGHTEST of them, and
// quantize all three against it. Rounding the brightest channel can carry it to
// 2^9 and overflow its own mantissa, which is what the second exponent step is
// for.

inline constexpr int RGB9E5_MANTISSA_BITS = 9;
inline constexpr int RGB9E5_EXPONENT_BIAS = 15;
inline constexpr int RGB9E5_MAX_EXPONENT = 31;
// (2^9 - 1) / 2^9 * 2^(31 - 15) -- the largest value the format can name.
inline constexpr float RGB9E5_MAX_VALUE = 65408.f;

[[nodiscard]] inline uint32_t pack_rgb9e5(const linalg::vec3 &linear_rgb)
{
  const auto clamp_channel = [](float value) {
    if (!(value > 0.f)) return 0.f;
    return value < RGB9E5_MAX_VALUE ? value : RGB9E5_MAX_VALUE;
  };

  const float red = clamp_channel(linear_rgb.x);
  const float green = clamp_channel(linear_rgb.y);
  const float blue = clamp_channel(linear_rgb.z);
  const float brightest = std::max(red, std::max(green, blue));

  int exponent = -RGB9E5_EXPONENT_BIAS - 1;
  if (brightest > 0.f)
    exponent = std::max((int)std::floor(std::log2(brightest)), -RGB9E5_EXPONENT_BIAS - 1);
  exponent += 1 + RGB9E5_EXPONENT_BIAS;

  const auto quantize = [](float value, int shared_exponent) {
    const float scale = std::exp2(
        (float)(shared_exponent - RGB9E5_EXPONENT_BIAS - RGB9E5_MANTISSA_BITS));
    return std::clamp((int)std::floor(value / scale + 0.5f), 0, (1 << RGB9E5_MANTISSA_BITS) - 1);
  };

  const float first_scale =
      std::exp2((float)(exponent - RGB9E5_EXPONENT_BIAS - RGB9E5_MANTISSA_BITS));
  if ((int)std::floor(brightest / first_scale + 0.5f) == (1 << RGB9E5_MANTISSA_BITS))
    ++exponent;
  exponent = std::clamp(exponent, 0, RGB9E5_MAX_EXPONENT);

  return (uint32_t)quantize(red, exponent) |
         ((uint32_t)quantize(green, exponent) << 9) |
         ((uint32_t)quantize(blue, exponent) << 18) | ((uint32_t)exponent << 27);
}

[[nodiscard]] inline linalg::vec3 unpack_rgb9e5(uint32_t word)
{
  const int exponent = (int)((word >> 27) & 0x1fu);
  const float scale =
      std::exp2((float)(exponent - RGB9E5_EXPONENT_BIAS - RGB9E5_MANTISSA_BITS));
  return {(float)(word & 0x1ffu) * scale, (float)((word >> 9) & 0x1ffu) * scale,
          (float)((word >> 18) & 0x1ffu) * scale};
}

// The atlas itself: the pixels a face samples, page-major, in whatever `format`
// says. A BYTE buffer rather than a texel one, because a texel stopped being a
// byte -- and `bytes` is named for what it holds so nothing can index it as if
// it were still the other thing.
//
// Nothing outside reads `bytes` directly: `store` and `load` are the vocabulary,
// and they speak LINEAR RGB, so the quantization lives in exactly one place and
// a second format is one arm rather than a hunt through every caller.
struct lightmap_pages_t
{
  lightmap_pixel_format_t format = lightmap_pixel_format_t::Rgb9e5;
  int size_in_texels = 0;
  int page_count = 0;
  std::vector<uint8_t> bytes;

  [[nodiscard]] bool empty() const { return bytes.empty(); }

  [[nodiscard]] size_t texel_count() const
  {
    return (size_t)page_count * (size_t)size_in_texels * (size_t)size_in_texels;
  }

  [[nodiscard]] size_t byte_offset_of(int page, int x, int y) const
  {
    const size_t texel =
        ((size_t)page * (size_t)size_in_texels + (size_t)y) * (size_t)size_in_texels +
        (size_t)x;
    return texel * (size_t)bytes_per_texel(format);
  }

  void allocate(const lightmap_atlas_t &atlas, lightmap_pixel_format_t pixel_format);
  void store(int page, int x, int y, const linalg::vec3 &linear_rgb);
  [[nodiscard]] linalg::vec3 load(int page, int x, int y) const;
};

// A baked lightmap, resident. The pixels are what the renderer samples and the
// charts are what turns a face into a place in them -- neither is any use
// without the other, so they are one value and they load and save together.
//
// The settings are CARRIED rather than re-derived: a chart's placement is only
// meaningful against the gutter and density that produced it, and the
// alternative is every reader hoping its caller passes the same struct.
struct lightmap_t
{
  lightmap_bake_settings_t settings;
  lightmap_atlas_t atlas;

  // Charts as the bake left them, MINUS `polygon` -- that is the bake's own
  // coverage test and nothing downstream reads it, so it is neither saved nor
  // rebuilt. Mesh generation projects through the stored basis instead, which
  // is what makes it immune to a face being re-wound by a hull rebuild.
  std::vector<lightmap_chart_t> charts;

  // The format lives on the PAGES, which are what it describes -- one owner, so
  // there is no second spelling to disagree with the bytes it is about.
  lightmap_pages_t pages;

  // Identifies everything a MESH is built from -- the charts, the settings and
  // the atlas dimensions, and deliberately not the pixels, which no vertex reads.
  // The generated-mesh cache compares it, so a rebake rebuilds meshes that
  // nothing else about the brush distinguishes: a chart moving in the atlas moves
  // no vertex and changes no material, and without this every cached mesh would
  // still look current and keep its old UVs until some unrelated edit.
  //
  // A CONTENT id rather than a counter, so two identical bakes are identical and
  // correctly rebuild nothing. Set by set_lightmap_geometry_id, which is called
  // wherever a lightmap_t comes into existence.
  uint32_t geometry_id = 0;

  // Empty means this map has no bake -- that is the whole test, exactly like
  // mesh_asset_t::skin and ::blend. Every face then draws unlit.
  [[nodiscard]] bool empty() const { return charts.empty(); }
};

// The lightmap_uv a vertex on an UNLIT face carries. A real coordinate has u and
// v normalized into [0, 1] and a page index at 0 or above, so negative is
// unreachable -- which lets the shader test one component instead of needing a
// second array or a per-submesh flag. Zero would NOT do: it is texel (0, 0) of
// page 0, and reading it is a face wearing some other face's lighting, which is
// exactly the failure the chart key exists to prevent.
inline constexpr linalg::vec3 UNLIT_LIGHTMAP_UV{-1.f, -1.f, -1.f};

// What a brush needs to find its own lighting: the map's bake, plus WHICH object
// this brush is. A null lightmap is "this map has no bake", which is the state
// every map is in until one is run and is not an error.
struct brush_lightmap_ref_t
{
  const lightmap_t *lightmap = nullptr;
  entity_uid_t object_uid = 0;

  [[nodiscard]] bool has_bake() const { return lightmap && !lightmap->empty(); }
};

// The chart a face's plane names, or null. Nearest by normal then distance,
// keyed by (object_uid, plane) -- the same identity rule find_face_surface uses,
// for the same reason: faces are derived from the canonical vertex set and
// rebuilt on every edit, so an index means nothing across one.
//
// The uid is in the key because two brushes can share a plane -- a floor and the
// slab beneath it -- and a plane alone would let one wear the other's lighting.
// A face that matches nothing draws UNLIT, which is a visible, correct-looking
// way to say "this face has no bake"; wearing a neighbour's lighting is the
// failure worth engineering against, precisely because it looks plausible.
// Recomputes `geometry_id` from the charts, the settings and the atlas. Call it
// on any lightmap_t that was just filled in -- the sidecar loader and the bake
// are the two places that is true.
void set_lightmap_geometry_id(lightmap_t &lightmap);

[[nodiscard]] const lightmap_chart_t *find_chart(const lightmap_t &lightmap,
                                                 entity_uid_t object_uid,
                                                 const Plane &plane);

} // namespace shared
