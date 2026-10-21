#include "map_geometry_mesh.hpp"

#include "../collision/collision_detection.hpp"
#include "log.hpp"
#include "map_geometry_text.hpp"
#include "brush/brush_holes.hpp"
#include "mesh/mesh_decomposition.hpp"
#include "mesh/mesh_primitives.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace shared
{

static_mesh_geometry_t static_mesh_view_of_asset_mesh(const mesh_geometry_t &mesh,
                                                      const asset_mesh_source_t &source)
{
  static_mesh_geometry_t view;
  view.position = mesh.position;
  view.orientation = mesh.orientation;
  view.scale = source.scale;
  view.surface = mesh.surface;
  view.surface.mesh_path = source.path;
  view.collides = mesh.collides;
  view.casts_shadows = mesh.casts_shadows;
  view.owner_uid = mesh.owner_uid;
  return view;
}

aabb_bounds_t compute_mesh_world_bounds(const mesh_geometry_t &mesh)
{
  if (const asset_mesh_source_t *asset = std::get_if<asset_mesh_source_t>(&mesh.source))
    return get_bounds(geometry_value_t{static_mesh_view_of_asset_mesh(mesh, *asset)});

  if (mesh.vertices.empty())
    return {mesh.position, mesh.position};
  aabb_bounds_t bounds;
  bool first = true;
  for (const linalg::vec3 &vertex : mesh.vertices)
  {
    const linalg::vec3 world = mesh.position + linalg::rotate(mesh.orientation, vertex);
    if (first)
    {
      bounds = {world, world};
      first = false;
    }
    else
      expand_aabb_to_include_point(bounds, world);
  }
  return bounds;
}

linalg::vec3 compute_mesh_world_half_extents(const mesh_geometry_t &mesh)
{
  const aabb_bounds_t bounds = compute_mesh_world_bounds(mesh);
  return (bounds.max - bounds.min) * 0.5f;
}

void scale_mesh_to_world_box(mesh_geometry_t &mesh, const aabb_t &box)
{
  const aabb_bounds_t bounds = compute_mesh_world_bounds(mesh);
  const linalg::vec3 old_center = (bounds.min + bounds.max) * 0.5f;
  const linalg::vec3 old_half_extents = (bounds.max - bounds.min) * 0.5f;
  const linalg::vec3 factor{
      old_half_extents.x > 1e-4f ? box.half_extents.x / old_half_extents.x : 1.f,
      old_half_extents.y > 1e-4f ? box.half_extents.y / old_half_extents.y : 1.f,
      old_half_extents.z > 1e-4f ? box.half_extents.z / old_half_extents.z : 1.f,
  };
  const auto scale_world = [&factor](const linalg::vec3 &v)
  { return linalg::vec3{v.x * factor.x, v.y * factor.y, v.z * factor.z}; };

  mesh.position = box.center + scale_world(mesh.position - old_center);

  if (asset_mesh_source_t *asset = std::get_if<asset_mesh_source_t>(&mesh.source))
  {
    const linalg::vec3 local_axes[3] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
    float *scale[3] = {&asset->scale.x, &asset->scale.y, &asset->scale.z};
    const float factors[3] = {factor.x, factor.y, factor.z};
    for (int axis = 0; axis < 3; ++axis)
    {
      const linalg::vec3 world = linalg::rotate(mesh.orientation, local_axes[axis]);
      const float magnitudes[3] = {std::fabs(world.x), std::fabs(world.y), std::fabs(world.z)};
      int dominant = 0;
      for (int candidate = 1; candidate < 3; ++candidate)
        if (magnitudes[candidate] > magnitudes[dominant])
          dominant = candidate;
      *scale[axis] *= factors[dominant];
    }
    return;
  }

  const linalg::quatf inverse_orientation = linalg::conjugate(mesh.orientation);
  const auto scale_local = [&](const linalg::vec3 &v)
  { return linalg::rotate(inverse_orientation, scale_world(linalg::rotate(mesh.orientation, v))); };
  for (linalg::vec3 &vertex : mesh.vertices)
    vertex = scale_local(vertex);
  for (mesh_face_t &face : mesh.faces)
    for (linalg::vec3 &offset : face.surface.offsets)
      offset = scale_local(offset);
  (void)freeze_mesh_recipe(mesh);
}

std::vector<collision_piece_t> compute_mesh_collision_pieces(const mesh_geometry_t &mesh,
                                                             entity_uid_t uid)
{
  if (!mesh.collides)
    return {};
  if (const asset_mesh_source_t *asset = std::get_if<asset_mesh_source_t>(&mesh.source))
    return {static_mesh_collision_box(static_mesh_view_of_asset_mesh(mesh, *asset))};

  uint32_t sculpted_faces = 0;
  for (const mesh_face_t &face : mesh.faces)
    if (face_is_subdivided(face.surface) && face.vertex_indices.size() == 4)
      ++sculpted_faces;
  if (sculpted_faces > 0)
  {
    if (!mesh_is_convex(mesh))
      log_error("get_collision_pieces: mesh {} is sculpted and not convex - its grids collide "
                "flat (geometry_def.md 11.8 increment 12)",
                uid);
    else
    {
      const brush_polyhedron_t polyhedron = compute_mesh_world_polyhedron(mesh);
      brush_face_grids_t grids;
      grids.grid_vertices.resize(mesh.faces.size());
      grids.grid_weights.resize(mesh.faces.size());
      grids.grid_pinned.resize(mesh.faces.size());
      grids.grid_corners.resize(mesh.faces.size());
      for (uint32_t face_index = 0; face_index < (uint32_t)mesh.faces.size(); ++face_index)
      {
        const mesh_face_t &face = mesh.faces[face_index];
        if (!face_is_subdivided(face.surface) || face.vertex_indices.size() != 4)
          continue;
        grids.grid_vertices[face_index] = compute_mesh_face_grid_world_vertices(mesh, face_index);
        for (size_t corner = 0; corner < 4; ++corner)
          grids.grid_corners[face_index].corners[corner] =
              polyhedron.vertices[face.vertex_indices[corner]];
        grids.any = true;
      }
      if (std::optional<std::vector<collision_piece_t>> columns =
              try_build_subdivided_face_columns(polyhedron, grids))
        return std::move(*columns);
      log_error("get_collision_pieces: mesh {} carries {} sculpted faces and the column path "
                "takes one - its grids collide flat",
                uid, sculpted_faces);
    }
  }

  const std::optional<std::vector<mesh_geometry_t>> convex_pieces =
      try_decompose_mesh_into_convex_pieces(mesh);
  if (!convex_pieces)
  {
    log_error("get_collision_pieces: mesh {} with {} faces does not decompose - it will not "
              "collide",
              uid, mesh.faces.size());
    return {};
  }
  std::vector<collision_piece_t> pieces;
  pieces.reserve(convex_pieces->size());
  for (const mesh_geometry_t &piece : *convex_pieces)
    pieces.push_back(collision_piece_from_convex_mesh(piece));
  return pieces;
}

light_occlusion_t compute_mesh_light_occlusion(const mesh_geometry_t &mesh,
                                               Span<const std::string> materials)
{
  if (!mesh.casts_shadows)
    return light_occlusion_t::None;
  if (mesh.faces.empty())
    return light_occlusion_t::Opaque;

  bool every_face_blends = true;
  bool every_face_is_tested = true;
  for (const mesh_face_t &face : mesh.faces)
  {
    const std::string path = face.surface.material < materials.count
                                 ? materials[face.surface.material]
                                 : std::string();
    const assets::alpha_mode_t mode = resolve_material_maps(path).alpha_mode;
    every_face_blends = every_face_blends && mode == assets::alpha_mode_t::blend;
    every_face_is_tested = every_face_is_tested && mode == assets::alpha_mode_t::cutout;
  }
  if (every_face_blends)
    return light_occlusion_t::Transmissive;
  if (every_face_is_tested)
    return light_occlusion_t::Alpha_Tested;
  return light_occlusion_t::Opaque;
}

Plane compute_mesh_world_face_plane(const mesh_geometry_t &mesh, uint32_t face_index)
{
  const Plane local = compute_mesh_face_plane(mesh, face_index);
  Plane world;
  world.normal = linalg::rotate(mesh.orientation, local.normal);
  world.point = mesh.position + linalg::rotate(mesh.orientation, local.point);
  return world;
}

std::optional<uint32_t> try_find_mesh_face_by_world_plane(const mesh_geometry_t &mesh,
                                                          const Plane &plane)
{
  for (uint32_t face_index = 0; face_index < (uint32_t)mesh.faces.size(); ++face_index)
    if (planes_coincide(compute_mesh_world_face_plane(mesh, face_index), plane))
      return face_index;
  return std::nullopt;
}

std::vector<linalg::vec3> compute_mesh_face_grid_world_vertices(const mesh_geometry_t &mesh,
                                                                uint32_t face_index)
{
  const mesh_face_t &face = mesh.faces[face_index];
  if (face.vertex_indices.size() != 4 || !face_is_subdivided(face.surface))
    fatal_error("compute_mesh_face_grid_world_vertices: face {} carries no grid", face_index);

  face_grid_t grid;
  for (size_t corner = 0; corner < 4; ++corner)
    grid.corners[corner] =
        mesh.position + linalg::rotate(mesh.orientation, mesh.vertices[face.vertex_indices[corner]]);

  const int level = face.surface.subdivision_level;
  const int size = face_grid_size(level);
  std::vector<linalg::vec3> vertices((size_t)face_grid_vertex_count(level));
  for (int j = 0; j < size; ++j)
  {
    for (int i = 0; i < size; ++i)
    {
      const size_t vertex = (size_t)(j * size + i);
      vertices[vertex] = face_grid_base_vertex(grid, level, i, j) +
                         linalg::rotate(mesh.orientation, face.surface.offsets[vertex]);
    }
  }
  return vertices;
}

brush_polyhedron_t compute_mesh_world_polyhedron(const mesh_geometry_t &mesh)
{
  brush_polyhedron_t polyhedron;
  polyhedron.vertices.reserve(mesh.vertices.size());
  for (const linalg::vec3 &vertex : mesh.vertices)
    polyhedron.vertices.push_back(mesh.position + linalg::rotate(mesh.orientation, vertex));
  polyhedron.faces.reserve(mesh.faces.size());
  for (uint32_t face_index = 0; face_index < (uint32_t)mesh.faces.size(); ++face_index)
  {
    brush_face_t face;
    face.plane = compute_mesh_world_face_plane(mesh, face_index);
    face.vertex_indices = mesh.faces[face_index].vertex_indices;
    polyhedron.faces.push_back(std::move(face));
  }
  return polyhedron;
}

namespace
{

bool mesh_face_carries_grid(const mesh_face_t &face)
{
  return face.vertex_indices.size() == 4 && face_is_subdivided(face.surface);
}

float stroke_falloff(float distance, float radius)
{
  const float t = 1.f - distance / radius;
  return t * t * (3.f - 2.f * t);
}

} // namespace

bool mesh_has_subdivided_face(const mesh_geometry_t &mesh)
{
  for (const mesh_face_t &face : mesh.faces)
    if (mesh_face_carries_grid(face))
      return true;
  return false;
}

std::optional<mesh_face_hit_t> try_pick_mesh_face(const mesh_geometry_t &mesh,
                                                  const linalg::vec3 &ray_origin,
                                                  const linalg::vec3 &ray_direction)
{
  std::optional<mesh_face_hit_t> nearest;
  const auto consider = [&](uint32_t face_index, const linalg::vec3 &a, const linalg::vec3 &b,
                            const linalg::vec3 &c) {
    const linalg::vec3 cross = linalg::cross(b - a, c - a);
    if (linalg::dot(cross, ray_direction) >= 0.f || linalg::length_squared(cross) <= 1e-12f)
      return;
    float distance = 0.f;
    if (!ray_triangle(ray_origin, ray_direction, a, b, c, distance))
      return;
    if (nearest && nearest->distance <= distance)
      return;

    mesh_face_hit_t hit;
    hit.face_index = face_index;
    hit.position = ray_origin + ray_direction * distance;
    hit.surface_normal = linalg::normalize(cross);
    hit.distance = distance;
    nearest = hit;
  };
  const auto world_vertex = [&](uint32_t vertex) -> linalg::vec3 {
    return mesh.position + linalg::rotate(mesh.orientation, mesh.vertices[vertex]);
  };

  std::vector<linalg::vec3> grid;
  for (uint32_t face_index = 0; face_index < (uint32_t)mesh.faces.size(); ++face_index)
  {
    const mesh_face_t &face = mesh.faces[face_index];
    if (mesh_face_carries_grid(face))
    {
      grid = compute_mesh_face_grid_world_vertices(mesh, face_index);
      const int size = face_grid_size(face.surface.subdivision_level);
      for (int j = 0; j + 1 < size; ++j)
      {
        for (int i = 0; i + 1 < size; ++i)
        {
          const linalg::vec3 &a = grid[(size_t)(j * size + i)];
          const linalg::vec3 &b = grid[(size_t)(j * size + i + 1)];
          const linalg::vec3 &c = grid[(size_t)((j + 1) * size + i + 1)];
          const linalg::vec3 &d = grid[(size_t)((j + 1) * size + i)];
          consider(face_index, a, b, c);
          consider(face_index, a, c, d);
        }
      }
      continue;
    }

    if (face.vertex_indices.size() < 3)
      continue;
    const linalg::vec3 origin = world_vertex(face.vertex_indices[0]);
    for (size_t offset = 1; offset + 1 < face.vertex_indices.size(); ++offset)
      consider(face_index, origin, world_vertex(face.vertex_indices[offset]),
               world_vertex(face.vertex_indices[offset + 1]));
  }
  return nearest;
}

size_t paint_mesh_face_grid_blend(mesh_geometry_t &mesh, const linalg::vec3 &center,
                                  float radius, float amount, int layer)
{
  if (radius <= 0.f || amount <= 0.f)
    return 0;

  size_t painted = 0;
  for (uint32_t face_index = 0; face_index < (uint32_t)mesh.faces.size(); ++face_index)
  {
    mesh_face_t &face = mesh.faces[face_index];
    if (!mesh_face_carries_grid(face))
      continue;

    const std::vector<linalg::vec3> grid = compute_mesh_face_grid_world_vertices(mesh, face_index);
    for (size_t vertex = 0; vertex < grid.size(); ++vertex)
    {
      const float distance = linalg::length(grid[vertex] - center);
      if (distance >= radius)
        continue;
      paint_face_layer_weight(face.surface, vertex, layer, amount * stroke_falloff(distance, radius));
      ++painted;
    }
  }
  return painted;
}

size_t sculpt_mesh_face_grid_vertices(mesh_geometry_t &mesh, const linalg::vec3 &center,
                                      float radius, const linalg::vec3 &delta)
{
  if (radius <= 0.f)
    return 0;

  const linalg::vec3 local_delta = linalg::rotate(linalg::conjugate(mesh.orientation), delta);
  size_t moved = 0;
  for (uint32_t face_index = 0; face_index < (uint32_t)mesh.faces.size(); ++face_index)
  {
    mesh_face_t &face = mesh.faces[face_index];
    if (!mesh_face_carries_grid(face))
      continue;

    const std::vector<linalg::vec3> grid = compute_mesh_face_grid_world_vertices(mesh, face_index);
    for (size_t vertex = 0; vertex < grid.size(); ++vertex)
    {
      const float distance = linalg::length(grid[vertex] - center);
      if (distance >= radius)
        continue;
      face.surface.offsets[vertex] =
          face.surface.offsets[vertex] + local_delta * stroke_falloff(distance, radius);
      ++moved;
    }
  }
  return moved;
}

namespace
{

struct mesh_slot_layers_t
{
  Array<uint16_t, BLEND_LAYER_COUNT> layer;
  bool blends = false;
};

bool mesh_slot_layers_equal(const mesh_slot_layers_t &lhs, const mesh_slot_layers_t &rhs)
{
  if (lhs.blends != rhs.blends)
    return false;
  for (int index = 0; index < BLEND_LAYER_COUNT; ++index)
    if (lhs.layer.data[index] != rhs.layer.data[index])
      return false;
  return true;
}

vertex_blend_t grid_vertex_blend(const face_surface_t &surface, size_t grid_vertex)
{
  vertex_blend_t blend;
  for (int layer = 1; layer < BLEND_LAYER_COUNT; ++layer)
    blend.weight[layer - 1] = face_layer_weight(surface, grid_vertex, layer);
  return blend;
}

uint64_t smoothing_key(uint32_t vertex, int32_t group)
{
  return ((uint64_t)vertex << 32) | (uint32_t)group;
}

} // namespace

assets::mesh_asset_t generate_mesh_geometry_mesh(const mesh_geometry_t &mesh,
                                                 Span<const std::string> materials,
                                                 const object_lightmap_ref_t &lighting)
{
  if (std::holds_alternative<asset_mesh_source_t>(mesh.source))
    fatal_error("generate_mesh_geometry_mesh: an asset-sourced mesh draws as the static mesh "
                "it views");

  mesh_geometry_t world = mesh;
  world.position = {0.f, 0.f, 0.f};
  world.orientation = linalg::quatf::identity();
  for (linalg::vec3 &vertex : world.vertices)
    vertex = mesh.position + linalg::rotate(mesh.orientation, vertex);

  const uint32_t face_count = (uint32_t)world.faces.size();
  std::vector<Plane> planes;
  planes.reserve(face_count);
  for (uint32_t face_index = 0; face_index < face_count; ++face_index)
    planes.push_back(compute_mesh_face_plane(world, face_index));

  std::unordered_map<uint64_t, linalg::vec3> smoothing_sums;
  for (uint32_t face_index = 0; face_index < face_count; ++face_index)
  {
    const mesh_face_t &face = world.faces[face_index];
    if (face.surface.smoothing_group == 0)
      continue;
    const linalg::vec3 weighted =
        planes[face_index].normal * compute_mesh_face_area(world, face_index);
    for (uint32_t vertex : face.vertex_indices)
    {
      linalg::vec3 &sum = smoothing_sums
                              .try_emplace(smoothing_key(vertex, face.surface.smoothing_group),
                                           linalg::vec3{0.f, 0.f, 0.f})
                              .first->second;
      sum = sum + weighted;
    }
  }
  const auto corner_normal = [&](uint32_t face_index, uint32_t vertex) -> linalg::vec3 {
    const int32_t group = world.faces[face_index].surface.smoothing_group;
    if (group == 0)
      return planes[face_index].normal;
    const auto found = smoothing_sums.find(smoothing_key(vertex, group));
    if (found == smoothing_sums.end() || linalg::length_squared(found->second) <= 1e-12f)
      return planes[face_index].normal;
    return linalg::normalize(found->second);
  };

  std::vector<mesh_slot_layers_t> slot_layers;
  std::vector<std::vector<uint32_t>> faces_by_slot;
  bool mesh_blends = false;
  bool mesh_is_lightmapped = false;

  for (uint32_t face_index = 0; face_index < face_count; ++face_index)
  {
    const face_surface_t &surface = world.faces[face_index].surface;
    if (!surface.emits_geometry)
      continue;
    if (lighting.has_bake() &&
        find_chart(*lighting.lightmap, lighting.object_uid, face_index))
      mesh_is_lightmapped = true;

    mesh_slot_layers_t layers;
    layers.blends = face_is_blended(surface);
    for (int layer = 0; layer < BLEND_LAYER_COUNT; ++layer)
      layers.layer.data[layer] = face_layer_material(surface, layers.blends ? layer : 0);
    mesh_blends = mesh_blends || layers.blends;

    size_t slot = 0;
    while (slot < slot_layers.size() && !mesh_slot_layers_equal(slot_layers[slot], layers))
      ++slot;
    if (slot == slot_layers.size())
    {
      slot_layers.push_back(layers);
      faces_by_slot.emplace_back();
    }
    faces_by_slot[slot].push_back(face_index);
  }

  assets::mesh_asset_t out;
  std::vector<linalg::vec3> grid_vertices;

  for (size_t slot = 0; slot < slot_layers.size(); ++slot)
  {
    const uint32_t index_start = (uint32_t)out.indices.size();

    for (uint32_t face_index : faces_by_slot[slot])
    {
      const mesh_face_t &face = world.faces[face_index];
      const face_surface_t &surface = face.surface;
      const Plane &plane = planes[face_index];
      const uint32_t base = (uint32_t)out.vertices.size();

      const bool gridded = face_is_subdivided(surface) && face.vertex_indices.size() == 4;
      if (face_is_subdivided(surface) && !gridded)
        log_error("generate_mesh_geometry_mesh: face {} with {} vertices carries a "
                  "subdivision - only a quad can, so it draws flat",
                  face_index, face.vertex_indices.size());

      if (gridded)
      {
        grid_vertices = compute_mesh_face_grid_world_vertices(mesh, face_index);
        emit_face_grid(out, grid_vertices, plane.normal, surface.uv, true);
      }
      else
      {
        for (uint32_t vertex : face.vertex_indices)
        {
          const linalg::vec3 &position = world.vertices[vertex];
          out.vertices.push_back({position, corner_normal(face_index, vertex),
                                  face_uv_at(surface.uv, position, plane.normal)});
        }
        for (uint32_t offset = 1; offset + 1 < (uint32_t)face.vertex_indices.size(); ++offset)
        {
          out.indices.push_back(base);
          out.indices.push_back(base + offset);
          out.indices.push_back(base + offset + 1);
        }
      }

      if (mesh_is_lightmapped)
      {
        out.lightmap.resize(out.vertices.size());
        if (const lightmap_chart_t *chart =
                find_chart(*lighting.lightmap, lighting.object_uid, face_index))
        {
          for (size_t vertex = base; vertex < out.vertices.size(); ++vertex)
          {
            out.lightmap[vertex].uv =
                lightmap_uv_for(*chart, lighting.lightmap->settings, lighting.lightmap->atlas,
                                out.vertices[vertex].position);
            out.lightmap[vertex].light_slots = chart->light_slots;
          }
        }
      }

      if (mesh_blends)
      {
        out.blend.resize(out.vertices.size());
        if (slot_layers[slot].blends && gridded)
          for (size_t vertex = 0; base + vertex < out.blend.size(); ++vertex)
            out.blend[base + vertex] = grid_vertex_blend(surface, vertex);
      }
    }

    const auto path_for = [&](uint16_t index) -> std::string {
      if (index < materials.count)
        return materials[index];
      log_error("generate_mesh_geometry_mesh: a face names material {}, but the map "
                "declares {} - drawing it untextured",
                index, materials.count);
      return {};
    };

    assets::material_t material{};
    material.diffuse_color = mesh.surface.color;
    material.name = std::to_string(slot_layers[slot].layer.data[0]);
    material.texture_path = path_for(slot_layers[slot].layer.data[0]);
    material.maps = resolve_material_maps(material.texture_path);
    if (slot_layers[slot].blends)
    {
      for (int layer = 1; layer < BLEND_LAYER_COUNT; ++layer)
      {
        material.blend_texture_path.data[layer - 1] =
            path_for(slot_layers[slot].layer.data[layer]);
        material.blend_maps.data[layer - 1] =
            resolve_material_maps(material.blend_texture_path.data[layer - 1]);
      }
    }

    assets::submesh_t submesh;
    submesh.index_offset = index_start;
    submesh.index_count = (uint32_t)out.indices.size() - index_start;
    submesh.material_index = (uint32_t)slot;
    out.materials.push_back(std::move(material));
    out.submeshes.push_back(submesh);
  }
  return out;
}

namespace
{

bool mesh_sources_equal(const mesh_source_t &lhs, const mesh_source_t &rhs)
{
  if (lhs.index() != rhs.index())
    return false;
  if (const recipe_mesh_source_t *recipe = std::get_if<recipe_mesh_source_t>(&lhs))
    return recipe->recipe == std::get<recipe_mesh_source_t>(rhs).recipe;
  if (const asset_mesh_source_t *asset = std::get_if<asset_mesh_source_t>(&lhs))
  {
    const asset_mesh_source_t &other = std::get<asset_mesh_source_t>(rhs);
    return asset->path == other.path && asset->scale == other.scale;
  }
  return true;
}

} // namespace

bool mesh_geometries_equal(const mesh_geometry_t &lhs, const mesh_geometry_t &rhs)
{
  if (!(lhs.position == rhs.position) || lhs.orientation.x != rhs.orientation.x ||
      lhs.orientation.y != rhs.orientation.y || lhs.orientation.z != rhs.orientation.z ||
      lhs.orientation.w != rhs.orientation.w)
    return false;
  if (lhs.vertices != rhs.vertices || lhs.faces.size() != rhs.faces.size())
    return false;
  for (size_t i = 0; i < lhs.faces.size(); ++i)
    if (lhs.faces[i].vertex_indices != rhs.faces[i].vertex_indices ||
        !face_surfaces_identical(lhs.faces[i].surface, rhs.faces[i].surface))
      return false;
  return mesh_sources_equal(lhs.source, rhs.source) && surfaces_equal(lhs.surface, rhs.surface) &&
         lhs.collides == rhs.collides && lhs.casts_shadows == rhs.casts_shadows &&
         lhs.owner_uid == rhs.owner_uid;
}

namespace
{

std::string format_vertex_list(Span<const linalg::vec3> vertices)
{
  std::string text;
  text.reserve(vertices.count * 32);
  char buffer[96];
  for (uint32_t i = 0; i < vertices.count; ++i)
  {
    std::snprintf(buffer, sizeof(buffer), "%.9g %.9g %.9g", vertices[i].x, vertices[i].y,
                  vertices[i].z);
    if (i != 0)
      text += ", ";
    text += buffer;
  }
  return text;
}

// x y z, x y z, ... in the order written, which is the index order.
std::optional<std::vector<linalg::vec3>> try_parse_vertex_list(const std::string &text)
{
  std::vector<linalg::vec3> vertices;
  const char *cursor = text.data();
  const char *end = text.data() + text.size();
  while (cursor < end)
  {
    linalg::vec3 vertex;
    for (float *component : {&vertex.x, &vertex.y, &vertex.z})
    {
      while (cursor < end && (*cursor == ' ' || *cursor == ','))
        ++cursor;
      const std::from_chars_result parsed = std::from_chars(cursor, end, *component);
      if (parsed.ec != std::errc())
        return std::nullopt;
      cursor = parsed.ptr;
    }
    vertices.push_back(vertex);
    while (cursor < end && *cursor == ' ')
      ++cursor;
  }
  return vertices;
}

std::string format_index_list(Span<const uint32_t> indices)
{
  std::string text;
  for (uint32_t i = 0; i < indices.count; ++i)
  {
    if (i != 0)
      text += ' ';
    text += std::to_string(indices[i]);
  }
  return text;
}

std::optional<std::vector<uint32_t>> try_parse_index_list(const std::string &text)
{
  std::vector<uint32_t> indices;
  const char *cursor = text.data();
  const char *end = text.data() + text.size();
  while (cursor < end)
  {
    while (cursor < end && *cursor == ' ')
      ++cursor;
    if (cursor == end)
      break;
    uint32_t index;
    const std::from_chars_result parsed = std::from_chars(cursor, end, index);
    if (parsed.ec != std::errc())
      return std::nullopt;
    cursor = parsed.ptr;
    indices.push_back(index);
  }
  return indices;
}

} // namespace

void write_mesh_row(const mesh_geometry_t &mesh, Span<const uint16_t> material_remap,
                    std::vector<std::pair<std::string, std::string>> &out_properties,
                    std::vector<map_block_out_t> &out_children)
{
  out_properties.emplace_back("position", format_vec3_exact(mesh.position));
  out_properties.emplace_back("rotation", format_quat_exact(mesh.orientation));

  const asset_mesh_source_t *asset = std::get_if<asset_mesh_source_t>(&mesh.source);
  const recipe_mesh_source_t *recipe = std::get_if<recipe_mesh_source_t>(&mesh.source);
  if (asset)
  {
    out_properties.emplace_back("asset", asset->path);
    out_properties.emplace_back("scale", format_vec3_exact(asset->scale));
  }
  else if (recipe)
    out_children.push_back(write_primitive_recipe_block(recipe->recipe));
  else
    out_properties.emplace_back("vertices", format_vertex_list(mesh.vertices));

  write_surface(mesh.surface, out_properties);
  if (!mesh.collides)
    out_properties.emplace_back("collides", format_bool(mesh.collides));
  if (!mesh.casts_shadows)
    out_properties.emplace_back("casts_shadows", format_bool(mesh.casts_shadows));
  write_uid_property("owner", mesh.owner_uid, out_properties);

  if (asset)
    return;
  for (const mesh_face_t &face : mesh.faces)
  {
    map_block_out_t &block = out_children.emplace_back();
    block.keyword = "face";
    if (!recipe)
      block.properties.emplace_back("indices", format_index_list(face.vertex_indices));
    write_face_surface_fields(face.surface, material_remap, block);
  }
}

std::optional<mesh_geometry_t>
try_parse_mesh_row(const std::map<std::string, std::string> &properties,
                   Span<const map_block_t> children)
{
  mesh_geometry_t mesh;
  read_vec3(properties, "position", mesh.position);
  read_rotation(properties, mesh.orientation);
  read_surface(properties, mesh.surface);
  read_bool(properties, "collides", mesh.collides);
  read_bool(properties, "casts_shadows", mesh.casts_shadows);
  read_uid_property(properties, "owner", mesh.owner_uid);

  if (const std::string *asset_path = find_property(properties, "asset"))
  {
    asset_mesh_source_t asset;
    asset.path = *asset_path;
    read_vec3(properties, "scale", asset.scale);
    mesh.source = std::move(asset);
    return mesh;
  }

  const map_block_t *primitive = nullptr;
  for (const map_block_t &child : children)
    if (child.keyword == "primitive")
      primitive = &child;
  if (primitive)
  {
    const std::optional<primitive_recipe_t> recipe =
        parse_primitive_recipe_block(*primitive, mesh.owner_uid);
    if (!recipe)
    {
      log_error("mesh: its primitive block does not parse - skipping the object");
      return std::nullopt;
    }
    const mesh_geometry_t built = make_primitive_mesh(*recipe);
    mesh.vertices = built.vertices;
    mesh.faces = built.faces;
    mesh.source = built.source;
    size_t face_index = 0;
    for (const map_block_t &child : children)
    {
      if (child.keyword == "primitive")
        continue;
      if (child.keyword != "face")
      {
        log_error("mesh: unknown sub-block \"{}\" - skipped", child.keyword);
        continue;
      }
      if (face_index >= mesh.faces.size())
      {
        log_error("mesh: more face blocks than the {} faces its {} builds - skipping the object",
                  mesh.faces.size(), to_string(recipe->kind));
        return std::nullopt;
      }
      mesh.faces[face_index].surface = read_face_surface(child);
      ++face_index;
    }
    return mesh;
  }

  const std::string *raw_vertices = find_property(properties, "vertices");
  if (!raw_vertices)
  {
    log_error("mesh: no \"vertices\", no \"asset\" and no primitive - skipping the object");
    return std::nullopt;
  }
  std::optional<std::vector<linalg::vec3>> vertices = try_parse_vertex_list(*raw_vertices);
  if (!vertices)
  {
    log_error("mesh \"vertices\": \"{}\" is not x/y/z triples - skipping the object",
              *raw_vertices);
    return std::nullopt;
  }
  mesh.vertices = std::move(*vertices);

  for (const map_block_t &child : children)
  {
    if (child.keyword != "face")
    {
      log_error("mesh: unknown sub-block \"{}\" - skipped", child.keyword);
      continue;
    }
    const std::string *raw_indices = find_property(child.properties, "indices");
    std::optional<std::vector<uint32_t>> indices =
        raw_indices ? try_parse_index_list(*raw_indices) : std::nullopt;
    if (!indices)
    {
      log_error("mesh face {}: no readable \"indices\" - skipping the object",
                mesh.faces.size());
      return std::nullopt;
    }
    mesh.faces.push_back({std::move(*indices), read_face_surface(child)});
  }

  if (std::optional<mesh_defect_t> defect = find_mesh_defect(mesh))
  {
    log_error("mesh: {} - skipping the object", format_mesh_defect(*defect));
    return std::nullopt;
  }
  return mesh;
}

} // namespace shared
