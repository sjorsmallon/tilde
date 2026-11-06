#include "map_geometry_asset.hpp"
#include "geometry/convex_decomposition.hpp"
#include "geometry/convex_hull.hpp"
#include "geometry/directional_bounds.hpp"
#include "mesh/mesh.hpp"
#include "mesh/mesh_decomposition.hpp"

#include "../../third_party/xatlas.h"

#include "log.hpp"
#include "parse_number.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>

namespace shared
{

const mesh_geometry_t *find_asset_sourced_mesh(const mesh_geometry_t &geometry)
{
  if (!std::holds_alternative<asset_mesh_source_t>(geometry.source))
    return nullptr;
  return &geometry;
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

const char* to_string(asset_collision_shape_t shape)
{
  switch (shape)
  {
    case asset_collision_shape_t::Dop_26: return "dop_26";
    case asset_collision_shape_t::Decompose_Mesh_Into_Convex_Pieces: return "decompose_mesh_into_convex_pieces";
    case asset_collision_shape_t::Hulls: return "hull";
  }
  fatal_error("to_string: asset_collision_shape_t {} is no member", (int)shape);
}

std::optional<asset_collision_file_t> try_parse_collision_file(std::string_view text, std::string_view file_name)
{
  const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  std::vector<std::string_view> tokens;
  for (size_t cursor = 0; cursor < text.size();)
  {
    if (is_space(text[cursor]))
    {
      ++cursor;
      continue;
    }
    size_t end = cursor;
    while (end < text.size() && !is_space(text[end]))
      ++end;
    tokens.push_back(text.substr(cursor, end - cursor));
    cursor = end;
  }

  if (tokens.size() == 1)
    for (const asset_collision_shape_t member :
         {asset_collision_shape_t::Dop_26, asset_collision_shape_t::Decompose_Mesh_Into_Convex_Pieces})
      if (tokens[0] == to_string(member))
        return asset_collision_file_t{.shape = member};
  if (tokens.empty() || tokens[0] != to_string(asset_collision_shape_t::Hulls))
  {
    log_error("'{}' says neither dop_26, decompose_mesh_into_convex_pieces nor a list of hull blocks", file_name);
    return std::nullopt;
  }

  asset_collision_file_t file{.shape = asset_collision_shape_t::Hulls};
  std::vector<float>     numbers;
  for (size_t index = 1; index <= tokens.size(); ++index)
  {
    if (index < tokens.size() && tokens[index] != to_string(asset_collision_shape_t::Hulls))
    {
      float                        value  = 0.f;
      const std::string_view       token  = tokens[index];
      const std::from_chars_result result = try_parse_number(token.data(), token.data() + token.size(), value);
      if (result.ec != std::errc{} || result.ptr != token.data() + token.size())
      {
        log_error("'{}': hull block {} holds '{}', which is not a number", file_name, file.hulls.size() + 1, token);
        return std::nullopt;
      }
      numbers.push_back(value);
      continue;
    }

    const size_t block = file.hulls.size() + 1;
    if (numbers.size() % 3 != 0)
    {
      log_error("'{}': hull block {} holds {} numbers, which is not a whole number of points", file_name, block,
                numbers.size());
      return std::nullopt;
    }
    const size_t point_count = numbers.size() / 3;
    if (point_count < 4 || point_count > MAX_SIDECAR_HULL_POINTS)
    {
      log_error("'{}': hull block {} has {} points; a hull takes four to {}", file_name, block, point_count,
                MAX_SIDECAR_HULL_POINTS);
      return std::nullopt;
    }
    std::vector<linalg::vec3>& points = file.hulls.emplace_back();
    for (size_t first = 0; first < numbers.size(); first += 3)
      points.push_back({numbers[first], numbers[first + 1], numbers[first + 2]});
    numbers.clear();
  }
  return file;
}

std::string collision_sidecar_path_for_mesh_asset(std::string_view mesh_path)
{
  const size_t slash         = mesh_path.find_last_of('/');
  const size_t dot           = mesh_path.find_last_of('.');
  const bool   has_extension = dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash);
  return std::string(has_extension ? mesh_path.substr(0, dot) : mesh_path) + ".collision";
}

asset_collision_file_t read_asset_collision_file(const asset_mesh_source_t& source)
{
  if (source.path.empty())
    return {};
  const std::string sidecar = collision_sidecar_path_for_mesh_asset(source.path);
  if (!assets::asset_exists(sidecar.c_str()))
    return {};
  const Span<const uint8_t> bytes = assets::read_asset_bytes(sidecar.c_str());
  std::optional<asset_collision_file_t> parsed =
      try_parse_collision_file(std::string_view(reinterpret_cast<const char*>(bytes.data), bytes.count), sidecar);
  if (!parsed)
  {
    log_error("{} collides as its dop_26", source.path);
    return {};
  }
  return std::move(*parsed);
}

std::optional<std::vector<collision_piece_t>>
try_build_collision_pieces_from_hulls(Span<const std::vector<linalg::vec3>> hulls, const linalg::vec3& scale,
                                      const linalg::vec3& position, const linalg::quatf& orientation,
                                      std::string_view file_name)
{
  std::vector<collision_piece_t> pieces;
  pieces.reserve(hulls.count);
  std::vector<linalg::vec3> placed;
  for (size_t block = 0; block < hulls.count; ++block)
  {
    placed.clear();
    for (const linalg::vec3& point : hulls[block])
      placed.push_back(position + linalg::rotate(orientation, {point.x * scale.x, point.y * scale.y, point.z * scale.z}));
    const std::optional<convex_polyhedron_t> body = try_build_convex_hull(placed);
    if (!body)
    {
      log_error("'{}': hull block {} is flat under the scale ({} {} {}); it closes nothing", file_name, block + 1,
                scale.x, scale.y, scale.z);
      return std::nullopt;
    }
    pieces.push_back({.bounds        = compute_point_bounds(body->vertices),
                      .planes        = compute_polyhedron_planes(*body),
                      .face_polygons = compute_polyhedron_face_polygons(*body)});
  }
  return pieces;
}

namespace
{

struct welded_position_key_t
{
  int64_t x, y, z;
  bool    operator==(const welded_position_key_t&) const = default;
};

struct welded_position_hash_t
{
  size_t operator()(const welded_position_key_t& key) const
  {
    return std::hash<int64_t>{}(key.x) ^ (std::hash<int64_t>{}(key.y) * 31) ^ (std::hash<int64_t>{}(key.z) * 961);
  }
};

// Positions within this of each other are one vertex: an exporter's duplicated corners agree to far
// finer, and a real edge is never this short.
constexpr float WELD_CELL = 1e-3f;

} // namespace

mesh_geometry_t make_mesh_from_asset_triangles(const assets::mesh_asset_t& asset, const linalg::vec3& scale)
{
  const bool mirrored = scale.x * scale.y * scale.z < 0.f;

  mesh_geometry_t mesh;
  std::unordered_map<welded_position_key_t, uint32_t, welded_position_hash_t> welded;
  std::vector<uint32_t> welded_index(asset.vertices.size(), 0);
  for (size_t index = 0; index < asset.vertices.size(); ++index)
  {
    const linalg::vec3 scaled = {asset.vertices[index].position.x * scale.x,
                                 asset.vertices[index].position.y * scale.y,
                                 asset.vertices[index].position.z * scale.z};
    const welded_position_key_t key = {(int64_t)std::llround(scaled.x / WELD_CELL),
                                       (int64_t)std::llround(scaled.y / WELD_CELL),
                                       (int64_t)std::llround(scaled.z / WELD_CELL)};
    const auto found = welded.find(key);
    if (found != welded.end())
    {
      welded_index[index] = found->second;
      continue;
    }
    welded_index[index] = (uint32_t)mesh.vertices.size();
    welded.emplace(key, welded_index[index]);
    mesh.vertices.push_back(scaled);
  }

  for (size_t first = 0; first + 2 < asset.indices.size(); first += 3)
  {
    mesh_face_t face;
    face.vertex_indices = {welded_index[asset.indices[first]], welded_index[asset.indices[first + 1]],
                           welded_index[asset.indices[first + 2]]};
    if (mirrored)
      std::swap(face.vertex_indices[1], face.vertex_indices[2]);
    if (face.vertex_indices[0] == face.vertex_indices[1] || face.vertex_indices[1] == face.vertex_indices[2] ||
        face.vertex_indices[0] == face.vertex_indices[2])
      continue;
    mesh.faces.push_back(std::move(face));
  }
  return mesh;
}

std::optional<std::vector<collision_piece_t>>
try_decompose_asset_mesh_into_collision_pieces(const assets::mesh_asset_t& asset, const linalg::vec3& scale,
                                               const linalg::vec3& position, const linalg::quatf& orientation)
{
  mesh_geometry_t closed = make_mesh_from_asset_triangles(asset, scale);
  if (closed.faces.size() < 4 || find_mesh_defect(closed))
    return std::nullopt;
  closed.position    = position;
  closed.orientation = orientation;
  const std::optional<std::vector<mesh_geometry_t>> convex = try_decompose_mesh_into_convex_pieces(closed);
  if (!convex)
    return std::nullopt;

  // A piece's faces are still triangles, many to a plane; the piece rebuilt from its distinct planes has one
  // face per plane, which is what the sweep, the silhouette walk and the solid shadow's plane count want.
  std::vector<collision_piece_t> pieces;
  pieces.reserve(convex->size());
  for (const mesh_geometry_t& piece_mesh : *convex)
  {
    const collision_piece_t triangulated = collision_piece_from_convex_mesh(piece_mesh);
    std::vector<Plane>      distinct;
    for (const Plane& plane : triangulated.planes)
    {
      bool seen = false;
      for (const Plane& kept : distinct)
        if (planes_coincide(kept, plane))
        {
          seen = true;
          break;
        }
      if (!seen)
        distinct.push_back(plane);
    }
    const linalg::vec3 center = (triangulated.bounds.min + triangulated.bounds.max) * 0.5f;
    const float        radius = linalg::length(triangulated.bounds.max - triangulated.bounds.min) * 0.5f + 1.f;
    const std::optional<convex_polyhedron_t> body = try_build_convex_from_planes(distinct, center, radius);
    if (!body)
      return std::nullopt;
    pieces.push_back({.bounds        = compute_point_bounds(body->vertices),
                      .planes        = compute_polyhedron_planes(*body),
                      .face_polygons = compute_polyhedron_face_polygons(*body)});
  }
  return pieces;
}

bool collision_pieces_coincide(const collision_piece_t& a, const collision_piece_t& b)
{
  if (a.planes.size() != b.planes.size())
    return false;
  for (const Plane& plane : a.planes)
  {
    bool found = false;
    for (const Plane& other : b.planes)
      if (planes_coincide(plane, other))
      {
        found = true;
        break;
      }
    if (!found)
      return false;
  }
  return true;
}

std::vector<collision_piece_t> compute_asset_mesh_collision_pieces(const mesh_geometry_t &mesh)
{
  const asset_mesh_source_t&   source = get_mesh_asset_source(mesh);
  const asset_collision_file_t file   = read_asset_collision_file(source);
  switch (file.shape)
  {
    case asset_collision_shape_t::Dop_26: return {compute_asset_mesh_directional_bound_piece(mesh)};
    case asset_collision_shape_t::Hulls:
    {
      std::optional<std::vector<collision_piece_t>> pieces = try_build_collision_pieces_from_hulls(
          file.hulls, source.scale, mesh.position, mesh.orientation, collision_sidecar_path_for_mesh_asset(source.path));
      if (!pieces)
      {
        log_error("{} collides as its dop_26", source.path);
        return {compute_asset_mesh_directional_bound_piece(mesh)};
      }
      return std::move(*pieces);
    }
    case asset_collision_shape_t::Decompose_Mesh_Into_Convex_Pieces: break;
  }
  const assets::asset_handle_t<assets::mesh_asset_t> handle = resolve_mesh_asset(source);
  const assets::mesh_asset_t* asset = handle.valid() ? assets::get(handle) : nullptr;
  if (asset == nullptr)
    return {compute_asset_mesh_directional_bound_piece(mesh)};
  std::optional<std::vector<collision_piece_t>> pieces =
      try_decompose_asset_mesh_into_collision_pieces(*asset, source.scale, mesh.position, mesh.orientation);
  if (!pieces)
  {
    const std::optional<mesh_defect_t> defect = find_mesh_defect(make_mesh_from_asset_triangles(*asset, source.scale));
    log_error("'{}' asks to be decomposed into convex pieces but {}; it collides as its dop_26", source.path,
              defect ? "its triangles are not a solid: " + format_mesh_defect(*defect)
                     : std::format("the decomposer refused it (past {} pieces or {} splits)", MAX_MESH_CONVEX_PIECES,
                                   MAX_MESH_DECOMPOSITION_SPLITS));
    return {compute_asset_mesh_directional_bound_piece(mesh)};
  }
  return std::move(*pieces);
}

collision_piece_t compute_asset_mesh_directional_bound_piece(const mesh_geometry_t &mesh)
{
  const asset_mesh_source_t &source = get_mesh_asset_source(mesh);
  const assets::asset_handle_t<assets::mesh_asset_t> handle = resolve_mesh_asset(source);
  const assets::mesh_asset_t *asset = handle.valid() ? assets::get(handle) : nullptr;
  if (asset == nullptr || asset->vertices.empty())
    return piece_from_oriented_box(compute_asset_mesh_box(mesh));

  std::vector<linalg::vec3> points;
  points.reserve(asset->vertices.size());
  for (const vertex_xnu &vertex : asset->vertices)
    points.push_back({vertex.position.x * source.scale.x, vertex.position.y * source.scale.y,
                      vertex.position.z * source.scale.z});

  const Array<Plane, DIRECTIONAL_BOUND_PLANES> local = compute_directional_bound_planes(points);
  std::vector<Plane>                           planes;
  planes.reserve(DIRECTIONAL_BOUND_PLANES);
  for (uint32_t index = 0; index < local.size(); ++index)
    planes.push_back({.point  = mesh.position + linalg::rotate(mesh.orientation, local[index].point),
                      .normal = linalg::rotate(mesh.orientation, local[index].normal)});

  const oriented_box_t                     box  = compute_asset_mesh_box(mesh);
  const std::optional<convex_polyhedron_t> body =
      try_build_convex_from_planes(planes, box.center, linalg::length(box.half_extents) + 1.f);
  if (!body)
    fatal_error("compute_asset_mesh_directional_bound_piece: the bound of the asset mesh at ({} {} {}) closes nothing",
                mesh.position.x, mesh.position.y, mesh.position.z);
  return {.bounds        = compute_point_bounds(body->vertices),
          .planes        = compute_polyhedron_planes(*body),
          .face_polygons = compute_polyhedron_face_polygons(*body)};
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
