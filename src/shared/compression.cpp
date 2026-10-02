#include "compression.hpp"

#include "log.hpp"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz/miniz.h"

namespace shared
{

namespace
{

constexpr int    FASTEST_DEFLATE_LEVEL     = 1;
constexpr int    RAW_DEFLATE_WINDOW_BITS   = -15;
constexpr size_t DEFLATE_MAXIMUM_EXPANSION = 1032;

mz_bool append_compressed_bytes(const void* bytes, int count, void* user)
{
  std::vector<uint8_t>* compressed_bytes = static_cast<std::vector<uint8_t>*>(user);
  const uint8_t*        first            = static_cast<const uint8_t*>(bytes);
  compressed_bytes->insert(compressed_bytes->end(), first, first + count);
  return MZ_TRUE;
}

} // namespace

std::vector<uint8_t> compress_bytes(Span<const uint8_t> bytes)
{
  std::vector<uint8_t> compressed_bytes;
  const mz_uint        flags = tdefl_create_comp_flags_from_zip_params(
      FASTEST_DEFLATE_LEVEL, RAW_DEFLATE_WINDOW_BITS, MZ_DEFAULT_STRATEGY);

  if (!tdefl_compress_mem_to_output(bytes.data, bytes.count, append_compressed_bytes,
                                    &compressed_bytes, static_cast<int>(flags)))
    fatal_error("compress_bytes: deflate refused {} bytes", bytes.count);

  return compressed_bytes;
}

std::optional<std::vector<uint8_t>> try_decompress_bytes(Span<const uint8_t> compressed_bytes,
                                                         size_t decompressed_size_in_bytes)
{
  if (decompressed_size_in_bytes >
      static_cast<size_t>(compressed_bytes.count) * DEFLATE_MAXIMUM_EXPANSION)
    return std::nullopt;

  std::vector<uint8_t> bytes(decompressed_size_in_bytes);
  const size_t         written = tinfl_decompress_mem_to_mem(
      bytes.data(), bytes.size(), compressed_bytes.data, compressed_bytes.count, 0);

  if (written != decompressed_size_in_bytes)
    return std::nullopt;

  return bytes;
}

} // namespace shared
