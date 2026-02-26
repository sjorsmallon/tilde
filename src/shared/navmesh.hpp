#pragma once
#include <vector>
#include <cstdint>
#include "linalg.hpp"

// A single vertex in the navmesh polygon soup.
struct nav_vertex_t
{
  linalg::vec3f pos;
};

// A triangle polygon in the navmesh.
// Vertices are wound CCW when viewed from above (+Y).
// Edge i runs from verts[i] to verts[(i+1)%3].
// neighbors[i] is the index of the polygon sharing edge i, or -1 (boundary / no connection).
struct nav_polygon_t
{
  int32_t verts[3];
  int32_t neighbors[3];
  int32_t island; // connected-component ID
};

struct navmesh_t
{
  std::vector<nav_vertex_t>  vertices;
  std::vector<nav_polygon_t> polygons;

  bool valid() const { return !polygons.empty(); }

  // Find the index of the polygon whose XZ projection contains (px, pz).
  // Returns -1 if no polygon covers that point.
  int find_polygon(float px, float pz) const
  {
    for (int i = 0; i < (int)polygons.size(); ++i)
    {
      const auto &p = polygons[i];
      const linalg::vec3f &a = vertices[p.verts[0]].pos;
      const linalg::vec3f &b = vertices[p.verts[1]].pos;
      const linalg::vec3f &c = vertices[p.verts[2]].pos;

      // 2-D point-in-triangle via cross products (XZ plane)
      auto cross2d = [](float ax, float az, float bx, float bz) {
        return ax * bz - az * bx;
      };

      float d0 = cross2d(b.x - a.x, b.z - a.z, px - a.x, pz - a.z);
      float d1 = cross2d(c.x - b.x, c.z - b.z, px - b.x, pz - b.z);
      float d2 = cross2d(a.x - c.x, a.z - c.z, px - c.x, pz - c.z);

      bool has_neg = (d0 < 0) || (d1 < 0) || (d2 < 0);
      bool has_pos = (d0 > 0) || (d1 > 0) || (d2 > 0);
      if (!(has_neg && has_pos))
        return i;
    }
    return -1;
  }
};
