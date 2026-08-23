#include "asset_package.hpp"

#include <algorithm>
#include <cstring>

namespace assets
{

namespace
{

uint32_t read_u32(const uint8_t* at)
{
  uint32_t value = 0;
  memcpy(&value, at, sizeof(value));
  return value;
}

uint64_t read_u64(const uint8_t* at)
{
  uint64_t value = 0;
  memcpy(&value, at, sizeof(value));
  return value;
}

void append_u32(std::vector<uint8_t>& out, uint32_t value)
{
  const size_t at = out.size();
  out.resize(at + sizeof(value));
  memcpy(out.data() + at, &value, sizeof(value));
}

void append_u64(std::vector<uint8_t>& out, uint64_t value)
{
  const size_t at = out.size();
  out.resize(at + sizeof(value));
  memcpy(out.data() + at, &value, sizeof(value));
}

uint64_t aligned_up(uint64_t value, uint64_t alignment)
{
  return (value + alignment - 1) / alignment * alignment;
}

} // namespace

std::optional<asset_package_t> try_open_asset_package(Span<const uint8_t> bytes,
                                                      std::string&        out_reason)
{
  if (bytes.size() < ASSET_PACKAGE_HEADER_SIZE)
  {
    out_reason = "shorter than a package header";
    return std::nullopt;
  }
  if (memcmp(bytes.data, ASSET_PACKAGE_MAGIC, sizeof(ASSET_PACKAGE_MAGIC)) != 0)
  {
    out_reason = "does not start with the package magic";
    return std::nullopt;
  }

  const uint32_t version = read_u32(bytes.data + 8);
  if (version != ASSET_PACKAGE_VERSION)
  {
    out_reason = "is version " + std::to_string(version) + ", this build reads version " +
                 std::to_string(ASSET_PACKAGE_VERSION);
    return std::nullopt;
  }

  asset_package_t package;
  package.bytes             = bytes;
  package.entry_count       = read_u32(bytes.data + 12);
  package.string_table_size = read_u32(bytes.data + 16);
  package.index_offset      = ASSET_PACKAGE_HEADER_SIZE;
  package.string_table_offset =
      package.index_offset + package.entry_count * ASSET_PACKAGE_ENTRY_SIZE;

  const uint64_t sections_end =
      (uint64_t)package.string_table_offset + package.string_table_size;
  if (sections_end > bytes.size())
  {
    out_reason = "index and string table run past the end of the package";
    return std::nullopt;
  }

  // Every entry is bounds-checked ONCE, here, so no lookup has to. A package is
  // opened once per process and this is linear in the index, not in the bytes.
  for (uint32_t which = 0; which < package.entry_count; ++which)
  {
    const asset_package_entry_t entry = asset_package_entry_at(package, which);
    if ((uint64_t)entry.path_offset + entry.path_length > package.string_table_size)
    {
      out_reason = "entry " + std::to_string(which) + " names a path outside the string table";
      return std::nullopt;
    }
    if (entry.data_offset + entry.data_size > bytes.size())
    {
      out_reason = "entry " + std::to_string(which) + " names bytes past the end of the package";
      return std::nullopt;
    }
  }

  return package;
}

asset_package_entry_t asset_package_entry_at(const asset_package_t& package, uint32_t which)
{
  const uint8_t* at = package.bytes.data + package.index_offset +
                      (size_t)which * ASSET_PACKAGE_ENTRY_SIZE;

  asset_package_entry_t entry;
  entry.path_offset = read_u32(at + 0);
  entry.path_length = read_u32(at + 4);
  entry.data_offset = read_u64(at + 8);
  entry.data_size   = read_u64(at + 16);
  return entry;
}

std::string_view asset_package_path_at(const asset_package_t& package, uint32_t which)
{
  const asset_package_entry_t entry = asset_package_entry_at(package, which);
  return std::string_view(
      reinterpret_cast<const char*>(package.bytes.data + package.string_table_offset +
                                    entry.path_offset),
      entry.path_length);
}

std::optional<Span<const uint8_t>> try_find_asset_in_package(const asset_package_t& package,
                                                             const char*            path)
{
  const std::string_view wanted(path);

  uint32_t low  = 0;
  uint32_t high = package.entry_count;
  while (low < high)
  {
    const uint32_t         middle = low + (high - low) / 2;
    const std::string_view here   = asset_package_path_at(package, middle);
    if (here < wanted)
      low = middle + 1;
    else if (wanted < here)
      high = middle;
    else
    {
      const asset_package_entry_t entry = asset_package_entry_at(package, middle);
      return Span<const uint8_t>(package.bytes.data + entry.data_offset, (uint32_t)entry.data_size);
    }
  }
  return std::nullopt;
}

std::vector<uint8_t> build_asset_package(std::vector<asset_package_input_t>& files)
{
  std::sort(files.begin(), files.end(),
            [](const asset_package_input_t& left, const asset_package_input_t& right)
            { return left.path < right.path; });

  std::vector<uint8_t> string_table;
  std::vector<uint32_t> path_offsets(files.size());
  for (size_t which = 0; which < files.size(); ++which)
  {
    path_offsets[which] = (uint32_t)string_table.size();
    string_table.insert(string_table.end(), files[which].path.begin(), files[which].path.end());
  }

  const uint64_t index_offset        = ASSET_PACKAGE_HEADER_SIZE;
  const uint64_t string_table_offset = index_offset + files.size() * ASSET_PACKAGE_ENTRY_SIZE;
  const uint64_t blob_offset =
      aligned_up(string_table_offset + string_table.size(), ASSET_PACKAGE_DATA_ALIGNMENT);

  std::vector<uint64_t> data_offsets(files.size());
  uint64_t              at = blob_offset;
  for (size_t which = 0; which < files.size(); ++which)
  {
    data_offsets[which] = at;
    at = aligned_up(at + files[which].bytes.size(), ASSET_PACKAGE_DATA_ALIGNMENT);
  }

  std::vector<uint8_t> package;
  package.reserve((size_t)at);

  package.insert(package.end(), ASSET_PACKAGE_MAGIC,
                 ASSET_PACKAGE_MAGIC + sizeof(ASSET_PACKAGE_MAGIC));
  append_u32(package, ASSET_PACKAGE_VERSION);
  append_u32(package, (uint32_t)files.size());
  append_u32(package, (uint32_t)string_table.size());
  append_u32(package, 0);

  for (size_t which = 0; which < files.size(); ++which)
  {
    append_u32(package, path_offsets[which]);
    append_u32(package, (uint32_t)files[which].path.size());
    append_u64(package, data_offsets[which]);
    append_u64(package, (uint64_t)files[which].bytes.size());
  }

  package.insert(package.end(), string_table.begin(), string_table.end());

  for (size_t which = 0; which < files.size(); ++which)
  {
    package.resize((size_t)data_offsets[which], 0);
    package.insert(package.end(), files[which].bytes.begin(), files[which].bytes.end());
  }
  package.resize((size_t)at, 0);

  return package;
}

} // namespace assets
