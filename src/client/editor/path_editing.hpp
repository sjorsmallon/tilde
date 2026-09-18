#pragma once

// What every tool must honour about path chains, whichever tool is active. mover_def.md ss7 and ss15.

#include "../../shared/entity_system.hpp"
#include "../../shared/mover_path.hpp"
#include "../frame_builder.hpp"
#include "editor_types.hpp"
#include "transaction_system.hpp"

#include <vector>

namespace client
{

struct path_scratch_t
{
  shared::Entity_System system;
  shared::path_links_t  links;
};

void rebuild_path_scratch(const shared::map_t& map, path_scratch_t& scratch);

[[nodiscard]] std::vector<shared::entity_uid_t> chain_through(const path_scratch_t& scratch,
                                                              shared::entity_uid_t node);

[[nodiscard]] bool chain_is_closed(const path_scratch_t& scratch,
                                   const std::vector<shared::entity_uid_t>& chain);

[[nodiscard]] std::vector<shared::entity_uid_t> geometry_owned_by(const shared::map_t& map,
                                                                  shared::entity_uid_t owner);

void relink_paths_around_removed_nodes(shared::map_t& map,
                                       Span<const shared::entity_uid_t> removed,
                                       transaction_t& transaction);

void draw_path_links(const shared::map_t& map, const editor_context_t& ctx, pass_builder_t& draws);

} // namespace client
