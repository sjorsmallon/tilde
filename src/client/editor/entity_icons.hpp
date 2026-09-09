#pragma once

#include "../../shared/entity_uid.hpp"
#include "../../shared/span.hpp"
#include "editor_types.hpp"

// Screen-space editor icons: an image drawn at an entity's projected position at
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
// An icon is a `texture_asset`, which means it is a PNG under resources/icons/
// and nothing had to be taught about it: the one resource walk enumerates any
// unclaimed file at any depth, and .png is a texture. The art is WHITE WITH AN
// ALPHA CHANNEL because the pass tints it with the light's own colour, and a
// coloured icon could only be multiplied by that. These replaced hand-authored
// polylines; src/tools/icon_bake.py is where those points went.

namespace shared { struct map_t; }

namespace client
{

// Every icon in the map, projected through `view` and drawn into ImGui's
// background draw list -- background so wiring and icons pass UNDER the panels
// rather than over the inspector you are reading.
//
// Takes the whole map rather than one entity because the pass is per FRAME and
// the projection is the expensive half; a per-entity entry point would be a
// second place that has to agree about culling.
// `hidden` is what the outliner hid; an icon for something you cannot see is a
// glyph you cannot click.
void draw_entity_icons(const shared::map_t& map, const viewport_state_t& view,
                       Span<const shared::entity_uid_t> hidden);

} // namespace client
