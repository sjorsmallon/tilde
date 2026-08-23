//
// asset_pack.cpp -- the asset walker. Walks the resource tree once and writes
// the manifest def_gen reads.
//
// A host tool beside def_gen and map_convert, and like them it is allowed
// PROJECT KNOWLEDGE: the extension table below is the whole of it. def_gen has
// none -- everything it needs about assets arrives in the manifest, which is
// what let `import` and its three rules go away.
//
// One walk owns what exists on disk: names, ids, AND bytes. That is a
// load-bearing property, not a tidiness one -- a second walk could disagree
// with the first about what exists or about what id 3 means, and a package
// built from the disagreeing half ships a game that resolves the wrong mesh.
// --package is therefore the same walk as --manifest, not a second tool: the
// enumerated files it wrote ids for are the same objects it puts bytes in.
//
// ---------------------------------------------------------------------------
// Classification -- two rules, and between them they cover the real tree
// ---------------------------------------------------------------------------
//
// 1. DEPTH 1 ONLY. Files directly under <resources>/<dir>/ are the id space.
//    Anything nested is the path-referenced pool: packed, never enumerated.
//    That line is not arbitrary, it is the existing split between "referenced
//    by id from code or a map" and "referenced by path from another asset" --
//    textures/harsh_bricks/albedo.png and models/textures/leet_skin.png are
//    both already below it.
//
// 2. EXTENSION DECIDES THE CLASS, from CLASS_TABLE. Directory names carry no
//    meaning: merge obj/ into models/ or don't, nothing regenerates
//    differently. models/ holding four kinds of file is why directory-as-class
//    cannot work, and .png being a sprite in sprites/ and a material map in
//    textures/harsh_bricks/ is why extension alone cannot either. Depth 1 is
//    what resolves the second one.
//
// An unknown extension at depth 1 is an ERROR NAMING THE FILE, not a skip --
// that is the first of the two forced stops when a new asset kind arrives (the
// second is the link error for its decoder). IGNORED_EXTENSIONS is therefore a
// decision on the record rather than a fallthrough: .mtl and .skeleton are read
// at runtime and are packed, they are simply never given an id, because the
// file format that names them uses a sibling path as their identity and an id
// would be a second, weaker copy of it.
//
// ---------------------------------------------------------------------------
// Output grammar
// ---------------------------------------------------------------------------
//
//   manifest     -> comment* class_block*
//
//   comment      -> '#' <to end of line>
//
//   class_block  -> 'class' IDENTIFIER IDENTIFIER PATH EXTENSION+ NEWLINE entry+
//                          ^class name ^value type ^header    ^what it decodes
//
//   entry        -> IDENTIFIER (PATH | '-') NEWLINE
//
// The extension list is what the class DECODES, not what the tree happens to
// hold: def_gen turns each one into a decode_<ext> the loader dispatches on, and
// a class's loader also serves PATH-REFERENCED files that were never
// enumerated. Deriving the list from the entries instead would mean a format
// stopped being loadable the day the last file of it left the tree.
//
// Entry 0 of every class is `Missing` with '-' for its path: it has no file,
// its bytes are a compiled-in constant, and that is what makes a placeholder
// infallible by construction. Every other entry's path is relative to the
// project root with forward slashes -- the ONE spelling read_asset_bytes takes,
// so the same string keys the loose backend, the pkg index and the embed blob.
//
// Ids are positional and NOT stable: adding a file renumbers everything after
// it in its class. Names are the on-disk identity, which is why they are what a
// .source map file stores and why a minted name may not be mangled.
//

#define _CRT_SECURE_NO_WARNINGS // fopen

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../shared/asset_package.hpp"

