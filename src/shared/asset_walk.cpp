#include "asset_walk.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_set>

namespace assets
{

const char* to_string(asset_class_t asset_class)
{
  switch (asset_class)
  {
    case asset_class_t::mesh_asset:      return "mesh_asset";
    case asset_class_t::texture_asset:   return "texture_asset";
    case asset_class_t::sound_asset:     return "sound_asset";
    case asset_class_t::animation_asset: return "animation_asset";
    case asset_class_t::hitbox_rig:      return "hitbox_rig";
    case asset_class_t::font_asset:      return "font_asset";
    case asset_class_t::pbr_material:    return "pbr_material";
    case asset_class_t::cubemap_asset:   return "cubemap_asset";
  }
  return "?";
}

namespace
{

struct class_row_t
{
  const char*   extension;
  asset_class_t asset_class;
};

// Adding a row is harmless; adding an extension to an existing class renumbers
// that class, which is fine since ids are minted per process.
constexpr class_row_t CLASS_TABLE[] = {
    {".obj", asset_class_t::mesh_asset},
    {".mesh", asset_class_t::mesh_asset},
    {".glb", asset_class_t::mesh_asset},
    {".png", asset_class_t::texture_asset},
    {".tga", asset_class_t::texture_asset},
    {".wav", asset_class_t::sound_asset},
    {".animation", asset_class_t::animation_asset},
    {".hitboxes", asset_class_t::hitbox_rig},
    {".ttf", asset_class_t::font_asset},
};

// The classes whose unit is a FOLDER. The marker is the one file the loader
// cannot do without: albedo, because a material with no albedo resolves to
// nothing; up.png, because a cube with five faces is not a cube.
struct directory_class_row_t
{
  const char*   marker;
  asset_class_t asset_class;
};

constexpr directory_class_row_t DIRECTORY_CLASS_TABLE[] = {
    {"albedo.png", asset_class_t::pbr_material},
    {"up.png", asset_class_t::cubemap_asset},
};

// Never given an id. .skeleton and .mtl are named from inside another asset,
// so the sibling path is the identity the format itself uses; .md is
// documentation that happens to sit in a resource directory.
constexpr const char* IGNORED_EXTENSIONS[] = {".skeleton", ".mtl", ".md"};

// Not resources in the runtime sense: blender/ is source art; shaders/ is
// compiled to SPIR-V by the build on a path of its own.
constexpr const char* EXCLUDED_DIRECTORIES[] = {"blender", "shaders"};

constexpr const char* UNPACKED_EXTENSIONS[] = {".md"};

std::string lowercase_extension(std::string_view path)
{
  const size_t slash = path.find_last_of('/');
  const size_t dot   = path.find_last_of('.');
  if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
    return {};

  std::string extension(path.substr(dot));
  for (char& character : extension)
    character = (char)tolower((unsigned char)character);
  return extension;
}

std::string_view filename_of(std::string_view path)
{
  const size_t slash = path.find_last_of('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string_view stem_of(std::string_view path)
{
  const std::string_view filename = filename_of(path);
  const size_t           dot      = filename.find_last_of('.');
  return dot == std::string_view::npos ? filename : filename.substr(0, dot);
}

bool extension_is_ignored(const std::string& extension)
{
  for (const char* ignored : IGNORED_EXTENSIONS)
  {
    if (extension == ignored)
      return true;
  }
  return false;
}

std::optional<asset_class_t> try_find_class_for_extension(const std::string& extension)
{
  for (const class_row_t& row : CLASS_TABLE)
  {
    if (extension == row.extension)
      return row.asset_class;
  }
  return std::nullopt;
}

bool directory_is_excluded(std::string_view name)
{
  for (const char* excluded : EXCLUDED_DIRECTORIES)
  {
    if (name == excluded)
      return true;
  }
  return false;
}

// A leading dot is an OS or tool artifact (.DS_Store, ._name, .gitkeep), never
// an asset, and it is skipped wherever it sits.
bool name_is_hidden(std::string_view name)
{
  return !name.empty() && name[0] == '.';
}

// Basename minus extension, case preserved, and it must ALREADY be a valid C++
// identifier. There is deliberately no mangling rule: the minted name is the
// identity written into .source map files, so a mangling rule is a way for
// two files to quietly claim one name.
bool name_is_mintable(std::string_view stem)
{
  if (stem.empty())
    return false;

  const char first = stem[0];
  if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_'))
    return false;

  for (const char character : stem)
  {
    const bool is_identifier_character = (character >= 'A' && character <= 'Z') ||
                                         (character >= 'a' && character <= 'z') ||
                                         (character >= '0' && character <= '9') ||
                                         character == '_';
    if (!is_identifier_character)
      return false;
  }
  return true;
}

// The OUTERMOST ancestor directory of `path` (below the root component) that
// holds a marker file, with the class the marker names. Outermost, because a
// marker directory nested inside a claimed one is contents, not an asset.
struct directory_claim_t
{
  std::string   directory;
  asset_class_t asset_class;
};

std::optional<directory_claim_t> try_find_directory_claim(
    std::string_view path, const std::unordered_set<std::string_view>& all_paths)
{
  size_t slash = path.find('/');
  while (slash != std::string_view::npos)
  {
    const std::string_view directory = path.substr(0, slash);
    for (const directory_class_row_t& row : DIRECTORY_CLASS_TABLE)
    {
      std::string marker_path(directory);
      marker_path += '/';
      marker_path += row.marker;
      if (all_paths.contains(marker_path))
        return directory_claim_t{std::string(directory), row.asset_class};
    }
    slash = path.find('/', slash + 1);
  }
  return std::nullopt;
}

void mint(asset_classification_t& classification, asset_class_t asset_class, std::string_view name,
          std::string_view path)
{
  for (const classified_asset_t& existing : classification.assets)
  {
    if (existing.asset_class == asset_class && existing.name == name)
    {
      classification.errors.push_back("asset class '" + std::string(to_string(asset_class)) +
                                      "' has two entries named '" + std::string(name) + "' ('" +
                                      existing.path + "' and '" + std::string(path) +
                                      "'); rename one");
      return;
    }
  }
  classification.assets.push_back({asset_class, std::string(name), std::string(path)});
}

} // namespace

asset_classification_t classify_asset_paths(Span<const std::string> paths)
{
  asset_classification_t classification;

  std::unordered_set<std::string_view> all_paths;
  for (const std::string& path : paths)
    all_paths.insert(path);

  std::unordered_set<std::string> minted_directories;

  for (const std::string& path : paths)
  {
    if (const std::optional<directory_claim_t> claim = try_find_directory_claim(path, all_paths))
    {
      if (!minted_directories.insert(claim->directory).second)
        continue;

      const std::string_view name = filename_of(claim->directory);
      if (!name_is_mintable(name))
        classification.errors.push_back("'" + claim->directory + "' is a " +
                                        to_string(claim->asset_class) +
                                        ", but its name cannot become an identifier. Rename the "
                                        "directory: a minted name is never mangled");
      else
        mint(classification, claim->asset_class, name, claim->directory);
      continue;
    }

    const std::string extension = lowercase_extension(path);
    if (extension_is_ignored(extension))
      continue;

    const std::optional<asset_class_t> asset_class = try_find_class_for_extension(extension);
    if (!asset_class)
    {
      classification.errors.push_back(
          "'" + path + "' has extension '" + (extension.empty() ? "(none)" : extension) +
          "', which no asset class claims. Add a row to CLASS_TABLE in asset_walk.cpp, or add "
          "the extension to IGNORED_EXTENSIONS if it is never referenced by id");
      continue;
    }

    const std::string_view stem = stem_of(path);
    if (!name_is_mintable(stem))
    {
      classification.errors.push_back(
          "'" + path + "' cannot become an identifier, so it cannot be an asset name. Rename the "
          "file: a minted name is never mangled, because the name is what a .source map file stores");
      continue;
    }

    mint(classification, *asset_class, stem, path);
  }

  std::sort(classification.assets.begin(), classification.assets.end(),
            [](const classified_asset_t& left, const classified_asset_t& right) {
              if (left.asset_class != right.asset_class)
                return left.asset_class < right.asset_class;
              return left.path < right.path;
            });

  return classification;
}

asset_tree_listing_t list_asset_tree(const std::filesystem::path& root)
{
  asset_tree_listing_t listing;

  std::string prefix = root.filename().generic_string();
  if (prefix.empty())
    prefix = root.parent_path().filename().generic_string();

  std::error_code failure;
  if (!std::filesystem::is_directory(root, failure))
  {
    listing.errors.push_back("'" + root.generic_string() + "' is not a directory");
    return listing;
  }

  for (const std::filesystem::directory_entry& item : std::filesystem::directory_iterator(root, failure))
  {
    const std::string name = item.path().filename().generic_string();
    if (name_is_hidden(name))
      continue;

    if (item.is_directory())
    {
      if (directory_is_excluded(name))
        continue;

      for (const std::filesystem::directory_entry& nested :
           std::filesystem::recursive_directory_iterator(item.path(), failure))
      {
        if (!nested.is_regular_file())
          continue;

        const std::filesystem::path relative = std::filesystem::relative(nested.path(), root, failure);
        bool                        hidden   = false;
        for (const std::filesystem::path& component : relative)
          hidden |= name_is_hidden(component.generic_string());
        if (hidden)
          continue;

        listing.paths.push_back(prefix + "/" + relative.generic_string());
      }
      continue;
    }

    listing.errors.push_back("'" + prefix + "/" + name +
                             "' sits directly in the resource root; move it into a subdirectory");
  }

  if (failure)
    listing.errors.push_back("cannot read '" + root.generic_string() + "': " + failure.message());

  std::sort(listing.paths.begin(), listing.paths.end());
  return listing;
}

bool asset_path_is_packed(std::string_view path)
{
  const std::string extension = lowercase_extension(path);
  for (const char* unpacked : UNPACKED_EXTENSIONS)
  {
    if (extension == unpacked)
      return false;
  }
  return true;
}

} // namespace assets
