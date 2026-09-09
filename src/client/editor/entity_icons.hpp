#pragma once

#include "../../shared/linalg.hpp"
#include "../../shared/span.hpp"
#include "editor_types.hpp"

// Screen-space editor icons: a glyph drawn at an entity's projected position at
// a constant PIXEL size, the way every level editor marks a thing whose
// interesting property is not its shape.
//
// Constant size is the whole point rather than a shortcut. A world-space gizmo
// for a light is a cross that is three pixels across from the far side of the
// room, which is exactly where you are standing when you want to know where the
// lights are. A billboard in the debug list would have been the other answer --
// depth-tested and in-world -- and is what to reach for the day an icon needs to
// be occluded by geometry. These deliberately are not: a light behind a wall is
// a thing you still want to see.
//
// The glyphs are DATA, so this header knows nothing about ImGui and nothing
// about a renderer: it is authored coordinates plus the one function that
// strokes them.

namespace shared { struct map_t; }
namespace entities { struct Entity; }

namespace client
{

// One stroke of a glyph. Two points is a line; `closed` joins the last point
// back to the first.
struct icon_polyline_t
{
  Span<const linalg::vec2> points;
  bool                     closed = false;
};

// Coordinates are AUTHORED in a y-DOWN canvas, which is what a drawing program
// hands you and what the glyphs below were drawn in. Normalizing once here beats
// converting forty points by hand, and it means a glyph can be edited in the
// space it was drawn in.
//
// `canvas_height` is the span that maps to the icon's pixel size, so a glyph
// whose rays reach past its bulb stays inside its allotted square by having a
// canvas_height that includes them.
struct icon_shape_t
{
  Span<const icon_polyline_t> polylines;
  linalg::vec2                canvas_center;
  float                       canvas_height = 1.f;
};

extern const icon_shape_t POINT_LIGHT_ICON;
extern const icon_shape_t SPOT_LIGHT_ICON;
extern const icon_shape_t DIRECTIONAL_LIGHT_ICON;

// Every icon in the map, projected through `view` and stroked into ImGui's
// background draw list -- background so wiring and icons pass UNDER the panels
// rather than over the inspector you are reading.
//
// Takes the whole map rather than one entity because the pass is per FRAME and
// the projection is the expensive half; a per-entity entry point would be a
// second place that has to agree about culling.
void draw_entity_icons(const shared::map_t& map, const viewport_state_t& view);

} // namespace client
