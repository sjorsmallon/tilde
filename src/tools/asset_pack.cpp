//
// asset_pack.cpp -- the package writer. Walks the resource tree through the
// SAME classification the game numbers its ids from at startup
// (src/shared/asset_walk.cpp) and writes assets.pkg: the bytes of every file
// the game can reach at runtime, keyed by the one path spelling
// read_asset_bytes takes.
//
// It carries no project knowledge of its own any more: the extension table,
// the directory classes and the ignored extensions live in asset_walk.cpp,
// where the game reads them too. A tree this tool refuses is a tree the game
// would refuse at startup, with the same message.
//
// The package is WIDER than the id space and narrower than the tree. Wider,
// because .mtl and .skeleton are named from inside another asset and are
// mandatory at runtime; narrower by the unpacked extensions, because
// documentation that happens to sit in a resource directory is not read by
// the game, and shipping it would put a README in .rodata.
//

#define _CRT_SECURE_NO_WARNINGS // fopen

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../shared/asset_package.hpp"
#include "../shared/asset_walk.hpp"

namespace
{

bool read_whole_file(const std::filesystem::path& path, std::vector<uint8_t>& out)
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open())
    return false;

  const std::streamoff size = file.tellg();
  if (size < 0)
    return false;
  file.seekg(0, std::ios::beg);

  out.resize((size_t)size);
  return size == 0 || (bool)file.read(reinterpret_cast<char*>(out.data()), size);
}

// Write-if-different, so an unchanged tree rebuilds nothing downstream.
bool write_if_different(const std::filesystem::path& path, const std::string& text)
{
  FILE* existing = fopen(path.string().c_str(), "rb");
  if (existing != nullptr)
  {
    std::string current;
    char        buffer[4096];
    size_t      read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), existing)) > 0)
      current.append(buffer, read);
    fclose(existing);

    if (current == text)
    {
      fprintf(stderr, "asset_pack: %s is up to date\n", path.string().c_str());
      return true;
    }
  }

  FILE* file = fopen(path.string().c_str(), "wb");
  if (file == nullptr)
  {
    fprintf(stderr, "asset_pack: error: cannot write '%s'\n", path.string().c_str());
    return false;
  }
  fwrite(text.data(), 1, text.size(), file);
  fclose(file);

  fprintf(stderr, "asset_pack: wrote %s\n", path.string().c_str());
  return true;
}

} // namespace

int main(int argument_count, char** arguments)
{
  const char* resources_directory = nullptr;
  const char* package_path        = nullptr;

  for (int index = 1; index < argument_count; ++index)
  {
    if (strcmp(arguments[index], "--package") == 0 && index + 1 < argument_count)
    {
      package_path = arguments[++index];
      continue;
    }
    if (arguments[index][0] == '-')
    {
      fprintf(stderr, "asset_pack: error: unknown option '%s'\n", arguments[index]);
      return 1;
    }
    if (resources_directory != nullptr)
    {
      fprintf(stderr, "asset_pack: error: more than one resource directory given\n");
      return 1;
    }
    resources_directory = arguments[index];
  }

  if (resources_directory == nullptr || package_path == nullptr)
  {
    fprintf(stderr, "usage: asset_pack <resources-dir> --package <path>\n"
                    "\n"
                    "Classifies <resources-dir> exactly as the game does at startup and writes\n"
                    "assets.pkg: the bytes of every file the game can reach at runtime, keyed\n"
                    "by the one path spelling read_asset_bytes takes. Write-if-different.\n");
    return 1;
  }

  const std::filesystem::path root = std::filesystem::path(resources_directory);

  const assets::asset_tree_listing_t   listing        = assets::list_asset_tree(root);
  const assets::asset_classification_t classification =
      assets::classify_asset_paths(Span<const std::string>(listing.paths));

  int32_t error_count = 0;
  for (const std::string& error : listing.errors)
  {
    fprintf(stderr, "asset_pack: error: %s\n", error.c_str());
    ++error_count;
  }
  for (const std::string& error : classification.errors)
  {
    fprintf(stderr, "asset_pack: error: %s\n", error.c_str());
    ++error_count;
  }
  if (error_count > 0)
  {
    fprintf(stderr, "asset_pack: %d error%s\n", error_count, error_count == 1 ? "" : "s");
    return 1;
  }

  // The logical path is "<root name>/<relative>"; the file sits under the
  // root's parent, which is where the game runs from too.
  const std::filesystem::path mount = root.parent_path();

  std::vector<assets::asset_package_input_t> package_files;
  for (const std::string& logical : listing.paths)
  {
    if (!assets::asset_path_is_packed(logical))
      continue;

    std::vector<uint8_t> bytes;
    if (!read_whole_file(mount / logical, bytes))
    {
      fprintf(stderr, "asset_pack: error: cannot read '%s' for the package\n", logical.c_str());
      return 1;
    }
    package_files.push_back({logical, std::move(bytes)});
  }

  const std::vector<uint8_t> package = assets::build_asset_package(package_files);
  const std::string          bytes(reinterpret_cast<const char*>(package.data()), package.size());
  if (!write_if_different(std::filesystem::path(package_path), bytes))
    return 1;

  fprintf(stderr, "asset_pack: package holds %zu files (%zu ids), %zu bytes\n",
          package_files.size(), classification.assets.size(), package.size());
  return 0;
}
