#pragma once

// Charts and packing: a brush face becomes a rectangle, and the rectangles
// become atlas pages. The TYPES all live in lightmap.hpp, which knows nothing
// about a map -- that is what lets map_t hold a lightmap_t with no include
// cycle, so this header is the two entry points and nothing else.

#include "lightmap.hpp"
#include "map.hpp"

#include <vector>

namespace shared
{

// One chart per brush face and one per PLANE of a static mesh, in the six steps
// lightmap.hpp lists above lightmap_chart_t. A mesh's coplanar triangles share a
// chart whether or not they touch, which is what keeps the (uid, plane) key
// unique with no second identity; the chart's coverage is then a triangle list
// rather than a polygon. A face with `emits_geometry` false gets none: nothing
// draws it, so nothing samples it.
//
// `face_surface_t::lightmap_scale` multiplies the density per face, and
// `max_chart_extent_in_texels` caps the result by LOWERING that density rather
// than truncating the chart -- a truncated chart is a face whose far half
// samples somebody else's texels.
//
// The charts come back unpacked: `page` is -1 and `atlas_rect` is sized but not
// placed until pack_lightmap_charts runs.
[[nodiscard]] std::vector<lightmap_chart_t>
build_lightmap_charts(const map_t &map, const lightmap_bake_settings_t &settings);

// Places every chart, writing `page` and the rect's min corner back INTO them --
// which is why the charts are taken by reference: a placement is a property of
// the chart, and returning it separately would be a second thing to keep in step
// with the list it describes.
//
// A chart too big for a page at all is a LOUD failure and an empty atlas, never
// a silent shrink: the fix is a settings change the author has to make, and a
// quietly rescaled level is one they cannot see they need to.
[[nodiscard]] lightmap_atlas_t
pack_lightmap_charts(std::vector<lightmap_chart_t> &charts,
                     const lightmap_bake_settings_t &settings);

} // namespace shared
