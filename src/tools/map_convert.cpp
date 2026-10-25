// Map converter: rewrites a map file into the form the current save path emits.
//
// Loading a map already performs every conversion in memory — the P5
// entity-text changes (legacy enum spellings, retired keys) — so converting a
// file is exactly "load it, save it". This tool exists so that can be done deliberately, to every map at once,
// with a report of what changed, rather than discovered one map at a time by
// opening each in the editor.
//
// "Needs a rewrite" is decided by comparing the file against what save_map
// would write, so it stays correct as further conversions are added; nothing
// here enumerates them.
//
// Usage:
//   map_convert <map-file> [<map-file> ...]     convert in place (writes a .bak)
//   map_convert --check <map-file> [...]        report only, write nothing
//
// The .navmesh sidecar is untouched (save_map rewrites it from the loaded
// navmesh, which load_map read back unchanged).

#include "log.hpp"
#include "assets/asset_state.hpp"
#include "map/map.hpp"
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

struct conversion_report_t
{
  size_t asset_meshes = 0;
  size_t subdivided_faces = 0;
  size_t meshes = 0;
  size_t entities = 0;
  bool needs_rewrite = false;   // the file's text is not what save_map writes
};

conversion_report_t inspect(const shared::map_t &map, const std::string &original_text)
{
  conversion_report_t report;

  for (const shared::map_geometry_t &entry : map.geometry)
  {
    const shared::mesh_geometry_t &mesh = entry.value;
    ++report.meshes;
    if (std::holds_alternative<shared::asset_mesh_source_t>(mesh.source))
      ++report.asset_meshes;
    for (const shared::mesh_face_t &face : mesh.faces)
      if (face.surface.subdivision_level > 0)
        ++report.subdivided_faces;
  }

  report.entities = map.entities.size();

  // Rather than enumerate what a load converts (legacy numeric/lowercase enum
  // values, retired keys like entity_id), ask the real question — is the text
  // on disk what save_map would write? Anything that load_map silently fixed up
  // shows as a difference here.
  //
  // Compared with line endings normalized: save_map writes through a text-mode
  // ofstream, so on Windows the bytes on disk carry \r\n while
  // serialize_map_to_string emits \n. Without this every map on Windows would
  // report as needing a rewrite, forever.
  std::string on_disk = original_text;
  std::erase(on_disk, '\r');
  std::string canonical = shared::serialize_map_to_string(map);
  std::erase(canonical, '\r');
  report.needs_rewrite = canonical != on_disk;

  return report;
}

std::string read_file(const std::string &path)
{
  FILE *file = std::fopen(path.c_str(), "rb");
  if (!file)
    return {};

  std::string content;
  char buffer[4096];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    content.append(buffer, read);
  std::fclose(file);
  return content;
}

bool convert_one(const std::string &path, bool check_only)
{
  if (!std::filesystem::exists(path))
  {
    log_error("map_convert: '{}' does not exist", path);
    return false;
  }

  const std::string original_text = read_file(path);

  std::optional<shared::map_t> loaded = shared::try_load_map(path);
  if (!loaded)
  {
    log_error("map_convert: failed to load '{}'", path);
    return false;
  }
  shared::map_t &map = *loaded;

  const conversion_report_t report = inspect(map, original_text);

  const char *status = report.needs_rewrite ? "  [not canonical]" : "  [canonical]";

  std::printf("%s: %zu mesh (%zu from an asset, %zu subdivided faces), %zu entities%s\n",
              path.c_str(), report.meshes, report.asset_meshes, report.subdivided_faces,
              report.entities, status);

  if (check_only)
    return true;

  if (!report.needs_rewrite)
    return true; // nothing to rewrite; leave the file (and its mtime) alone

  // Keep a copy of the pre-conversion file: the conversion is one-way. Never
  // overwrite an existing backup, which may hold an older original.
  std::string backup_path = path + ".preconvert.bak";
  for (int attempt = 1; std::filesystem::exists(backup_path); ++attempt)
    backup_path = path + ".preconvert." + std::to_string(attempt) + ".bak";

  std::error_code error;
  std::filesystem::copy_file(path, backup_path, error);
  if (error)
  {
    log_error("map_convert: could not back up '{}' to '{}' ({}) — refusing to "
              "overwrite it",
              path, backup_path, error.message());
    return false;
  }

  if (!shared::save_map(path, map))
  {
    log_error("map_convert: failed to save '{}'", path);
    return false;
  }

  std::printf("  converted (backup at %s)\n", backup_path.c_str());
  return true;
}

} // namespace

int main(int argc, char **argv)
{
  bool check_only = false;
  std::vector<std::string> paths;

  for (int i = 1; i < argc; ++i)
  {
    const std::string argument = argv[i];
    if (argument == "--check")
      check_only = true;
    else
      paths.push_back(argument);
  }

  if (paths.empty())
  {
    std::printf("usage: map_convert [--check] <map-file> [<map-file> ...]\n");
    return 2;
  }

  // A map names assets by field; the ids come from the tree, as in the game.
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  bool all_ok = true;
  for (const std::string &path : paths)
    all_ok = convert_one(path, check_only) && all_ok;

  return all_ok ? 0 : 1;
}
