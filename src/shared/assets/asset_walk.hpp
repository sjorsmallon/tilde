#pragma once

// The ONE classification of the resource tree, read by two callers: the game
// at startup, to number the ids, and asset_pack, to write the package. A
// second copy of these rules could disagree with the first about what exists
// or what id 3 means, so there is no second copy.
//
// Two rules, and between them they cover the real tree:
//
// 1. A CLAIMED FILE HAS NO ID. Something else already names it: it sits under
//    a directory DIRECTORY_CLASS_TABLE names by its marker file (a material, a
//    cubemap -- the folder is the asset and its files are its contents), or it
//    carries an extension on IGNORED_EXTENSIONS (.mtl, .skeleton -- named as a
//    bare sibling from inside the file that needs them). Everything unclaimed
//    is enumerated, at ANY depth.
//
// 2. EXTENSION DECIDES THE CLASS, from CLASS_TABLE. Directory names carry no
//    meaning. An unknown extension on an unclaimed file is an ERROR NAMING THE
//    FILE, not a skip.
//
// Classification runs over a list of LOGICAL PATHS ("resources/glb/duck.glb")
// rather than over a directory, so the package index in a pkg or embed build
// classifies through the same function as the loose tree.

#include "asset_id.hpp"
#include "span.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace assets
{

struct classified_asset_t
{
  asset_class_t asset_class;
  std::string   name;
  std::string   path;
};

// `assets` is sorted by (class, path), which is the id order. `errors` name
// the file; the game dies on them, asset_pack prints them and exits.
struct asset_classification_t
{
  std::vector<classified_asset_t> assets;
  std::vector<std::string>        errors;
};

[[nodiscard]] asset_classification_t classify_asset_paths(Span<const std::string> paths);

// Every regular file under `root`, as logical paths prefixed with the root's
// own name, sorted. Hidden names (a leading dot) are skipped wherever they
// sit; EXCLUDED_DIRECTORIES are skipped at the root; a file directly in the
// root is an error.
struct asset_tree_listing_t
{
  std::vector<std::string> paths;
  std::vector<std::string> errors;
};

[[nodiscard]] asset_tree_listing_t list_asset_tree(const std::filesystem::path& root);

// Whether a file the tree holds ships in the package. Documentation does not.
[[nodiscard]] bool asset_path_is_packed(std::string_view path);

} // namespace assets
