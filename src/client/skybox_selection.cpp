#include "skybox_selection.hpp"

#include "../shared/log.hpp"

namespace client
{

renderer::skybox_handle_t skybox_selection_t::resolve(std::string_view requested)
{
  if (has_resolved_ && resolved_name_ == requested)
    return handle_;

  resolved_name_.assign(requested);
  has_resolved_ = true;
  handle_       = {};

  if (requested.empty())
    return handle_;

  // try_from_string is the same resolve the console and the map file go
  // through, so a name that is not a cubemap is refused here rather than
  // reaching the renderer as an out-of-range id.
  const std::optional<assets::cubemap_asset> id =
      assets::try_from_string<assets::cubemap_asset>(requested);
  if (!id.has_value())
  {
    log_error("[client] skybox '{}' is not a cubemap this build has -- no sky drawn. Cubemaps "
              "are folders under resources/cubemaps/",
              resolved_name_);
    return handle_;
  }

  handle_ = renderer::register_skybox(*id);
  return handle_;
}

} // namespace client