namespace
{

struct class_row_t
{
  const char* extension;
  const char* class_name;
  const char* value_type;
  const char* value_header; // where value_type is declared, for the generated state
};

// The one place project knowledge lives. def_gen has none: it never sees a
// directory, an extension or a header name, only what these rows produce.
//
// The class name is the value type minus "_t" wherever the type is the asset
// system's own; hitbox_rig breaks that, because hitbox_rig_t is a domain type
// hitscan reads and renaming it to fit a naming rule would be the tail wagging
// the dog. The columns are written out rather than derived for exactly that
// reason -- and a wrong one fails at compile time in the generated state.
//
// Order matters: it is the order the classes appear in the manifest, and
// therefore the order def_gen emits them. Adding a row is harmless (ids are per
// class); adding an EXTENSION to an existing class is not -- it renumbers that
// class, and ids are not stable across that.
constexpr class_row_t CLASS_TABLE[] = {
    {".obj", "mesh_asset", "mesh_asset_t", "asset_types.hpp"},
    {".mesh", "mesh_asset", "mesh_asset_t", "asset_types.hpp"},
    {".png", "texture_asset", "texture_asset_t", "asset_types.hpp"},
    {".tga", "texture_asset", "texture_asset_t", "asset_types.hpp"},
    {".wav", "sound_asset", "sound_asset_t", "asset_types.hpp"},
    {".animation", "animation_asset", "animation_asset_t", "animation.hpp"},
    {".hitboxes", "hitbox_rig", "hitbox_rig_t", "hitbox_rig.hpp"},
    {".ttf", "font_asset", "font_asset_t", "asset_types.hpp"},
};

// Present at depth 1, never given an id. Each one is a decision with a reason:
//
//   .skeleton  a .mesh and an .animation name theirs as a bare sibling from
//              INSIDE the file, so the sibling path is the identity the format
//              itself uses. Minting skeleton_asset::Rig on top of that gives
//              one skeleton two names, and two names is how bone 7 stops being
//              one bone.
//   .mtl       named from inside an .obj, same shape.
//   .md        documentation that happens to sit in a resource directory.
constexpr const char* IGNORED_EXTENSIONS[] = {".skeleton", ".mtl", ".md"};

// Not resources in the runtime sense. blender/ is source art; shaders/ is
// compiled to SPIR-V by the build on a path of its own.
constexpr const char* EXCLUDED_DIRECTORIES[] = {"blender", "shaders"};

// The package is WIDER than the id space and narrower than the tree. Wider,
// because .mtl and .skeleton are named from inside another asset and are
// mandatory at runtime, and because everything NESTED is path-referenced and
// never enumerated at all. Narrower by exactly this list: documentation that
// happens to sit in a resource directory is not read by the game, and shipping
// it would put a README in .rodata.
constexpr const char* UNPACKED_EXTENSIONS[] = {".md"};

bool extension_is_unpacked(const std::string& extension)
{
  for (const char* unpacked : UNPACKED_EXTENSIONS)
  {
    if (extension == unpacked)
      return true;
  }
  return false;
}

int32_t error_count = 0;

void report_error(const char* format, ...)
{
  va_list arguments;
  va_start(arguments, format);
  fprintf(stderr, "asset_pack: error: ");
  vfprintf(stderr, format, arguments);
  fprintf(stderr, "\n");
  va_end(arguments);
  ++error_count;
}

const class_row_t* find_class_row(const std::string& extension)
{
  for (const class_row_t& row : CLASS_TABLE)
  {
    if (extension == row.extension)
      return &row;
  }
  return nullptr;
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

bool directory_is_excluded(const std::string& name)
{
  for (const char* excluded : EXCLUDED_DIRECTORIES)
  {
    if (name == excluded)
      return true;
  }
  return false;
}

// Basename minus extension, case preserved, and it must ALREADY be a valid C++
// identifier. There is deliberately no mangling rule: the minted name is the
// on-disk identity written into .source map files, so a mangling rule is a way
// for two files to quietly claim one name, and a map saved yesterday has to
// load today.
bool name_is_mintable(const std::string& stem)
{
  if (stem.empty())
    return false;

  const char first = stem[0];
  if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_'))
    return false;

  for (char character : stem)
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

struct entry_t
{
  std::string name;
  std::string path; // project-root relative, forward slashes
};

struct class_bucket_t
{
  const char*          class_name;
  const char*          value_type;
  const char*          value_header;
  std::vector<entry_t> entries;
};

// The directory this walk was pointed at, spelled the way the game spells it:
// the prefix every emitted path carries. Derived from the argument rather than
// hardcoded, so pointing the tool at a copy of the tree produces a manifest
// that names the copy.
std::string path_prefix;

std::string to_logical_path(const std::string& relative)
{
  return path_prefix + "/" + relative;
}

class_bucket_t* bucket_for(std::vector<class_bucket_t>& buckets, const class_row_t* row)
{
  for (class_bucket_t& bucket : buckets)
  {
    if (strcmp(bucket.class_name, row->class_name) == 0)
      return &bucket;
  }
  buckets.push_back({row->class_name, row->value_type, row->value_header, {}});
  return &buckets.back();
}

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

// ONE traversal, two outputs. Depth 1 is the id space; anything nested is the
// path-referenced pool, packed but never enumerated. Both fall out of the same
// pass, which is the point -- a second walk could disagree with this one about
// what exists, and a package built from the disagreeing half ships a game that
// resolves the wrong mesh.
void walk_directory(const std::filesystem::path& root, const std::string& relative_directory,
                    uint32_t depth, std::vector<class_bucket_t>& buckets,
                    std::vector<assets::asset_package_input_t>* package_files)
{
  const std::filesystem::path directory = root / relative_directory;

  std::error_code                    failure;
  std::vector<std::filesystem::path> files;
  std::vector<std::string>           subdirectories;
  for (const std::filesystem::directory_entry& item :
       std::filesystem::directory_iterator(directory, failure))
  {
    if (item.is_regular_file())
      files.push_back(item.path());
    else if (item.is_directory())
      subdirectories.push_back(item.path().filename().generic_string());
  }
  if (failure)
  {
    report_error("cannot read '%s': %s", directory.string().c_str(), failure.message().c_str());
    return;
  }

  // directory_iterator's order is unspecified, and an unspecified order would
  // make asset ids depend on the filesystem.
  std::sort(files.begin(), files.end(),
            [](const std::filesystem::path& left, const std::filesystem::path& right)
            { return left.generic_string() < right.generic_string(); });
  std::sort(subdirectories.begin(), subdirectories.end());

  for (const std::filesystem::path& file : files)
  {
    std::string extension = file.extension().string();
    for (char& character : extension)
      character = (char)tolower((unsigned char)character);

    const std::string relative = relative_directory + "/" + file.filename().generic_string();
    const std::string logical  = to_logical_path(relative);

    if (package_files != nullptr && !extension_is_unpacked(extension))
    {
      std::vector<uint8_t> bytes;
      if (read_whole_file(file, bytes))
        package_files->push_back({logical, std::move(bytes)});
      else
        report_error("cannot read '%s' for the package", logical.c_str());
    }

    // Below depth 1 there is no id space: these are the files another asset
    // names by path from inside itself, and giving them ids would be a second,
    // weaker copy of an identity the file format already has.
    if (depth != 1)
      continue;

    if (extension_is_ignored(extension))
      continue;

    const class_row_t* row = find_class_row(extension);
    if (row == nullptr)
    {
      report_error("'%s' has extension '%s', which no asset class claims. Add a row to "
                   "CLASS_TABLE in asset_pack.cpp, or add the extension to IGNORED_EXTENSIONS "
                   "if it is never referenced by id",
                   logical.c_str(), extension.empty() ? "(none)" : extension.c_str());
      continue;
    }

    const std::string stem = file.stem().string();
    if (!name_is_mintable(stem))
    {
      report_error("'%s' cannot become an identifier, so it cannot be an asset name. Rename the "
                   "file: a minted name is never mangled, because the name is what a .source map "
                   "file stores",
                   logical.c_str());
      continue;
    }

    class_bucket_t* bucket = bucket_for(buckets, row);
    for (const entry_t& existing : bucket->entries)
    {
      if (existing.name != stem)
        continue;
      report_error("asset class '%s' has two entries named '%s' ('%s' and '%s'); rename one",
                   row->class_name, stem.c_str(), existing.path.c_str(), logical.c_str());
    }

    bucket->entries.push_back({stem, logical});
  }

  for (const std::string& name : subdirectories)
    walk_directory(root, relative_directory + "/" + name, depth + 1, buckets, package_files);
}

// Write-if-different: the manifest is a build input, so rewriting it with
// identical bytes would re-run def_gen and rebuild the world on every build.
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
    report_error("cannot write '%s'", path.string().c_str());
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
  const char* manifest_path       = nullptr;
  const char* package_path        = nullptr;

  for (int index = 1; index < argument_count; ++index)
  {
    if (strcmp(arguments[index], "--manifest") == 0 && index + 1 < argument_count)
    {
      manifest_path = arguments[++index];
      continue;
    }
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

  if (resources_directory == nullptr || manifest_path == nullptr)
  {
    fprintf(stderr, "usage: asset_pack <resources-dir> --manifest <path> [--package <path>]\n"
                    "\n"
                    "Walks <resources-dir> and writes the asset manifest def_gen reads.\n"
                    "--package additionally writes assets.pkg from the SAME walk: the bytes of\n"
                    "every file the game can reach at runtime, keyed by the one path spelling\n"
                    "read_asset_bytes takes.\n"
                    "Both are write-if-different, so an unchanged tree rebuilds nothing.\n");
    return 1;
  }

  const std::filesystem::path root = std::filesystem::path(resources_directory);
  path_prefix                      = root.filename().generic_string();
  if (path_prefix.empty()) // a trailing slash leaves filename() empty
    path_prefix = root.parent_path().filename().generic_string();

  std::error_code failure;
  if (!std::filesystem::is_directory(root, failure))
  {
    fprintf(stderr, "asset_pack: error: '%s' is not a directory\n", resources_directory);
    return 1;
  }

  // Every class in the table gets a block whether or not the tree holds one of
  // its files, so the emitted enum exists from the day the row does rather than
  // from the day the first file of that kind arrives.
  std::vector<class_bucket_t> buckets;
  for (const class_row_t& row : CLASS_TABLE)
    bucket_for(buckets, &row);

  std::vector<assets::asset_package_input_t>  package_files;
  std::vector<assets::asset_package_input_t>* package_sink =
      package_path != nullptr ? &package_files : nullptr;

  std::vector<std::string> subdirectories;
  for (const std::filesystem::directory_entry& item :
       std::filesystem::directory_iterator(root, failure))
  {
    if (item.is_directory())
    {
      const std::string name = item.path().filename().generic_string();
      if (!directory_is_excluded(name))
        subdirectories.push_back(name);
      continue;
    }
    // Depth 0 is neither the id space nor the path-referenced pool, so no rule
    // covers it. Say so rather than silently dropping it.
    report_error("'%s/%s' sits directly in the resource root, which is neither depth 1 (the id "
                 "space) nor nested (path-referenced); move it into a subdirectory",
                 path_prefix.c_str(), item.path().filename().generic_string().c_str());
  }
  std::sort(subdirectories.begin(), subdirectories.end());

  for (const std::string& name : subdirectories)
    walk_directory(root, name, 1, buckets, package_sink);

  if (error_count > 0)
  {
    fprintf(stderr, "asset_pack: %d error%s\n", error_count, error_count == 1 ? "" : "s");
    return 1;
  }

  std::string text = "# generated by asset_pack -- do not edit\n";
  text += "# One walk of the resource tree. Ids are positional and not stable; names are.\n";

  for (const class_bucket_t& bucket : buckets)
  {
    text += "\nclass ";
    text += bucket.class_name;
    text += " ";
    text += bucket.value_type;
    text += " ";
    text += bucket.value_header;
    for (const class_row_t& row : CLASS_TABLE)
    {
      if (strcmp(row.class_name, bucket.class_name) != 0)
        continue;
      text += " ";
      text += row.extension;
    }
    text += "\n";
    // Entry 0, with no file behind it. Its bytes are a compiled-in constant, so
    // "this id resolves to nothing" is not representable.
    text += "  Missing -\n";
    for (const entry_t& entry : bucket.entries)
    {
      text += "  ";
      text += entry.name;
      text += " ";
      text += entry.path;
      text += "\n";
    }
  }

  if (!write_if_different(std::filesystem::path(manifest_path), text))
    return 1;

  if (package_path != nullptr)
  {
    const std::vector<uint8_t> package = assets::build_asset_package(package_files);
    const std::string          bytes(reinterpret_cast<const char*>(package.data()), package.size());
    if (!write_if_different(std::filesystem::path(package_path), bytes))
      return 1;
    fprintf(stderr, "asset_pack: package holds %zu files, %zu bytes\n", package_files.size(),
            package.size());
  }

  return 0;
}
