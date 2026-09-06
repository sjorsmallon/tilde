#pragma once

#include "aabb.hpp"
#include "array.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "plane.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace shared
{

// How many direct lights one chart can carry a visibility for, and the ONE place
// the number is written down. Four because that is what four UNORM8 scalars cost
// -- the same four bytes a texel of RGB9E5 irradiance already costs -- and what
// Source 2 caps a surface at. Growing it means growing the pixel format beside
// it, which is what the static_assert at that arm is for.
//
// The layout is per-CHART ids and per-TEXEL strengths (lighting_def.md ss12 A):
// the ids are a fixed, small growth of the chart record, and the strengths are
// the second page set. Light N+1 on a chart is DROPPED with a line naming both,
// never silently, and never quietly kept -- an unlisted light reads as fully
// occluded, so the failure is a dark face rather than light through a wall.
inline constexpr uint32_t LIGHTMAP_LIGHTS_PER_CHART = 4;

// An unfilled slot. Zero would not do: it is the first entry of the resolve
// table, which is a real light.
inline constexpr int16_t LIGHTMAP_NO_LIGHT_SLOT = -1;

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

struct chart_triangle_twin_t
{
  Array<linalg::vec3, 3> corners;
  Array<linalg::vec3, 3> normals;
};

// One vertex of a static mesh chart's UNWRAP: which asset vertex it came from,
// and where it sits in chart space (world units, from the chart's min corner).
struct unwrapped_vertex_t
{
  uint32_t xref = 0;
  linalg::vec2 uv{0.f, 0.f};
};

// A static mesh chart's unwrap, and the one thing about a chart that is SAVED
// beyond its placement: an xatlas uv is an algorithm's output, not a projection
// through a plane, so it cannot be re-derived at load. `indices` are into
// `vertices`, three per triangle; `faces[t]` is the SOURCE triangle of triangle
// t, which is what keeps the draw copy's submesh ranges intact.
struct chart_unwrap_t
{
  std::vector<unwrapped_vertex_t> vertices;
  std::vector<uint32_t> indices;
  std::vector<uint32_t> faces;

  [[nodiscard]] bool empty() const { return faces.empty(); }
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

  // The OTHER coverage shape, for a chart that is not one polygon: a static
  // mesh's coplanar triangles, three chart-space corners each. A mesh's
  // coplanar triangles need not be adjacent, so their union is not a polygon
  // and the point-in-face test walks these instead. Exactly one of `polygon`
  // and `triangles` is filled, and like `polygon` it is the bake's own and is
  // never saved.
  std::vector<linalg::vec2> triangles;

  // The 3D twin of each entry of `triangles`, one per three: the source
  // triangle's world corners and vertex normals, blended per texel by the bake.
  // Required whenever `triangles` is filled; bake-only like it.
  std::vector<chart_triangle_twin_t> twins;

  // A static mesh chart's unwrap; empty on a brush chart, whose uvs are the
  // stored plane projected. Saved, unlike everything above it.
  chart_unwrap_t unwrap;

  // Which atlas LAYER this chart landed on. Filled by pack_lightmap_charts; -1
  // is unpacked, which after a successful pack is a bug rather than a state.
  int page = -1;

  // Which light each channel of this chart's visibility texels is OF, as an
  // index into lightmap_t::light_uids. Filled by the solve, which ranks the
  // baked lights by what they deliver to this chart and keeps the strongest
  // few; LIGHTMAP_NO_LIGHT_SLOT is a channel no light claimed.
  //
  // Per CHART rather than per texel because a texel has room for a strength and
  // not for an index space, and per chart rather than per map because four
  // lights across a whole level is not a level.
  Array<int16_t, LIGHTMAP_LIGHTS_PER_CHART> light_slots{
      {LIGHTMAP_NO_LIGHT_SLOT, LIGHTMAP_NO_LIGHT_SLOT, LIGHTMAP_NO_LIGHT_SLOT,
       LIGHTMAP_NO_LIGHT_SLOT}};
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

  // Gate 5: the distance between irradiance probes on every axis. 64 is about a
  // player height, the doc's "one probe per 1-2 m". Carried like the rest so a
  // reload rebuilds the grid the bake traced.
  float probe_spacing_in_world_units = 64.f;

  float reflection_spacing_in_world_units = 512.f;
  int reflection_size_in_texels = 64;
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

// The ONE question the solve asks of a chart at a chart-space point: is there
// surface here, and if so where in the world and facing which way. A plane
// chart answers through its plane and polygon; a triangle chart rasterizes the
// 2D triangle under the point and blends its twin by barycentric weight.
struct texel_sample_t
{
  bool on_surface = false;
  linalg::vec3 position{0.f, 0.f, 0.f};
  linalg::vec3 normal{0.f, 0.f, 0.f};
};

[[nodiscard]] texel_sample_t sample_chart(const lightmap_chart_t &chart,
                                          const linalg::vec2 &chart_space);

// The same, asked about a whole texel through its CENTRE and not its corner:
// sampling the corner biases every lookup half a texel toward the chart origin.
[[nodiscard]] linalg::vec2 texel_center_in_chart_space(const lightmap_chart_t &chart,
                                                       int texel_x, int texel_y);
[[nodiscard]] texel_sample_t sample_texel(const lightmap_chart_t &chart, int texel_x,
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

// The same value from a CHART-SPACE position -- what an unwrapped vertex stores.
[[nodiscard]] linalg::vec3 lightmap_uv_from_chart_space(const lightmap_chart_t &chart,
                                                        const lightmap_bake_settings_t &settings,
                                                        const lightmap_atlas_t &atlas,
                                                        const linalg::vec2 &chart_space);


// --- What a baked map carries ------------------------------------------------

// How the pages are laid out, and the ONE thing every consumer switches on. The
// value is STORED in the sidecar rather than implied by its version, so adding a
// format is a new enumerator and an arm rather than a rewrite of every reader.
//
// TWO of them, because a bake produces two things of different shapes: HDR
// irradiance, and a per-light visibility that is four scalars in [0, 1]. They
// are TWO PAGE SETS rather than one buffer with a per-texel choice -- `format`
// describes a whole buffer, and lightmap_def.md decision D is where that caveat
// was written down before there was a second role to need it.
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
  // UNSIGNED, which is fine for irradiance and is why the SH L1 pages beside it
  // are Unorm8x4 and bias-encoded: L0 is non-negative and lands here, L1 is
  // signed and cannot.
  Rgb9e5 = 0,

  // The VISIBILITY role: one UNORM8 per light slot, in the chart's slot order.
  // Eight bits is enough because the value is a coverage FRACTION rather than a
  // radiance -- it has a top of 1.0 and no window to place, which is the whole
  // reason the irradiance beside it cannot use the same eight bits.
  //
  // Four bytes a texel, exactly what RGB9E5 costs, which is why the cap is four
  // rather than a number picked for the atlas budget.
  //
  // The SH L1 pages carry these same BITS with a different MEANING -- a signed
  // direction component, bias-encoded -- which is why they have a third accessor
  // pair below rather than reusing the visibility one.
  Unorm8x4 = 1,
};

[[nodiscard]] inline int bytes_per_texel(lightmap_pixel_format_t format)
{
  switch (format)
  {
  case lightmap_pixel_format_t::Rgb9e5:
    return 4;
  case lightmap_pixel_format_t::Unorm8x4:
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

// --- SH L1, the INDIRECT encoding (lighting_def.md gate 2) -------------------
//
// Four coefficients a colour channel: `l0`, the average radiance arriving from
// every direction, and an L1 3-vector saying which way that light leans and how
// hard. The shader reconstructs the irradiance at a SHADED normal with
//
//   E(N) = SH_L1_IRRADIANCE_L0 * L0 + SH_L1_IRRADIANCE_L1 * dot(L1, N)
//
// then clamps to zero, because a truncated L1 fit rings negative behind a strong
// lobe exactly as a truncated Fourier series overshoots. Those two weights are
// not tuning: they are pi * Y0 and (2pi/3) * Y1, the cosine lobe's per-band
// convolution weights folded into the basis normalization -- which is what takes
// the cosine OUT of the bake and lets a normal map move the indirect term.
inline constexpr float SH_L1_Y0 = 0.282095f;
inline constexpr float SH_L1_Y1 = 0.488603f;
inline constexpr float SH_L1_IRRADIANCE_L0 = 0.886227f;
inline constexpr float SH_L1_IRRADIANCE_L1 = 1.023328f;

// What |L1| / L0 maxes at, and it is DERIVED rather than picked: for light from
// a single direction the ratio is exactly Y1 / Y0 = sqrt(3). Normalizing by
// SH_L1_NORMALIZATION * L0 therefore lands in [-1, 1] BY CONSTRUCTION, so the
// bias encoding cannot clip on a legal bake.
inline constexpr float SH_L1_NORMALIZATION = 1.7320508f;

// A texel of L1 is nine numbers -- three axes by three colour channels -- and a
// Unorm8x4 word holds four. So the L1 page set has THREE layers per atlas page,
// one per world axis, each holding that axis's component for r, g and b.
//
// By AXIS and not by channel because that is the grouping the shader wants: it
// reconstructs all three colour channels at once out of one fetch per axis and
// one scalar multiply, where the other grouping needs a transpose.
inline constexpr int SH_L1_LAYERS_PER_PAGE = 3;

// What one texel of indirect light IS. `l1` is indexed by WORLD AXIS -- l1[0] is
// the x component of the L1 vector for each of r, g and b -- matching the page
// layout above.
//
// WORLD space and not tangent space: the fragment shader has a world normal, and
// lighting_def.md ss13 deliberately has no tangent vertex attribute.
struct indirect_sh_l1_t
{
  linalg::vec3 l0{0.f, 0.f, 0.f};
  Array<linalg::vec3, SH_L1_LAYERS_PER_PAGE> l1{};
};

// --- Irradiance probes, lighting_def.md gate 5 -------------------------------

// Vulkan's floor for maxImageDimension3D. A grid axis longer than this cannot be
// uploaded as one 3D texture, and the bake refuses rather than shrinks -- the
// spacing is the author's setting to change.
inline constexpr int MAX_PROBE_GRID_EXTENT = 256;

// WHERE the probes are. Derived from the map's geometry bound and one spacing
// (lightmap_probes.hpp builds one), so nothing places a probe and nothing can
// forget one.
struct probe_grid_t
{
  // The world position of probe (0, 0, 0); every other probe is origin plus an
  // integer multiple of the spacing along each axis.
  linalg::vec3 origin{0.f, 0.f, 0.f};
  float spacing = 0.f;
  linalg::vec3i count{0, 0, 0};

  [[nodiscard]] size_t probe_count() const
  {
    return (size_t)count.x * (size_t)count.y * (size_t)count.z;
  }

  // x fastest, then y, then z -- the order a 3D texture's texels are laid out.
  [[nodiscard]] size_t index_of(int x, int y, int z) const
  {
    return ((size_t)z * (size_t)count.y + (size_t)y) * (size_t)count.x + (size_t)x;
  }

  [[nodiscard]] linalg::vec3i coordinates_of(size_t index) const
  {
    const int x = (int)(index % (size_t)count.x);
    const int y = (int)((index / (size_t)count.x) % (size_t)count.y);
    const int z = (int)(index / ((size_t)count.x * (size_t)count.y));
    return {x, y, z};
  }

  [[nodiscard]] linalg::vec3 position_of(const linalg::vec3i &coordinates) const
  {
    return origin + linalg::vec3{(float)coordinates.x, (float)coordinates.y,
                                 (float)coordinates.z} *
                        spacing;
  }

  [[nodiscard]] aabb_bounds_t bounds() const
  {
    return {origin, position_of({count.x - 1, count.y - 1, count.z - 1})};
  }
};

// The SH L1 byte codec, shared by the atlas pages and the probe volume so the
// two cannot encode a direction differently. A component is normalized against
// SH_L1_NORMALIZATION * L0 and bias-encoded into a byte; an L0 of zero has no
// direction to encode and stores the zero vector rather than a division nobody
// can bound.
[[nodiscard]] inline uint8_t encode_sh_l1_component(float component, float l0_channel)
{
  if (!(l0_channel > 0.f)) return 128;
  const float normalized =
      std::clamp(component / (SH_L1_NORMALIZATION * l0_channel), -1.f, 1.f);
  return (uint8_t)std::clamp((int)std::lround((normalized * 0.5f + 0.5f) * 255.f), 0,
                             255);
}

[[nodiscard]] inline float decode_sh_l1_component(uint8_t encoded, float l0_channel)
{
  const float normalized = (float)encoded * (1.f / 255.f) * 2.f - 1.f;
  return normalized * SH_L1_NORMALIZATION * l0_channel;
}

// Gate 9 step 4: how many Mixed lights a probe stores a VISIBILITY for -- one
// channel each of one Unorm8x4 texel, exactly the atlas's per-texel cap and for
// its reason. A fifth Mixed light in a map gets no channel: it is unoccluded on
// dynamic objects, said once at bake time (assign_probe_visibility_channels).
// A second 3D image would need a per-fragment sampler pick, which is the one
// binding pattern the pass set deliberately has none of.
inline constexpr uint32_t PROBE_VISIBILITY_CHANNELS = 4;

// Which baked SLOT each visibility channel is OF -- the probes' twin of a
// chart's light_slots. LIGHTMAP_NO_LIGHT_SLOT is a channel no Mixed light
// claimed; the runtime matches a tail light's slot against these four.
using probe_visibility_slots_t = Array<int16_t, PROBE_VISIBILITY_CHANNELS>;
inline constexpr probe_visibility_slots_t NO_PROBE_VISIBILITY_SLOTS{
    {LIGHTMAP_NO_LIGHT_SLOT, LIGHTMAP_NO_LIGHT_SLOT, LIGHTMAP_NO_LIGHT_SLOT,
     LIGHTMAP_NO_LIGHT_SLOT}};

// WHAT the probes hold: the same four numbers a texel of indirect light holds,
// in the same two encodings, over the grid above. `l0_bytes` is one RGB9E5 word
// per probe in grid index order; `l1_bytes` is SH_L1_LAYERS_PER_PAGE Unorm8x4
// words per probe, AXIS-MAJOR -- every probe's x component, then every y, then
// every z -- so each axis is one contiguous 3D image upload, exactly as each
// axis is one layer of the atlas's L1 page set.
//
// `visibility_bytes` is gate 9's one new bake output: one Unorm8x4 word per
// probe, each channel the FRACTION of a Mixed light this point in space sees
// (light_visibility, the same function the atlas's channels come from), the
// channel named by `visibility_slots`. A dynamic object multiplies it into the
// Mixed light's analytic term the way a brush face multiplies in its atlas
// texel -- decision K's product, with the static occluders on this side of it.
// Sized with the volume whether or not any light claimed a channel, so a
// volume is one shape; an unclaimed channel reads 1.
//
// Empty is "this bake traced no probes", the test lightmap_t::empty makes for
// the charts.
struct probe_volume_t
{
  probe_grid_t grid;
  std::vector<uint8_t> l0_bytes;
  std::vector<uint8_t> l1_bytes;
  probe_visibility_slots_t visibility_slots = NO_PROBE_VISIBILITY_SLOTS;
  std::vector<uint8_t> visibility_bytes;

  [[nodiscard]] bool empty() const { return l0_bytes.empty(); }

  void allocate(const probe_grid_t &probe_grid);
  void store(size_t index, const indirect_sh_l1_t &value);
  [[nodiscard]] indirect_sh_l1_t load(size_t index) const;

  // The visibility role's pair, in coverage fractions per channel -- kept apart
  // from `store` for the reason lightmap_pages_t keeps store_visibility apart:
  // four independent scalars, not a colour.
  void store_visibility(size_t index, const Array<float, PROBE_VISIBILITY_CHANNELS> &coverage);
  [[nodiscard]] Array<float, PROBE_VISIBILITY_CHANNELS> load_visibility(size_t index) const;
};

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

  // `layers_per_atlas_page` is 1 for every role but SH L1, whose nine numbers a
  // texel do not fit one word -- see SH_L1_LAYERS_PER_PAGE. It is a parameter
  // rather than a second allocate so that the atlas stays the ONE thing every
  // page set is sized from.
  void allocate(const lightmap_atlas_t &atlas, lightmap_pixel_format_t pixel_format,
                int layers_per_atlas_page = 1);

  // The IRRADIANCE role's vocabulary, in linear RGB.
  void store(int page, int x, int y, const linalg::vec3 &linear_rgb);
  [[nodiscard]] linalg::vec3 load(int page, int x, int y) const;

  // The VISIBILITY role's, in coverage fractions -- one per light slot, in the
  // owning chart's slot order. A pair of its own rather than a widened `store`
  // because these are four independent scalars and not a colour: nothing here
  // may be tonemapped, exposed or sRGB-encoded, and a vec4 invites all three.
  void store_visibility(int page, int x, int y,
                        const Array<float, LIGHTMAP_LIGHTS_PER_CHART> &coverage);
  [[nodiscard]] Array<float, LIGHTMAP_LIGHTS_PER_CHART>
  load_visibility(int page, int x, int y) const;

  // The SH L1 role's, and it takes the texel's L0 because the encoding
  // normalizes against SH_L1_NORMALIZATION * L0 -- a direction has no scale of
  // its own to quantize against. Same Unorm8x4 BITS as the visibility pair and a
  // different MEANING, which is exactly why it is a third pair and not a reuse.
  //
  // `page` is the ATLAS page; the three layers it occupies are this pair's own
  // business, so no caller multiplies by SH_L1_LAYERS_PER_PAGE.
  void store_l1(int page, int x, int y, const linalg::vec3 &l0,
                const Array<linalg::vec3, SH_L1_LAYERS_PER_PAGE> &l1);
  [[nodiscard]] Array<linalg::vec3, SH_L1_LAYERS_PER_PAGE>
  load_l1(int page, int x, int y, const linalg::vec3 &l0) const;
};

// Gate 6, lighting_def.md decision L: reflection captures and their parallax boxes.
inline constexpr uint32_t REFLECTION_BLEND_COUNT = 4;
inline constexpr uint32_t REFLECTION_BOX_FACE_COUNT = 6;

// The cube map's face order, and the box's.
enum class reflection_box_face_t : uint8_t
{
  Positive_X,
  Negative_X,
  Positive_Y,
  Negative_Y,
  Positive_Z,
  Negative_Z,
};
inline constexpr int REFLECTION_CUBE_FACE_COUNT = 6;

// The direction through the centre of cube texel (x, y) of `face`, the GL cube
// map convention a samplerCube fetches by.
[[nodiscard]] inline linalg::vec3 reflection_cube_direction(int face, int x, int y,
                                                            int size_in_texels)
{
  const float u = 2.f * ((float)x + 0.5f) / (float)size_in_texels - 1.f;
  const float v = 2.f * ((float)y + 0.5f) / (float)size_in_texels - 1.f;
  linalg::vec3 direction{0.f, 0.f, 0.f};
  switch (face)
  {
    case 0: direction = {1.f, -v, -u}; break;
    case 1: direction = {-1.f, -v, u}; break;
    case 2: direction = {u, 1.f, v}; break;
    case 3: direction = {u, -1.f, -v}; break;
    case 4: direction = {u, -v, 1.f}; break;
    default: direction = {-u, -v, -1.f}; break;
  }
  return linalg::normalize(direction);
}

// The RGB9E5 mip chain of one capture: mip-major, then face, then rows. Mip 0
// is the traced picture; mip m is it GGX-prefiltered at roughness
// m / (mip_count - 1) (prefilter_reflection_cube).
struct reflection_cube_t
{
  int size_in_texels = 0;
  int mip_count = 0;
  std::vector<uint8_t> bytes;

  [[nodiscard]] bool empty() const { return bytes.empty(); }

  // Whether the bytes fit the declared chain -- what a reader asks of a file
  // before indexing it. An empty cube fits by definition.
  [[nodiscard]] bool bytes_fit_declared_chain() const
  {
    if (bytes.empty()) return true;
    return size_in_texels > 0 && mip_count == mip_count_for(size_in_texels) &&
           bytes.size() == texel_count() * sizeof(uint32_t);
  }

  [[nodiscard]] static int mip_count_for(int size)
  {
    int count = 1;
    while (size > 1)
    {
      size >>= 1;
      ++count;
    }
    return count;
  }
  [[nodiscard]] int size_of_mip(int mip) const { return std::max(size_in_texels >> mip, 1); }
  [[nodiscard]] size_t texels_in_mip(int mip) const
  {
    const size_t size = (size_t)size_of_mip(mip);
    return (size_t)REFLECTION_CUBE_FACE_COUNT * size * size;
  }
  [[nodiscard]] size_t texel_offset_of_mip(int mip) const
  {
    size_t offset = 0;
    for (int below = 0; below < mip; ++below) offset += texels_in_mip(below);
    return offset;
  }
  [[nodiscard]] size_t texel_count() const { return texel_offset_of_mip(mip_count); }
  [[nodiscard]] size_t texel_index_of(int mip, int face, int x, int y) const
  {
    const size_t size = (size_t)size_of_mip(mip);
    return texel_offset_of_mip(mip) + ((size_t)face * size + (size_t)y) * size + (size_t)x;
  }

  void allocate(int size)
  {
    size_in_texels = size;
    mip_count = mip_count_for(size);
    bytes.assign(texel_count() * sizeof(uint32_t), 0);
  }
  void store(size_t texel, const linalg::vec3 &linear_rgb)
  {
    const uint32_t word = pack_rgb9e5(linear_rgb);
    std::memcpy(bytes.data() + texel * sizeof(uint32_t), &word, sizeof(word));
  }
  void store(int mip, int face, int x, int y, const linalg::vec3 &linear_rgb)
  {
    store(texel_index_of(mip, face, x, y), linear_rgb);
  }
  [[nodiscard]] linalg::vec3 load(size_t texel) const
  {
    uint32_t word = 0;
    std::memcpy(&word, bytes.data() + texel * sizeof(uint32_t), sizeof(word));
    return unpack_rgb9e5(word);
  }
  [[nodiscard]] linalg::vec3 load(int mip, int face, int x, int y) const
  {
    return load(texel_index_of(mip, face, x, y));
  }
  [[nodiscard]] linalg::vec3 load(int face, int x, int y) const { return load(0, face, x, y); }
};

// reflection_cube_direction run backwards: the face and texel of `size` a
// direction falls in.
struct reflection_cube_texel_t
{
  int face = 0;
  int x = 0;
  int y = 0;
};
[[nodiscard]] reflection_cube_texel_t reflection_cube_texel_of(const linalg::vec3 &direction,
                                                                int size_in_texels);

struct reflection_capture_t
{
  linalg::vec3 position{0.f, 0.f, 0.f};
  aabb_bounds_t box{};
  uint32_t probe_index = 0;
  uint8_t open_faces = 0; // bit (reflection_box_face_t) set where the axis ray hit nothing
  bool box_overridden = false;
  reflection_cube_t cube;

  [[nodiscard]] bool face_is_open(reflection_box_face_t face) const
  {
    return (open_faces >> (uint8_t)face) & 1u;
  }
};

struct reflection_capture_set_t
{
  float spacing = 0.f;
  std::vector<reflection_capture_t> captures;

  [[nodiscard]] bool empty() const { return captures.empty(); }
  [[nodiscard]] bool baked() const
  {
    return !captures.empty() && !captures.front().cube.empty();
  }
};

struct reflection_capture_pick_t
{
  Array<uint32_t, REFLECTION_BLEND_COUNT> indices = {};
  Array<float, REFLECTION_BLEND_COUNT> weights = {};
  uint32_t count = 0;
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
  //
  // TWO sets, one per role. The irradiance is what a surface with no per-light
  // visibility falls back to; the visibility is what lets the shader run
  // shade_direct with the real light direction, which is the whole of
  // lighting_def.md decision A. Both are sized from the same atlas and a chart's
  // rect names a place in either, so a texel in one is the same texel in the
  // other.
  lightmap_pages_t irradiance_pages;
  lightmap_pages_t visibility_pages;

  // The path-traced bounce, SH L1 (lighting_def.md gate 2). Its OWN page set
  // rather than a term merged into the irradiance above, and that is not
  // tidiness: the residual is a fallback that disappears the day
  // LIGHTMAP_LIGHTS_PER_CHART rises, indirect is permanent -- and merging two
  // buffers later is trivial where splitting one is a migration.
  //
  // `indirect_l1_pages` carries SH_L1_LAYERS_PER_PAGE layers per atlas page.
  // Empty when the bake was not asked to trace, which is every bake before gate
  // 2 and every one with the setting off; the shader falls back to nothing.
  lightmap_pages_t indirect_l0_pages;
  lightmap_pages_t indirect_l1_pages;

  // Gate 5: the light at points in SPACE, for everything that has no chart.
  // Empty when the bake was not asked for probes.
  probe_volume_t probes;

  // Gate 6: the reflection captures and their traced cubes. Empty when the bake
  // was not asked for them.
  reflection_capture_set_t reflections;

  // The resolve table: baked slot -> the light ENTITY that slot is of, which is
  // what a chart's `light_slots` index into. A uid rather than an index into
  // anything runtime, because the runtime light array is rebuilt every frame by
  // the gather pass and a bake outlives every ordering it could have recorded.
  //
  // The one new invariant a mask brings: a baked slot must resolve to a live
  // light. A slot naming a light the author deleted resolves to nothing, and
  // nothing is what a deleted light contributes.
  std::vector<entity_uid_t> light_uids;

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

// What a lightmapped VERTEX carries: a place in the atlas, and which light each
// channel of the visibility texel there is OF. PARALLEL to mesh_asset_t's
// vertices and one vertex binding of its own, the shape vertex_skin_t and
// vertex_blend_t already have.
//
// ONE struct rather than two parallel arrays because both halves are one answer
// out of one chart -- the uv names a texel in BOTH page sets and the slots say
// how to read the second of them, so two arrays would be two things free to
// disagree about their length.
//
// The slots ride per VERTEX for the reason the page does: a submesh groups faces
// by MATERIAL, and two faces sharing a material are two charts with two
// different sets of lights. A chart never spans a vertex, so per-vertex cannot
// disagree with itself.
struct vertex_lightmap_t
{
  linalg::vec3 uv = UNLIT_LIGHTMAP_UV;

  // The owning chart's light_slots, verbatim: indices into lightmap_t::light_uids,
  // LIGHTMAP_NO_LIGHT_SLOT where no light claimed the channel.
  Array<int16_t, LIGHTMAP_LIGHTS_PER_CHART> light_slots{
      {LIGHTMAP_NO_LIGHT_SLOT, LIGHTMAP_NO_LIGHT_SLOT, LIGHTMAP_NO_LIGHT_SLOT,
       LIGHTMAP_NO_LIGHT_SLOT}};
};

// The renderer describes this to Vulkan as ONE binding of two attributes, at
// these offsets. A drift is a vertex buffer read at the wrong stride, which
// draws plausible garbage rather than failing anywhere.
static_assert(sizeof(vertex_lightmap_t) == 20);
static_assert(offsetof(vertex_lightmap_t, uv) == 0);
static_assert(offsetof(vertex_lightmap_t, light_slots) == 12);

// What a brush or a static mesh needs to find its own lighting: the map's bake,
// plus WHICH object this is. A null lightmap is "this map has no bake", which is
// the state every map is in until one is run and is not an error.
struct object_lightmap_ref_t
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

// The resolve table run BACKWARDS, and the gather pass is its one caller: the
// bake recorded which light ENTITY each slot is of, and a frame has the entity
// and wants the slot. LIGHTMAP_NO_LIGHT_SLOT for a light the bake never saw --
// a Dynamic one, or one authored since the last bake, and either way a light
// with no visibility to sample.
[[nodiscard]] int16_t find_baked_light_slot(const lightmap_t &lightmap,
                                            entity_uid_t light_uid);

// How many charts kept this slot among their LIGHTMAP_LIGHTS_PER_CHART. Zero for
// a light the bake saw and that delivered nothing to any face -- out of range,
// aimed at nothing, or occluded everywhere -- which draws exactly as if it were
// not there, and is the one thing a Baked light can do that nothing else reports.
[[nodiscard]] size_t count_charts_keeping_light(const lightmap_t &lightmap, int16_t slot);

} // namespace shared
