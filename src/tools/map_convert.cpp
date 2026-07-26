// One-time map converter for the geometry exit.
//
// Loading a map already performs the conversion in memory (see
// convert_legacy_geometry_entity in map.cpp), so converting a file is exactly
// "load it, save it". This tool exists so that can be done deliberately, to
// every map at once, with a report of what changed — rather than discovered one
// map at a time by opening each in the editor.
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
  size_t entities = 0;
  bool was_legacy = false; // the file still held geometry as entity blocks
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

  shared::map_t map;
  if (!shared::load_map(path, map))
  {
    log_error("map_convert: failed to load '{}'", path);
    return false;
  }

  const conversion_report_t report = inspect(map, original_text);

  std::printf("%s: %zu box, %zu static_mesh, %zu displacement, %zu entities%s\n",
              path.c_str(), report.boxes, report.static_meshes,
              report.displacements, report.entities,
              report.was_legacy ? "  [was legacy]" : "  [already converted]");

  if (check_only)
    return true;

  if (!report.was_legacy)
    return true; // nothing to rewrite; leave the file (and its mtime) alone

  // Keep a copy of the pre-conversion file. The conversion is one-way, and the
  // wedges it drops are real data.
  std::error_code error;
  std::filesystem::copy_file(path, path + ".preconvert.bak",
                             std::filesystem::copy_options::overwrite_existing,
                             error);
  if (error)
  {
    log_error("map_convert: could not back up '{}' ({}) — refusing to overwrite it",
              path, error.message());
    return false;
  }

  if (!shared::save_map(path, map))
  {
    log_error("map_convert: failed to save '{}'", path);
    return false;
  }

  std::printf("  converted (backup at %s.preconvert.bak)\n", path.c_str());
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
