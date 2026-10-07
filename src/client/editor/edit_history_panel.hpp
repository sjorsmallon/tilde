#pragma once

#include "../../shared/map.hpp"
#include "transaction_system.hpp"

namespace client
{

[[nodiscard]] bool draw_edit_history_panel(Transaction_System& transactions, shared::map_t& map, bool& open);

} // namespace client
