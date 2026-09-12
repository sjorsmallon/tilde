#pragma once

// Which sky a view pass draws, resolved from a NAME.
//
// The name is a cubemap_asset's, and it arrives as text -- out of sv_skybox
// over the cvar mirror in game, out of the open map's own cvars block in the
// editor. Both are read every frame, and both can name a sky this build does
// not have, so the resolve has to be cheap AND has to complain exactly once.
// That is the whole reason this is a value rather than a free function: it
// remembers what it last resolved, so an unresolvable name logs on the frame it
// appears instead of on every frame it persists.
//
// register_skybox itself is already idempotent, so re-resolving the SAME name
// costs a short scan; what this adds is not the caching, it is the silence.

#include "renderer.hpp"

#include <string>
#include <string_view>

namespace client
{

struct skybox_selection_t
{
  // An invalid handle is a complete answer: the pass draws no sky and the scene
  // clear shows through, which is what every map without one does today.
  [[nodiscard]] renderer::skybox_handle_t resolve(std::string_view requested);

private:
  std::string               resolved_name_;
  bool                      has_resolved_ = false;
  renderer::skybox_handle_t handle_;
};

} // namespace client
