#pragma once

// Irradiance probes, lighting_def.md gate 5: WHERE they are and how they are
// FILLED. The grid is derived from the map -- the geometry bound and one spacing
// -- so nothing places a probe, nothing can forget one, and the editor's preview
// and the bake enumerate the same points by calling the same function. The
// types (probe_grid_t, probe_volume_t) live in lightmap.hpp, which knows nothing
// about a map; this header is the entry points.

#include "collision_detection.hpp"
#include "lightmap.hpp"
#include "lightmap_lights.hpp"
#include "lightmap_trace.hpp"
#include "map.hpp"
#include "span.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace shared
{

// The grid over a map: the union of the GEOMETRY bounds -- entities move and
// a light above the level is not a place light bounces -- snapped outward to
// multiples of the spacing and padded by one spacing on every side, so the
// outermost wall has a probe outside it as well as inside. Snapping is what
// keeps an edit to one brush from shifting every probe by a fraction.
//
// Empty when the map has no geometry, or when an axis would exceed
// MAX_PROBE_GRID_EXTENT -- that one logs the spacing that would fit.
[[nodiscard]] std::optional<probe_grid_t> try_build_probe_grid(const map_t &map,
                                                               float spacing);

// One flag per probe, in index order: true where the probe sits inside a solid.
// `occluders` is the bake's own BVH (build_occluder_bvh), never the editor's,
// which holds entity boxes a probe is free to sit inside.
[[nodiscard]] std::vector<uint8_t>
classify_probes_inside_solid(const probe_grid_t &grid,
                             const Bounding_Volume_Hierarchy &occluders);

// Which Mixed light each of the PROBE_VISIBILITY_CHANNELS is OF, by baked slot,
// in slot order -- the first four Mixed lights of the bake. A fifth and later
// gets LIGHTMAP_NO_LIGHT_SLOT nowhere and a line naming it: on dynamic objects
// it is unoccluded by static geometry, its shadow map still holds the dynamic
// casters. A Baked light needs no channel (its occlusion is folded into the
// probe's radiance) and a Dynamic light has no bake.
[[nodiscard]] probe_visibility_slots_t
assign_probe_visibility_channels(Span<const baked_light_t> lights);

// Fill every probe flagged inside a solid from the ones around it that are not:
// the gutter dilation in three dimensions. Repeated until every probe holds a
// value, one shell per pass, so a probe deep in a thick wall takes the value of
// the nearest open air rather than staying black. Over RAW floats, before any
// encoding -- averaging bias-encoded bytes would be wrong for the reason the
// gutter's is. A probe with no open neighbour in the whole volume stays zero.
// The visibility channels dilate beside the light, for the same reason the
// atlas's one gutter pass covers both of its roles.
void dilate_probes_inside_solid(const probe_grid_t &grid, Span<const uint8_t> inside,
                                std::vector<probe_trace_t> &values);

// The whole probe half of a bake: classify the grid against `occluders`, trace
// every open probe on every core, dilate into the rest, encode. `lights` and
// `scene` are the ones the chart solve used, so a probe and the wall beside it
// are one lighting model. `settings.rays_per_sample` at zero traces no chain and
// stores the direct term alone.
[[nodiscard]] probe_volume_t bake_probe_volume(const probe_grid_t &grid,
                                               const Bounding_Volume_Hierarchy &occluders,
                                               const traced_scene_t &scene,
                                               Span<const baked_light_t> lights,
                                               const indirect_trace_settings_t &settings);

} // namespace shared
