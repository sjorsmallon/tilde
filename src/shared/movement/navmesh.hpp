#pragma once

#include "linalg.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace shared
{

struct nav_vertex_t
{
  linalg::vec3f position;
};

// Convex, wound counter-clockwise seen from +Y. Edge i runs from vertices[i] to vertices[(i + 1) % N];
// neighbors[i] is the polygon sharing edge i, or -1 at a boundary.
struct nav_polygon_t
{
  std::vector<int32_t> vertices;
  std::vector<int32_t> neighbors;
  int32_t              island;
};

struct navmesh_t
{
  std::vector<nav_vertex_t>  vertices;
  std::vector<nav_polygon_t> polygons;

  bool valid() const { return !polygons.empty(); }

  // The polygon whose XZ projection contains the point.
  [[nodiscard]] std::optional<int32_t> try_find_polygon_index_containing(float x, float z) const
  {
    for (int32_t index = 0; index < static_cast<int32_t>(polygons.size()); ++index)
    {
      const nav_polygon_t& polygon = polygons[index];
      const int32_t        count   = static_cast<int32_t>(polygon.vertices.size());
      bool                 inside  = true;
      for (int32_t edge = 0; edge < count; ++edge)
      {
        const linalg::vec3f& a = vertices[polygon.vertices[edge]].position;
        const linalg::vec3f& b = vertices[polygon.vertices[(edge + 1) % count]].position;
        const float cross = (b.x - a.x) * (z - a.z) - (b.z - a.z) * (x - a.x);
        if (cross < 0.f)
        {
          inside = false;
          break;
        }
      }
      if (inside)
        return index;
    }
    return std::nullopt;
  }
};

// The corner points of a path over the navmesh from start to end; empty when either lies outside it.
[[nodiscard]] std::vector<linalg::vec3> find_path(const navmesh_t& navmesh, const linalg::vec3& start,
                                                  const linalg::vec3& end);

} // namespace shared
