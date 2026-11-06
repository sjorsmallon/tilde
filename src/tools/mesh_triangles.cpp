// Writes a mesh asset's triangles as the game decodes them, welded, in asset space, as an OBJ: what
// scripts/decompose_mesh.py decomposes, so the axes, units and node transforms are the decoder's and are
// spelled nowhere else (geometry_def.md ss11.6).
//
// Usage: mesh_triangles <mesh-path> <out.obj>

#include "assets/asset.hpp"
#include "assets/asset_state.hpp"
#include "log.hpp"
#include "map/map_geometry_asset.hpp"
#include "map/mesh/mesh.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
  if (argc != 3)
  {
    std::printf("usage: mesh_triangles <mesh-path> <out.obj>\n");
    return 2;
  }
  const std::string mesh_path = argv[1];
  const std::string out_path  = argv[2];

  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::mount_asset_source();
  assets::number_asset_ids_from_tree(asset_state, "resources");

  if (!assets::asset_exists(mesh_path.c_str()))
  {
    std::printf("mesh_triangles: '%s' is not an asset\n", mesh_path.c_str());
    return 1;
  }
  const assets::mesh_asset_t*   asset = assets::get(assets::load_mesh(mesh_path.c_str()));
  const shared::mesh_geometry_t mesh  = shared::make_mesh_from_asset_triangles(*asset, {1.f, 1.f, 1.f});

  FILE* file = std::fopen(out_path.c_str(), "wb");
  if (file == nullptr)
  {
    std::printf("mesh_triangles: cannot write '%s'\n", out_path.c_str());
    return 1;
  }
  for (const linalg::vec3& vertex : mesh.vertices)
    std::fprintf(file, "v %.9g %.9g %.9g\n", vertex.x, vertex.y, vertex.z);
  for (const shared::mesh_face_t& face : mesh.faces)
    std::fprintf(file, "f %u %u %u\n", face.vertex_indices[0] + 1, face.vertex_indices[1] + 1,
                 face.vertex_indices[2] + 1);
  std::fclose(file);

  const std::optional<shared::mesh_defect_t> defect = shared::find_mesh_defect(mesh);
  std::printf("%s: %zu vertices, %zu triangles, %s\n", mesh_path.c_str(), mesh.vertices.size(), mesh.faces.size(),
              defect ? ("not a solid: " + shared::format_mesh_defect(*defect)).c_str() : "a closed solid");
  return 0;
}
