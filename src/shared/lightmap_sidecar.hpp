#pragma once

// The .lightmap sidecar, beside the .source. The navmesh precedent: derived
// from the map, invalidated by editing it, far too big for the authored text --
// so it is a binary sidecar loaded into a member of map_t, and map_t stays
// purely authored.

#include "lightmap.hpp"

#include <string>

namespace shared
{

// Both take the MAP's path and derive the sidecar's, so no caller ever spells
// the extension and the two halves cannot disagree about where the file is.
void save_lightmap_sidecar(const std::string &map_path, const lightmap_t &lightmap,
                           uint32_t map_content_hash);

// Empty when there is no sidecar, or when one is unreadable. A HASH mismatch is
// not that case: it loads anyway and logs loudly. An author who nudged one brush
// invalidated the hash and almost nothing else, and refusing would black out the
// level for a one-brush edit -- which teaches people to distrust the bake. The
// per-face plane match is the fine-grained guard: the moved brush's faces find
// no chart and go unlit, every other face stays correct.
[[nodiscard]] lightmap_t load_lightmap_sidecar(const std::string &map_path,
                                               uint32_t map_content_hash);

} // namespace shared
