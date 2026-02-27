#pragma once
#include <vector>
#include <cstdint>
#include "linalg.hpp"

// A single vertex in the navmesh polygon soup.
struct nav_vertex_t
{
  linalg::vec3f pos;
};

// A convex polygon in the navmesh (may have 3 or more vertices after simplification).
// Vertices are wound CCW when viewed from above (+Y).
// Edge i runs from verts[i] to verts[(i+1) % N].
// neighbors[i] is the index of the polygon sharing edge i, or -1 (boundary / no connection).
struct nav_polygon_t
{
  std::vector<int32_t> verts;
  std::vector<int32_t> neighbors;
  int32_t island; // connected-component ID
};

struct navmesh_t
{
  std::vector<nav_vertex_t>  vertices;
  std::vector<nav_polygon_t> polygons;

  bool valid() const { return !polygons.empty(); }

  // Find the index of the polygon whose XZ projection contains (px, pz).
  // Returns -1 if no polygon covers that point.
  // Assumes all polygons are convex and wound CCW from above.
  int find_polygon(float px, float pz) const
  {
    for (int i = 0; i < (int)polygons.size(); ++i)
    {
      const auto &p = polygons[i];
      const int N = (int)p.verts.size();
      bool inside = true;
      for (int e = 0; e < N; ++e)
      {
        const linalg::vec3f &a = vertices[p.verts[e          ]].pos;
        const linalg::vec3f &b = vertices[p.verts[(e + 1) % N]].pos;
        // For CCW winding, point must be to the left of every edge.
        float cross = (b.x - a.x) * (pz - a.z) - (b.z - a.z) * (px - a.x);
        if (cross < 0.f) { inside = false; break; }
      }
      if (inside)
        return i;
    }
    return -1;
  }
};
