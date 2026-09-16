#pragma once

#include "input.hpp"

#include <optional>
#include <string_view>

namespace client::input
{

// The console's spelling of a key, as `bind` reads it and binds.cfg writes it.
[[nodiscard]] std::string_view key_name(key_t key);

// Case-insensitive. Refuses Unknown and anything not in the table.
[[nodiscard]] std::optional<key_t> try_key_from_name(std::string_view name);

} // namespace client::input
