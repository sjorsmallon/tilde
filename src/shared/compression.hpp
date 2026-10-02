#pragma once

#include "span.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace shared
{

// Raw DEFLATE with no header and no checksum: the caller carries the decompressed size and its own hash.
std::vector<uint8_t> compress_bytes(Span<const uint8_t> bytes);

[[nodiscard]] std::optional<std::vector<uint8_t>>
try_decompress_bytes(Span<const uint8_t> compressed_bytes, size_t decompressed_size_in_bytes);

} // namespace shared
