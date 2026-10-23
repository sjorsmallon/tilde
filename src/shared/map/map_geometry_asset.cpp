#include "map_geometry_asset.hpp"

#include "../../third_party/xatlas.h"

#include "log.hpp"
#include <algorithm>
#include <cmath>

namespace shared
{

const mesh_geometry_t *find_asset_sourced_mesh(const geometry_value_t &geometry)
{
  const mesh_geometry_t *mesh = std::get_if<mesh_geometry_t>(&geometry);
  if (mesh == nullptr || !std::holds_alternative<asset_mesh_source_t>(mesh->source))
    return nullptr;
  return mesh;
}

const asset_mesh_source_t &get_mesh_asset_source(const mesh_geometry_t &mesh)
{
  const asset_mesh_source_t *source = std::get_if<asset_mesh_source_t>(&mesh.source);
  if (!source)
    fatal_error("get_mesh_asset_source: the mesh is not sourced from an asset");
  return *source;
}

assets::asset_handle_t<assets::mesh_asset_t> resolve_mesh_asset(const asset_mesh_source_t &source)
{
  if (source.path.empty())
    return {};

  if (!assets::asset_exists(source.path.c_str()))
  {
    log_error("a mesh names the asset '{}', which is not there", source.path);
    return {};
  }

  return assets::load_mesh(source.path.c_str());
}

oriented_box_t compute_asset_mesh_box(const mesh_geometry_t &mesh)
{
  constexpr float fallback_half_extent = 32.f;

  const asset_mesh_source_t &source = get_mesh_asset_source(mesh);
  linalg::vec3 center_offset{0.f, 0.f, 0.f};
  linalg::vec3 half_extents{fallback_half_extent, fallback_half_extent, fallback_half_extent};

  const assets::asset_handle_t<assets::mesh_asset_t> handle = resolve_mesh_asset(source);
  const assets::mesh_asset_t *asset = handle.valid() ? assets::get(handle) : nullptr;
  if (asset && !asset->vertices.empty())
  {
    const aabb_bounds_t bounds = assets::compute_mesh_bounds(asset);
    const linalg::vec3 center = (bounds.min + bounds.max) * 0.5f;
    const linalg::vec3 half = (bounds.max - bounds.min) * 0.5f;
    const linalg::vec3 &scale = source.scale;
    center_offset = {center.x * scale.x, center.y * scale.y, center.z * scale.z};
    half_extents = {std::abs(half.x * scale.x), std::abs(half.y * scale.y),
                    std::abs(half.z * scale.z)};
  }

  return {.center = mesh.position + linalg::rotate(mesh.orientation, center_offset),
          .half_extents = half_extents,
          .orientation = mesh.orientation};
}

collision_piece_t compute_asset_mesh_collision_box(const mesh_geometry_t &mesh)
{
  return piece_from_oriented_box(compute_asset_mesh_box(mesh));
}

linalg::mat4f compute_asset_mesh_transform(const mesh_geometry_t &mesh)
{
  return linalg::compose_transform(mesh.position, mesh.orientation,
                                   get_mesh_asset_source(mesh).scale);
}

namespace
{

linalg::vec3 transform_point(const linalg::mat4f &transform, const linalg::vec3 &point)
{
  const linalg::vec4 moved = transform * linalg::vec4{point.x, point.y, point.z, 1.f};
  return {moved.x, moved.y, moved.z};
}

// A normal transforms by the inverse transpose: R * S^-1 for a rotation and a
// diagonal scale, which is what keeps it perpendicular under a non-uniform one.
linalg::vec3 compute_asset_mesh_world_normal(const mesh_geometry_t &mesh,
                                             const linalg::vec3 &mesh_normal)
{
  const linalg::vec3 &scale = get_mesh_asset_source(mesh).scale;
  const linalg::vec3 unscaled{scale.x != 0.f ? mesh_normal.x / scale.x : 0.f,
                              scale.y != 0.f ? mesh_normal.y / scale.y : 0.f,
                              scale.z != 0.f ? mesh_normal.z / scale.z : 0.f};
  const linalg::vec3 rotated = linalg::rotate(mesh.orientation, unscaled);
  const float length = linalg::length(rotated);
  return length > 0.f ? rotated * (1.f / length) : linalg::vec3{0.f, 0.f, 0.f};
}

} // namespace

std::vector<world_triangle_t> compute_asset_mesh_world_triangles(const mesh_geometry_t &mesh)
{
  std::vector<world_triangle_t> triangles;

  const assets::mesh_asset_t *asset = assets::get(resolve_mesh_asset(get_mesh_asset_source(mesh)));
  if (!asset || asset->indices.size() < 3)
    return triangles;

  const linalg::mat4f transform = compute_asset_mesh_transform(mesh);
  triangles.reserve(asset->indices.size() / 3);

  for (size_t first = 0; first + 2 < asset->indices.size(); first += 3)
  {
    world_triangle_t triangle;
    linalg::vec3 authored_normal{0.f, 0.f, 0.f};
    for (int corner = 0; corner < 3; ++corner)
    {
      const vertex_xnu &vertex = asset->vertices[asset->indices[first + (size_t)corner]];
      triangle.corners[corner] = transform_point(transform, vertex.position);
      authored_normal = authored_normal + compute_asset_mesh_world_normal(mesh, vertex.normal);
    }

    // The geometric normal is the plane; the authored normals say which side is
    // OUT, which the winding alone cannot once a negative scale has mirrored it.
    linalg::vec3 normal = linalg::cross(triangle.corners[1] - triangle.corners[0],
                                        triangle.corners[2] - triangle.corners[0]);
    const float length = linalg::length(normal);
    if (length > 1e-6f)
    {
      normal = normal * (1.f / length);
      if (linalg::dot(normal, authored_normal) < 0.f) normal = normal * -1.f;
      triangle.normal = normal;
    }

    triangles.push_back(triangle);
  }

  return triangles;
}

std::vector<vertex_xnu> compute_asset_mesh_world_vertices(const mesh_geometry_t &mesh)
{
  std::vector<vertex_xnu> vertices;

  const assets::mesh_asset_t *asset = assets::get(resolve_mesh_asset(get_mesh_asset_source(mesh)));
  if (!asset)
    return vertices;

  const linalg::mat4f transform = compute_asset_mesh_transform(mesh);
  vertices.reserve(asset->vertices.size());
  for (const vertex_xnu& vertex : asset->vertices)
    vertices.push_back({transform_point(transform, vertex.position),
                        compute_asset_mesh_world_normal(mesh, vertex.normal), vertex.uv});
  return vertices;
}

std::vector<chart_unwrap_t> unwrap_asset_mesh(const mesh_geometry_t &mesh,
                                              float texels_per_world_unit)
{
  std::vector<chart_unwrap_t> unwraps;

  const assets::mesh_asset_t *asset = assets::get(resolve_mesh_asset(get_mesh_asset_source(mesh)));
  const std::vector<vertex_xnu> vertices = compute_asset_mesh_world_vertices(mesh);
  if (!asset || vertices.empty() || asset->indices.size() < 3)
    return unwraps;
  if (!(texels_per_world_unit > 0.f))
  {
    log_error("[lightmap] unwrap asked at {} texels per unit; nothing to size a chart by.",
              texels_per_world_unit);
    return unwraps;
  }

  xatlas::Atlas* atlas = xatlas::Create();

  xatlas::MeshDecl declaration;
  declaration.vertexCount = (uint32_t)vertices.size();
  declaration.vertexPositionData = &vertices[0].position;
  declaration.vertexPositionStride = sizeof(vertex_xnu);
  declaration.vertexNormalData = &vertices[0].normal;
  declaration.vertexNormalStride = sizeof(vertex_xnu);
  declaration.indexData = asset->indices.data();
  declaration.indexCount = (uint32_t)asset->indices.size();
  declaration.indexFormat = xatlas::IndexFormat::UInt32;

  const xatlas::AddMeshError error = xatlas::AddMesh(atlas, declaration, 1);
  if (error != xatlas::AddMeshError::Success)
  {
    log_error("[lightmap] xatlas refused {}: {}.", get_mesh_asset_source(mesh).path,
              xatlas::StringForEnum(error));
    xatlas::Destroy(atlas);
    return unwraps;
  }

  // xatlas unwraps, we pack: its own packing runs only to get every chart's uvs
  // into one per-chart space at our density, and its atlas is thrown away.
  xatlas::PackOptions pack_options;
  pack_options.texelsPerUnit = texels_per_world_unit;
  pack_options.resolution = 0;
  pack_options.padding = 0;
  pack_options.bilinear = false;
  xatlas::Generate(atlas, xatlas::ChartOptions(), pack_options);

  const xatlas::Mesh& output = atlas->meshes[0];
  const float world_units_per_atlas_texel = 1.f / texels_per_world_unit;

  // Keyed by the SOURCE vertex a face corner names, not by xatlas's output
  // vertex: its xref goes through the colocal weld and can name another face's
  // copy of a shared corner, which carries that face's normal.
  std::vector<uint32_t> chart_vertex_index_by_source_vertex(vertices.size(), UINT32_MAX);
  std::vector<uint32_t> touched;
  unwraps.reserve(output.chartCount);

  for (uint32_t chart_index = 0; chart_index < output.chartCount; ++chart_index)
  {
    const xatlas::Chart& chart = output.chartArray[chart_index];
    if (chart.type == xatlas::ChartType::Invalid)
    {
      log_warning("[lightmap] xatlas could not parameterize {} face(s) of {}; they draw unlit.",
                  chart.faceCount, get_mesh_asset_source(mesh).path);
      continue;
    }

    chart_unwrap_t unwrap;
    unwrap.faces.reserve(chart.faceCount);
    unwrap.indices.reserve((size_t)chart.faceCount * 3);
    touched.clear();

    for (uint32_t f = 0; f < chart.faceCount; ++f)
    {
      const uint32_t face = chart.faceArray[f];
      unwrap.faces.push_back(face);
      for (uint32_t corner = 0; corner < 3; ++corner)
      {
        const uint32_t source_vertex = asset->indices[(size_t)face * 3 + corner];
        if (chart_vertex_index_by_source_vertex[source_vertex] == UINT32_MAX)
        {
          const xatlas::Vertex& vertex =
              output.vertexArray[output.indexArray[(size_t)face * 3 + corner]];
          chart_vertex_index_by_source_vertex[source_vertex] = (uint32_t)unwrap.vertices.size();
          touched.push_back(source_vertex);
          unwrap.vertices.push_back({source_vertex,
                                     {vertex.uv[0] * world_units_per_atlas_texel,
                                      vertex.uv[1] * world_units_per_atlas_texel}});
        }
        unwrap.indices.push_back(chart_vertex_index_by_source_vertex[source_vertex]);
      }
    }
    for (uint32_t source_vertex : touched) chart_vertex_index_by_source_vertex[source_vertex] = UINT32_MAX;

    linalg::vec2 minimum = unwrap.vertices[0].uv;
    for (const unwrapped_vertex_t& vertex : unwrap.vertices)
      minimum = {std::min(minimum.x, vertex.uv.x), std::min(minimum.y, vertex.uv.y)};
    for (unwrapped_vertex_t& vertex : unwrap.vertices)
      vertex.uv = {vertex.uv.x - minimum.x, vertex.uv.y - minimum.y};

    unwraps.push_back(std::move(unwrap));
  }

  xatlas::Destroy(atlas);
  return unwraps;
}

assets::mesh_asset_t generate_lightmapped_asset_mesh(const mesh_geometry_t &mesh,
                                                     const object_lightmap_ref_t &lighting)
{
  assets::mesh_asset_t out;

  const assets::mesh_asset_t *source = assets::get(resolve_mesh_asset(get_mesh_asset_source(mesh)));
  if (!source)
    return out;

  const std::vector<vertex_xnu> world = compute_asset_mesh_world_vertices(mesh);

  out.materials = source->materials;
  out.submeshes = source->submeshes;

  // One vertex per triangle CORNER and no lightmap array: byte for byte what a
  // mesh without a bake always uploaded.
  const auto copy_unlit = [&]() {
    out.vertices.clear();
    out.indices.clear();
    out.lightmap.clear();
    out.vertices.reserve(source->indices.size());
    out.indices.reserve(source->indices.size());
    for (uint32_t index : source->indices)
    {
      out.indices.push_back((uint32_t)out.vertices.size());
      out.vertices.push_back(world[index]);
    }
  };

  std::vector<const lightmap_chart_t*> charts;
  if (lighting.has_bake())
    for (const lightmap_chart_t& chart : lighting.lightmap->charts)
      if (chart.object_uid == lighting.object_uid && !chart.unwrap.empty())
        charts.push_back(&chart);

  if (charts.empty())
  {
    copy_unlit();
    return out;
  }

  // The unwrap's vertices, one per chart, with the index at each face's three
  // corners pointing into them -- so the index list keeps the source's face
  // order and every submesh range holds. A face no chart covers gets three
  // fresh unlit corners.
  const size_t face_count = source->indices.size() / 3;
  std::vector<uint8_t> covered(face_count, 0);
  out.indices.assign(face_count * 3, UINT32_MAX);

  const auto refuse = [&](const char* what, size_t value, size_t limit) {
    log_error("[lightmap] mesh {}'s unwrap names {} {} of {}; the bake does not "
              "match {} and it draws unlit. Rebake.",
              lighting.object_uid, what, value, limit, get_mesh_asset_source(mesh).path);
    copy_unlit();
  };

  for (const lightmap_chart_t* chart : charts)
  {
    const chart_unwrap_t& unwrap = chart->unwrap;
    const uint32_t base = (uint32_t)out.vertices.size();

    for (const unwrapped_vertex_t& vertex : unwrap.vertices)
    {
      if (vertex.xref >= world.size())
      {
        refuse("vertex", vertex.xref, world.size());
        return out;
      }
      out.vertices.push_back(world[vertex.xref]);
      out.lightmap.push_back(
          {lightmap_uv_from_chart_space(*chart, lighting.lightmap->settings,
                                        lighting.lightmap->atlas, vertex.uv),
           chart->light_slots});
    }

    for (size_t t = 0; t < unwrap.faces.size(); ++t)
    {
      const uint32_t face = unwrap.faces[t];
      if (face >= face_count)
      {
        refuse("face", face, face_count);
        return out;
      }
      if (covered[face])
      {
        refuse("face twice, face", face, face_count);
        return out;
      }
      covered[face] = 1;
      for (size_t corner = 0; corner < 3; ++corner)
      {
        const uint32_t local = unwrap.indices[t * 3 + corner];
        if (local >= unwrap.vertices.size())
        {
          refuse("chart vertex", local, unwrap.vertices.size());
          return out;
        }
        if (unwrap.vertices[local].xref != source->indices[(size_t)face * 3 + corner])
        {
          refuse("another mesh's corner, vertex", unwrap.vertices[local].xref, world.size());
          return out;
        }
        out.indices[(size_t)face * 3 + corner] = base + local;
      }
    }
  }

  for (size_t face = 0; face < face_count; ++face)
  {
    if (covered[face]) continue;
    for (size_t corner = 0; corner < 3; ++corner)
    {
      out.indices[face * 3 + corner] = (uint32_t)out.vertices.size();
      out.vertices.push_back(world[source->indices[face * 3 + corner]]);
      out.lightmap.push_back({});
    }
  }

  return out;
}

} // namespace shared
