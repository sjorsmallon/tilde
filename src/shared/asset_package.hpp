#pragma once

// asset_package.{hpp,cpp} -- the container `pkg` and `embed` both read, and the
// one place its layout is spelled. asset_pack WRITES it, the byte layer READS
// it, and the two are separate binaries, so the format is exactly the
// two-parties-must-agree case a shared header exists for.
//
// `pkg` and `embed` are NOT two code paths: a package is one contiguous byte
// range, and the only difference between the modes is where that range comes
// from -- a file read at mount, or a `#embed`ed array in .rodata. Everything
// below this line is identical for both, and everything above the byte layer
// cannot tell which one it is running against.
//
// GRAMMAR (little-endian, one contiguous byte range)
//
//   package       -> header index string_table padding blob
//
//   header        -> magic:char[8]         ASSET_PACKAGE_MAGIC, no terminator
//                    version:u32           ASSET_PACKAGE_VERSION
//                    entry_count:u32
//                    string_table_size:u32
//                    reserved:u32          zero
//
//   index         -> entry{entry_count}
//
//   entry         -> path_offset:u32       into string_table
//                    path_length:u32
//                    data_offset:u64       from the start of the package
//                    data_size:u64
//
//   string_table  -> path{entry_count}     NOT NUL terminated; the length is in
//                                          the entry, so a path with an
//                                          embedded NUL cannot truncate a lookup
//
//   padding       -> byte*                 to ASSET_PACKAGE_DATA_ALIGNMENT
//
//   blob          -> (bytes padding){entry_count}
//
// Entries are sorted by path, so a lookup is a binary search straight over the
// mapped bytes: nothing is parsed at mount, no map is built, and the index
// costs no allocation in either mode.
//
// An entry is read out with memcpy rather than by pointing a struct at the
// bytes. A `#embed`ed array has whatever alignment the declaration gives it and
// a package can arrive from anywhere; a binary search does log2(n) copies of 24
// bytes, which buys the alignment question never being asked.

#include "span.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace assets
{

inline constexpr char     ASSET_PACKAGE_MAGIC[8]         = {'T', 'I', 'L', 'D', 'E', 'P', 'K', 'G'};
inline constexpr uint32_t ASSET_PACKAGE_VERSION          = 1;
inline constexpr uint32_t ASSET_PACKAGE_HEADER_SIZE      = 24;
inline constexpr uint32_t ASSET_PACKAGE_ENTRY_SIZE       = 24;
inline constexpr uint32_t ASSET_PACKAGE_DATA_ALIGNMENT   = 16;

struct asset_package_entry_t
{
  uint32_t path_offset = 0;
  uint32_t path_length = 0;
  uint64_t data_offset = 0;
  uint64_t data_size   = 0;
};

// An opened package: the byte range plus where the two sections start in it.
// Holds no allocation of its own -- every span points into `bytes`, which the
// caller keeps alive (a file read once at mount, or .rodata).
struct asset_package_t
{
  Span<const uint8_t> bytes;
  uint32_t            entry_count       = 0;
  uint32_t            index_offset      = 0;
  uint32_t            string_table_offset = 0;
  uint32_t            string_table_size = 0;
};

// Fallible, and the caller is what makes it so: the bytes came off disk or out
// of an exe that may not be the one the package was built for. `out_reason`
// says which of the header checks failed, so the mount can name it.
[[nodiscard]] std::optional<asset_package_t> try_open_asset_package(Span<const uint8_t> bytes,
                                                                    std::string& out_reason);

[[nodiscard]] asset_package_entry_t asset_package_entry_at(const asset_package_t& package,
                                                           uint32_t               which);

// The path of an entry, as a view into the string table. Not NUL terminated.
[[nodiscard]] std::string_view asset_package_path_at(const asset_package_t& package,
                                                     uint32_t               which);

// The bytes behind a path, or empty when the package does not hold it. The
// caller decides which of those is a failure: read_asset_bytes is fatal on a
// miss, asset_exists returns the bool.
[[nodiscard]] std::optional<Span<const uint8_t>>
try_find_asset_in_package(const asset_package_t& package, const char* path);

// --- Writing (asset_pack) ---

struct asset_package_input_t
{
  std::string          path; // project-root relative, forward slashes
  std::vector<uint8_t> bytes;
};

// Sorts `files` by path (the index is searched, so the order is the format's,
// not the caller's) and returns the whole package. Duplicate paths are the
// caller's to reject -- asset_pack already reports them by name.
[[nodiscard]] std::vector<uint8_t> build_asset_package(std::vector<asset_package_input_t>& files);

// The `#embed`ed package, defined only in embed mode by the generated TU. A
// declaration with no definition is exactly right for the other two modes:
// nothing calls it, so nothing links it.
[[nodiscard]] Span<const uint8_t> embedded_asset_package();

} // namespace assets
