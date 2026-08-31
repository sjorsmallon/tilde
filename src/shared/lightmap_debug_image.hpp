#pragma once

#include "lightmap_bake.hpp"

#include <string>

namespace shared
{

// One PNG per atlas page, written as "<path_prefix>_page<N>.png".
//
// What it shows, and why each layer is there: the chart's whole RECT in a dim
// per-chart colour (its allocation, gutter included), the covered region inset
// by the gutter in a brighter shade (so the gutter is VISIBLE as a border --
// the most common packing mistake is not having one), and the face outline in
// white (so a chart whose polygon does not sit inside its own rect is obvious
// rather than subtly wrong).
//
// Returns false if a page could not be written; the caller decides whether a
// missing debug image matters.
[[nodiscard]] bool try_write_lightmap_debug_png(const std::vector<lightmap_chart_t> &charts,
                                                const lightmap_atlas_t &atlas,
                                                const lightmap_bake_settings_t &settings,
                                                const std::string &path_prefix);

} // namespace shared
