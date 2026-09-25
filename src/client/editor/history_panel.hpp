#pragma once

#include "../../shared/map.hpp"
#include "transaction_system.hpp"

namespace client
{

// Paint.NET's History window: every transaction by the name its gesture gave
// it, the undone ones greyed below the current one. A click undoes or redoes
// to that row, hovering lists what the row changed. True when the map moved.
// `open` is the toolbar's toggle, cleared by the window's close button.
[[nodiscard]] bool draw_history_panel(Transaction_System& transactions, shared::map_t& map, bool& open);

} // namespace client
