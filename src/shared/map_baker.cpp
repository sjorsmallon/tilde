#define ENTITIES_WANT_INCLUDES
#include "map_baker.hpp"
#include "collision_detection.hpp"
#include "entities/static_entities.hpp"
#include "player_move.hpp"
#include <cfloat>
#include <cmath>
#include <print>
#include <queue>
#include <unordered_map>

namespace shared
{

static constexpr float NAVMESH_MIN_CELL_SIZE    = 1.f;
static constexpr float NAVMESH_WALKABLE_SLOPE   = 0.7f;  // cos(~45°)
static constexpr float PLAYER_FULL_HEIGHT       = 72.f;
static constexpr float NAVMESH_STEP_HEIGHT      = 16.f;  // max height diff for adjacent spans

// A single walkable surface sample found during voxelization.
struct nav_span_t
{
  int   ix, iz;      // grid column
  float floor_y;     // world-space floor height
  int   island = -1; // filled during flood-fill
};

void bake_map(map_t &map, float cell_size)
{
  cell_size = std::max(cell_size, NAVMESH_MIN_CELL_SIZE);

  // --- 1. Build BVH from static geometry ---

  std::vector<BVH_Input> bvh_inputs;
  for (const auto &entry : map.entities)
  {
    auto *ent = entry.entity.get();
    if (!ent)
      continue;
    if (!dynamic_cast<network::AABB_Entity *>(ent) &&
        !dynamic_cast<network::Wedge_Entity *>(ent) &&
        !dynamic_cast<network::Static_Mesh_Entity *>(ent))
      continue;

    auto bounds = compute_entity_bounds(ent);
    BVH_Input input;
    input.aabb.min         = bounds.min;
    input.aabb.max         = bounds.max;
    input.id               = {Collision_Id::Type::Static_Geometry,
                               (uint32_t)bvh_inputs.size()};
    input.collision_planes = compute_entity_collision_planes(ent);
    input.face_polygons    = compute_entity_face_polygons(ent);
    bvh_inputs.push_back(std::move(input));
  }

  if (bvh_inputs.empty())
  {
    std::println("[bake] No static geometry — skipping navmesh generation.");
    return;
  }

  auto bvh = build_bvh(bvh_inputs);

  // --- 2. Compute world bounds ---

  vec3f world_min = { FLT_MAX,  FLT_MAX,  FLT_MAX};
  vec3f world_max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
  for (const auto &input : bvh_inputs)
  {
    world_min.x = std::min(world_min.x, input.aabb.min.x);
    world_min.y = std::min(world_min.y, input.aabb.min.y);
    world_min.z = std::min(world_min.z, input.aabb.min.z);
    world_max.x = std::max(world_max.x, input.aabb.max.x);
    world_max.y = std::max(world_max.y, input.aabb.max.y);
    world_max.z = std::max(world_max.z, input.aabb.max.z);
  }

  // --- 3. Grid parameters ---

  const float origin_x = world_min.x + cell_size * 0.5f;
  const float origin_z = world_min.z + cell_size * 0.5f;
  const int   width    = (int)std::ceil((world_max.x - world_min.x) / cell_size);
  const int   depth    = (int)std::ceil((world_max.z - world_min.z) / cell_size);
  const float ray_top  = world_max.y + 1.f;

  // --- 4. Multi-level voxelization ---
  // For each column (ix, iz), iteratively cast rays downward to collect
  // ALL walkable spans (handles multiple floors at the same XZ position).

  // spans_grid[iz * width + ix] = list of span indices in that column
  std::vector<std::vector<int>> spans_grid(width * depth);
  std::vector<nav_span_t>       spans;

  const vec3f down = {0.f, -1.f, 0.f};
  const vec3f up   = {0.f,  1.f, 0.f};

  std::println("[bake] Voxelizing {}x{} grid...", width, depth);
  int last_percent = -1;

  // so for every xz cell, we cast a ray downwards from the top of the world.
  //  If it hits a surface, we check if it's walkable (slope + headroom). If so, we record a nav_span_t for that floor. 
  // Then we move the ray origin slightly below that hit and repeat until we exit the geometry.

  for (int iz = 0; iz < depth; ++iz)
  {
    int percent = (iz * 100) / depth;
    if (percent / 10 != last_percent / 10)
    {
      std::println("[bake]   {}%", percent);
      last_percent = percent;
    }

    for (int ix = 0; ix < width; ++ix)
    {
      const float cx = origin_x + ix * cell_size;
      const float cz = origin_z + iz * cell_size;

      vec3f ray_origin = {cx, ray_top, cz};

      while (true)
      {
        Ray_Hit floor_hit;
        if (!bvh_intersect_ray(bvh, ray_origin, down, floor_hit))
          break;

        if (floor_hit.id.type != Collision_Id::Type::Static_Geometry)
          break;

        const float floor_y = ray_origin.y - floor_hit.t;

        // Slope check: the hit primitive must have at least one upward-facing plane.
        const auto &planes = bvh.primitives[floor_hit.id.index].collision_planes;
        float best_up = -FLT_MAX;
        for (const auto &pl : planes)
          best_up = std::max(best_up, pl.normal.y);

        if (best_up >= NAVMESH_WALKABLE_SLOPE)
        {
          // Headroom check: must have enough clearance above the floor.
          vec3f ceil_origin = {cx, floor_y + 0.01f, cz};
          Ray_Hit ceil_hit;
          float clearance = FLT_MAX;
          if (bvh_intersect_ray(bvh, ceil_origin, up, ceil_hit))
            clearance = ceil_hit.t;

          if (clearance >= PLAYER_FULL_HEIGHT)
          {
            int idx = (int)spans.size();
            spans_grid[iz * width + ix].push_back(idx);
            spans.push_back({ix, iz, floor_y, -1});
          }
        }

        // Advance below this surface to look for deeper floors.
        ray_origin.y = floor_y - 0.01f;
      }
    }
  }

  const int num_spans = (int)spans.size();

  // --- 5. Build span adjacency and connected-component flood fill ---
  // Two spans are adjacent if their columns are 4-connected and
  // their floor heights differ by at most STEP_HEIGHT.

  std::vector<std::vector<int>> span_adj(num_spans);

  const int dx[] = { 1, -1,  0,  0};
  const int dz[] = { 0,  0,  1, -1};

  for (int s = 0; s < num_spans; ++s)
  {
    const nav_span_t &span = spans[s];
    for (int d = 0; d < 4; ++d)
    {
      int nx = span.ix + dx[d];
      int nz = span.iz + dz[d];
      if (nx < 0 || nx >= width || nz < 0 || nz >= depth)
        continue;

      for (int t : spans_grid[nz * width + nx])
      {
        if (std::abs(spans[t].floor_y - span.floor_y) <= NAVMESH_STEP_HEIGHT)
          span_adj[s].push_back(t);
      }
    }
  }

  // BFS flood fill to assign island IDs.
  int num_islands = 0;
  for (int s = 0; s < num_spans; ++s)
  {
    if (spans[s].island >= 0)
      continue;
    std::queue<int> q;
    q.push(s);
    spans[s].island = num_islands;
    while (!q.empty())
    {
      int cur = q.front(); q.pop();
      for (int nb : span_adj[cur])
      {
        if (spans[nb].island < 0)
        {
          spans[nb].island = num_islands;
          q.push(nb);
        }
      }
    }
    ++num_islands;
  }

  // --- 6. Triangle generation ---
  // Each span emits one flat quad (2 triangles) at its floor_y.
  // Vertices are NOT deduplicated across spans — each span has 4 private corners.
  //
  // Triangulation of span s (global index s):
  //   quad corners (CCW from above):
  //     V0 = (cx-half, y, cz-half)  SW
  //     V1 = (cx+half, y, cz-half)  SE
  //     V2 = (cx+half, y, cz+half)  NE
  //     V3 = (cx-half, y, cz+half)  NW
  //
  //   tri0 (poly 2s  ): verts [V0, V1, V2]
  //   tri1 (poly 2s+1): verts [V0, V2, V3]
  //
  //   Shared hypotenuse V0↔V2:
  //     tri0.neighbors[2] = 2s+1
  //     tri1.neighbors[0] = 2s
  //
  // Cross-span adjacency (spans s and t in direction d):
  //   South (dz=-1): s.tri0.neighbors[0] ↔ t.tri1.neighbors[1]
  //   East  (dx=+1): s.tri0.neighbors[1] ↔ t.tri1.neighbors[2]
  //   North (dz=+1): s.tri1.neighbors[1] ↔ t.tri0.neighbors[0]
  //   West  (dx=-1): s.tri1.neighbors[2] ↔ t.tri0.neighbors[1]

  navmesh_t &nav = map.navmesh;
  nav.vertices.clear();
  nav.polygons.clear();
  nav.vertices.reserve(num_spans * 4);
  nav.polygons.reserve(num_spans * 2);

  const float half = cell_size * 0.5f;

  for (int s = 0; s < num_spans; ++s)
  {
    const nav_span_t &sp = spans[s];
    const float cx = origin_x + sp.ix * cell_size;
    const float cz = origin_z + sp.iz * cell_size;
    const float y  = sp.floor_y;

    int v0 = (int)nav.vertices.size();
    nav.vertices.push_back(nav_vertex_t{{cx - half, y, cz - half}}); // SW
    nav.vertices.push_back(nav_vertex_t{{cx + half, y, cz - half}}); // SE
    nav.vertices.push_back(nav_vertex_t{{cx + half, y, cz + half}}); // NE
    nav.vertices.push_back(nav_vertex_t{{cx - half, y, cz + half}}); // NW

    // tri0: [V0, V1, V2], tri1: [V0, V2, V3]
    // Within-cell neighbors set immediately; cross-span filled in step 7.
    nav_polygon_t tri0, tri1;
    tri0.verts[0] = v0; tri0.verts[1] = v0+1; tri0.verts[2] = v0+2;
    tri0.neighbors[0] = -1; // south edge   → filled later
    tri0.neighbors[1] = -1; // east edge    → filled later
    tri0.neighbors[2] = 2*s+1; // hypotenuse → tri1
    tri0.island = sp.island;

    tri1.verts[0] = v0; tri1.verts[1] = v0+2; tri1.verts[2] = v0+3;
    tri1.neighbors[0] = 2*s;   // hypotenuse → tri0
    tri1.neighbors[1] = -1; // north edge   → filled later
    tri1.neighbors[2] = -1; // west edge    → filled later
    tri1.island = sp.island;

    nav.polygons.push_back(tri0);
    nav.polygons.push_back(tri1);
  }

  // --- 7. Fill cross-span adjacency ---
  // We need a lookup: column (ix,iz) → list of (span_index, floor_y)
  // which we already have in spans_grid.
  //
  // For each span s, find its 4 directional neighbors and wire edges.
  // Only connect spans on the same island.

  for (int s = 0; s < num_spans; ++s)
  {
    const nav_span_t &sp = spans[s];

    // Directions: 0=South(iz-1), 1=East(ix+1), 2=North(iz+1), 3=West(ix-1)
    const int ndx[] = { 0, +1,  0, -1};
    const int ndz[] = {-1,  0, +1,  0};

    for (int d = 0; d < 4; ++d)
    {
      int nx = sp.ix + ndx[d];
      int nz = sp.iz + ndz[d];
      if (nx < 0 || nx >= width || nz < 0 || nz >= depth)
        continue;

      // Find the adjacent span with smallest compatible height difference.
      int   best_t    = -1;
      float best_diff = FLT_MAX;
      for (int t : spans_grid[nz * width + nx])
      {
        float diff = std::abs(spans[t].floor_y - sp.floor_y);
        if (diff <= NAVMESH_STEP_HEIGHT && diff < best_diff &&
            spans[t].island == sp.island)
        {
          best_diff = diff;
          best_t    = t;
        }
      }

      if (best_t < 0)
        continue;

      int t = best_t;
      switch (d)
      {
        case 0: // South: s.tri0.neighbors[0] ↔ t.tri1.neighbors[1]
          nav.polygons[2*s  ].neighbors[0] = 2*t+1;
          nav.polygons[2*t+1].neighbors[1] = 2*s;
          break;
        case 1: // East:  s.tri0.neighbors[1] ↔ t.tri1.neighbors[2]
          nav.polygons[2*s  ].neighbors[1] = 2*t+1;
          nav.polygons[2*t+1].neighbors[2] = 2*s;
          break;
        case 2: // North: s.tri1.neighbors[1] ↔ t.tri0.neighbors[0]
          nav.polygons[2*s+1].neighbors[1] = 2*t;
          nav.polygons[2*t  ].neighbors[0] = 2*s+1;
          break;
        case 3: // West:  s.tri1.neighbors[2] ↔ t.tri0.neighbors[1]
          nav.polygons[2*s+1].neighbors[2] = 2*t;
          nav.polygons[2*t  ].neighbors[1] = 2*s+1;
          break;
      }
    }
  }

  std::println("[bake] Navmesh: {} spans, {} polygons, {} islands.",
               num_spans, (int)nav.polygons.size(), num_islands);
}

} // namespace shared
