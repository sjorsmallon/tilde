#pragma once

#include <cstdint>

namespace shared
{

using entity_uid_t = uint32_t;

inline constexpr entity_uid_t null_entity_uid = 0;
inline constexpr entity_uid_t invalid_entity_uid = 0;

} // namespace shared
