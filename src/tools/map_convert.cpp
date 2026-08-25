// Map converter: rewrites a map file into the form the current save path emits.
//
// Loading a map already performs every conversion in memory — the geometry exit
// (convert_legacy_geometry_entity) and the P5 entity-text changes (legacy enum
// spellings, retired keys) — so converting a file is exactly "load it, save
// it". This tool exists so that can be done deliberately, to every map at once,
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
#include "map.hpp"
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

struct conversion_report_t
{
  size_t boxes = 0;
  size_t static_meshes = 0;
  size_t displacements = 0;
  size_t brushes = 0;
  size_t entities = 0;
  bool was_legacy = false;      // the file still held geometry as entity blocks
  bool needs_rewrite = false;   // the file's text is not what save_map writes
};

conversion_report_t inspect(const shared::map_t &map, const std::string &original_text)
{
  conversion_report_t report;

  for (const shared::map_geometry_t &entry : map.geometry)
  {
    switch (shared::get_kind(entry.value))
    {
    case shared::geometry_kind_t::Box:          ++report.boxes; break;
    case shared::geometry_kind_t::Static_Mesh:  ++report.static_meshes; break;
    case shared::geometry_kind_t::Displacement: ++report.displacements; break;
    case shared::geometry_kind_t::Brush:        ++report.brushes; break;
    }
  }

  report.entities = map.entities.size();

  // If the source text mentions any of the retired geometry classnames, this
  // file was in the pre-exit form and the load just converted it.
  for (const char *classname : {"\"aabb_entity\"", "\"static_mesh_entity\"",
                                "\"displacement_entity\"", "\"wedge_entity\""})
  {
    if (original_text.find(classname) != std::string::npos)
    {
      report.was_legacy = true;
      break;
    }
  }

  // The geometry exit is no longer the only thing a load converts: the P5
  // cutover also rewrote entity text (legacy numeric/lowercase enum values,
  // retired keys like entity_id). Rather than enumerate those, ask the real
  // question — is the text on disk what save_map would write? Anything that
  // load_map silently fixed up shows as a difference here.
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

  const char *status = "  [canonical]";
  if (report.was_legacy)
    status = "  [was legacy geometry]";
  else if (report.needs_rewrite)
    status = "  [not canonical]";

  std::printf("%s: %zu box, %zu static_mesh, %zu displacement, %zu brush, "
              "%zu entities%s\n",
              path.c_str(), report.boxes, report.static_meshes,
              report.displacements, report.brushes, report.entities, status);

  if (check_only)
    return true;

  if (!report.needs_rewrite)
    return true; // nothing to rewrite; leave the file (and its mtime) alone

  // Keep a copy of the pre-conversion file. The conversion is one-way, and the
  // wedges it drops are real data.
  //
  // Never overwrite an existing backup: a map converted once already (the
  // geometry exit) has a .preconvert.bak holding the ORIGINAL, and clobbering
  // it with today's content would quietly destroy the only copy of it. Maps are
  // not in version control, so this is the only safety net there is.
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

  bool all_ok = true;
  for (const std::string &path : paths)
    all_ok = convert_one(path, check_only) && all_ok;

  return all_ok ? 0 : 1;
}
