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

// ---------------------------------------------------------------------------
// Navmesh invariant checker — call before and after simplify_navmesh().
// Aborts loudly if any invariant is violated.
// ---------------------------------------------------------------------------

static void assert_navmesh_invariants(const navmesh_t &nav, const char *label, bool pre_only = false)
{
  const auto &polys = nav.polygons;
  const auto &verts = nav.vertices;
  const int np = (int)polys.size();
  const int nv = (int)verts.size();

  // Vertex indices in range.
  for (int a = 0; a < np; ++a)
    for (int vi : polys[a].verts)
      assert(vi >= 0 && vi < nv && "[navmesh] vertex index out of range");

  // Neighbor indices valid, no self-neighbors.
  for (int a = 0; a < np; ++a)
    for (int nb : polys[a].neighbors)
    {
      assert((nb == -1 || (nb >= 0 && nb < np)) && "[navmesh] neighbor index out of range");
      assert(nb != a && "[navmesh] polygon is its own neighbor");
    }

  // NEIGHBOR SYMMETRY — the most critical invariant.
  for (int a = 0; a < np; ++a)
    for (int nb : polys[a].neighbors)
    {
      if (nb < 0) continue;
      bool found = false;
      for (int back : polys[nb].neighbors)
        if (back == a) { found = true; break; }
      if (!found)
      {
        std::println(stderr, "[navmesh] SYMMETRY BROKEN at '{}': poly {} points to poly {} but not vice-versa", label, a, nb);
        std::abort();
      }
    }

  // Convexity: all cross-products non-negative (CCW from above).
  for (int a = 0; a < np; ++a)
  {
    const auto &p = polys[a];
    const int N = (int)p.verts.size();
    for (int i = 0; i < N; ++i)
    {
      const auto &prev = verts[p.verts[(i - 1 + N) % N]].pos;
      const auto &cur  = verts[p.verts[i              ]].pos;
      const auto &next = verts[p.verts[(i + 1)     % N]].pos;
      float cross_y = (cur.x - prev.x) * (next.z - cur.z) - (cur.z - prev.z) * (next.x - cur.x);
      if (cross_y < -1e-4f)
      {
        std::println(stderr, "[navmesh] CONVEXITY BROKEN at '{}': poly {} edge {}", label, a, i);
        std::abort();
      }
    }
  }

  if (pre_only)
  {
    // PRE-1: all quads.
    for (int a = 0; a < np; ++a)
    {
      assert(polys[a].verts.size() == 4 && "[navmesh-pre] polygon is not a quad");
      assert(polys[a].neighbors.size() == 4 && "[navmesh-pre] quad does not have 4 neighbors");
    }
    // PRE-2: 4 private verts per span.
    assert(nv == np * 4 && "[navmesh-pre] vertex count != 4 * num_polys");
    // PRE-3: island IDs assigned.
    for (int a = 0; a < np; ++a)
      assert(polys[a].island >= 0 && "[navmesh-pre] unassigned island");
  }

  std::println("[navmesh] '{}' OK — {} polys, {} verts.", label, np, nv);
}

// ---------------------------------------------------------------------------
// Navmesh simplification: merge adjacent coplanar convex polygons.
// ---------------------------------------------------------------------------

static bool is_convex_candidate(const navmesh_t &nav,
                                 const std::vector<int32_t> &verts)
{
  // All vertices must produce non-negative cross products for CCW winding.
  const int N = (int)verts.size();
  for (int i = 0; i < N; ++i)
  {
    const auto &prev = nav.vertices[verts[(i - 1 + N) % N]].pos;
    const auto &cur  = nav.vertices[verts[i              ]].pos;
    const auto &next = nav.vertices[verts[(i + 1)     % N]].pos;
    float edge1x = cur.x  - prev.x, edge1z = cur.z  - prev.z;
    float edge2x = next.x - cur.x,  edge2z = next.z - cur.z;
    float cross_y = edge1x * edge2z - edge1z * edge2x;
    if (cross_y < -1e-4f)
      return false;
  }
  return true;
}

void simplify_navmesh(navmesh_t &nav, int max_merges)
{
  const int orig_poly_count = (int)nav.polygons.size();

  // ---- Step 8a: vertex deduplication ----
  // Each span produced 4 private vertices; adjacent spans share positions.
  // Unify so that shared edges have identical vertex indices.

  const int nv = (int)nav.vertices.size();
  std::vector<int32_t> vert_remap(nv);
  for (int i = 0; i < nv; ++i) vert_remap[i] = i;

  constexpr float EPS = 1e-3f;
  for (int i = 0; i < nv; ++i)
  {
    if (vert_remap[i] != i) continue; // already remapped
    const auto &pi = nav.vertices[i].pos;
    for (int j = i + 1; j < nv; ++j)
    {
      if (vert_remap[j] != j) continue;
      const auto &pj = nav.vertices[j].pos;
      float dx = pi.x - pj.x, dy = pi.y - pj.y, dz = pi.z - pj.z;
      if (dx*dx + dy*dy + dz*dz < EPS*EPS)
        vert_remap[j] = i;
    }
  }

  // Apply remap to all polygon vertex lists.
  for (auto &poly : nav.polygons)
    for (auto &v : poly.verts)
      v = vert_remap[v];

  // Build compact vertex list and second remap for used indices.
  std::vector<int32_t> compact_remap(nv, -1);
  std::vector<nav_vertex_t> new_verts;
  for (int i = 0; i < nv; ++i)
  {
    if (vert_remap[i] == i)
    {
      compact_remap[i] = (int32_t)new_verts.size();
      new_verts.push_back(nav.vertices[i]);
    }
  }
  nav.vertices = std::move(new_verts);

  // Apply compact remap.
  for (auto &poly : nav.polygons)
    for (auto &v : poly.verts)
      v = compact_remap[vert_remap[v]];

  // ---- Step 8b: greedy convex polygon merging ----
  const int np = (int)nav.polygons.size();
  std::vector<bool> deleted(np, false);

  // We need a fast way to update neighbor references when B is absorbed into A.
  // After each merge we do a linear scan — acceptable for navmesh sizes.

  int merges_done = 0;
  bool any_merged = true;
  while (any_merged && merges_done < max_merges)
  {
    any_merged = false;
    for (int ai = 0; ai < np && merges_done < max_merges; ++ai)
    {
      if (deleted[ai]) continue;
      nav_polygon_t &A = nav.polygons[ai];
      const int Na = (int)A.verts.size();

      for (int ei = 0; ei < Na; ++ei)
      {
        int bi = A.neighbors[ei];
        if (bi < 0 || deleted[bi]) continue;

        nav_polygon_t &B = nav.polygons[bi];

        // Must be same island.
        if (A.island != B.island) continue;

        // Must be coplanar: check that all of B's vertices have the same Y
        // as A's vertices (within epsilon — flat geometry assumption).
        float ay = nav.vertices[A.verts[0]].pos.y;
        float by = nav.vertices[B.verts[0]].pos.y;
        if (std::abs(ay - by) > EPS) continue;

        // Find the reverse edge in B that shares the same two vertices as
        // A's edge ei.  A's edge ei runs from A.verts[ei] to A.verts[(ei+1)%Na].
        // The reverse edge in B must run from A.verts[(ei+1)%Na] to A.verts[ei].
        const int Nb = (int)B.verts.size();
        int shared_v0 = A.verts[ei];
        int shared_v1 = A.verts[(ei + 1) % Na];
        int ej = -1;
        for (int k = 0; k < Nb; ++k)
        {
          if (B.verts[k] == shared_v1 && B.verts[(k + 1) % Nb] == shared_v0)
          { ej = k; break; }
        }
        if (ej < 0) continue;

        // Count consecutive shared edges starting from (ei, ej).
        // In A they advance forward: ei, ei+1, ei+2, ...
        // In B they advance backward: ej, ej-1, ej-2, ...
        int shared_count = 1;
        {
          int a_edge = (ei + 1) % Na;
          int b_edge = (ej - 1 + Nb) % Nb;
          while (shared_count < Na - 1 && shared_count < Nb - 1)
          {
            if (A.neighbors[a_edge] != bi || B.neighbors[b_edge] != ai) break;
            int av0 = A.verts[a_edge], av1 = A.verts[(a_edge + 1) % Na];
            int bv0 = B.verts[b_edge], bv1 = B.verts[(b_edge + 1) % Nb];
            if (bv0 != av1 || bv1 != av0) break;
            ++shared_count;
            a_edge = (a_edge + 1) % Na;
            b_edge = (b_edge - 1 + Nb) % Nb;
          }
        }

        // Build candidate merged vertex list.
        // From A: Na - shared_count vertices starting after the shared run.
        // From B: Nb - shared_count vertices starting after B's shared endpoint.
        std::vector<int32_t> merged_verts;
        merged_verts.reserve(Na + Nb - 2 * shared_count);
        for (int k = 0; k < Na - shared_count; ++k)
          merged_verts.push_back(A.verts[(ei + shared_count + k) % Na]);
        for (int k = 0; k < Nb - shared_count; ++k)
          merged_verts.push_back(B.verts[(ej + 1 + k) % Nb]);

        // Sanity: reject if merged polygon has duplicate vertices.
        {
          bool has_dup = false;
          for (int m = 0; m < (int)merged_verts.size() && !has_dup; ++m)
            for (int n = m + 1; n < (int)merged_verts.size() && !has_dup; ++n)
              if (merged_verts[m] == merged_verts[n]) has_dup = true;
          if (has_dup) continue;
        }

        if (!is_convex_candidate(nav, merged_verts)) continue;

        // Build merged neighbor list to match merged_verts.
        // Na - shared_count edges from A, Nb - shared_count edges from B.
        std::vector<int32_t> merged_neighbors;
        merged_neighbors.reserve(Na + Nb - 2 * shared_count);
        for (int k = 0; k < Na - shared_count; ++k)
          merged_neighbors.push_back(A.neighbors[(ei + shared_count + k) % Na]);
        for (int k = 0; k < Nb - shared_count; ++k)
          merged_neighbors.push_back(B.neighbors[(ej + 1 + k) % Nb]);

        // Commit the merge: A absorbs B.
        A.verts     = std::move(merged_verts);
        A.neighbors = std::move(merged_neighbors);
        deleted[bi] = true;
        any_merged  = true;
        ++merges_done;

        // Fix up all polygons that pointed to B — redirect to A,
        // updating the edge index to match the new neighbor list.
        for (int ci = 0; ci < np; ++ci)
        {
          if (deleted[ci] || ci == ai) continue;
          nav_polygon_t &C = nav.polygons[ci];
          for (int k = 0; k < (int)C.neighbors.size(); ++k)
          {
            if (C.neighbors[k] == bi)
              C.neighbors[k] = ai;
          }
        }

        // Hit max_merges? Stop immediately.
        if (merges_done >= max_merges) { any_merged = false; }

        // Restart edge scan for this polygon since it changed.
        break;
      }
    }
  }
  std::println("[bake] simplify_navmesh: {} merges performed (limit={}).", merges_done,
               max_merges == INT_MAX ? -1 : max_merges);
  // ---- Compact: remove deleted polygons and remap neighbor indices ----

  std::vector<int32_t> poly_remap(np, -1);
  std::vector<nav_polygon_t> compact_polys;
  compact_polys.reserve(np);
  for (int i = 0; i < np; ++i)
  {
    if (!deleted[i])
    {
      poly_remap[i] = (int32_t)compact_polys.size();
      compact_polys.push_back(std::move(nav.polygons[i]));
    }
  }
  nav.polygons = std::move(compact_polys);

  for (auto &poly : nav.polygons)
    for (auto &nb : poly.neighbors)
      if (nb >= 0) nb = poly_remap[nb];

  // Debug: report state right after merge+compact, before collinear removal.
  {
    int total_verts_ref = 0;
    float mn_x = FLT_MAX, mx_x = -FLT_MAX, mn_z = FLT_MAX, mx_z = -FLT_MAX;
    for (const auto &p : nav.polygons)
    {
      total_verts_ref += (int)p.verts.size();
      for (int v : p.verts)
      {
        mn_x = std::min(mn_x, nav.vertices[v].pos.x);
        mx_x = std::max(mx_x, nav.vertices[v].pos.x);
        mn_z = std::min(mn_z, nav.vertices[v].pos.z);
        mx_z = std::max(mx_z, nav.vertices[v].pos.z);
      }
    }
    std::println("[bake] After merge: {} polys, {} unique verts, {} vert refs, bounds x=[{},{}] z=[{},{}]",
                 (int)nav.polygons.size(), (int)nav.vertices.size(), total_verts_ref,
                 mn_x, mx_x, mn_z, mx_z);
    for (int i = 0; i < (int)nav.polygons.size(); ++i)
      std::println("[bake]   poly[{}]: {} verts, island={}", i,
                   (int)nav.polygons[i].verts.size(), nav.polygons[i].island);
  }

  // ---- Step 8c: collinear vertex removal ----
  // After merging, a polygon's boundary may contain vertices that are
  // exactly collinear with their neighbours (old interior shared-edge midpoints).
  //
  // Vertex i is removable when both its incoming edge (i-1) and outgoing edge (i)
  // share the SAME neighbor (including both being -1), and the three consecutive
  // points are collinear. When the shared neighbor is a real polygon Q, we also
  // remove the matching collinear vertex from Q (by neighbor symmetry, both
  // reverse edges in Q point back to P, so Q's vertex is also collinear).

  // Run globally until no more collinear vertices are found, because removing
  // a vertex from neighbor Q can expose new collinear vertices in Q.
  bool global_removed = true;
  while (global_removed)
  {
    global_removed = false;
    for (int pi = 0; pi < (int)nav.polygons.size(); ++pi)
    {
      auto &p = nav.polygons[pi];
      bool any_removed = true;
      while (any_removed && (int)p.verts.size() > 3)
      {
        any_removed = false;
        const int N = (int)p.verts.size();
        for (int i = 0; i < N; ++i)
        {
          int prev_edge = (i - 1 + N) % N;
          int n_prev = p.neighbors[prev_edge];
          int n_curr = p.neighbors[i];

          // Both flanking edges must share the same neighbor (or both be -1).
          if (n_prev != n_curr) continue;

          const int vi_b = p.verts[i];
          const auto &a = nav.vertices[p.verts[prev_edge]].pos;
          const auto &b = nav.vertices[vi_b].pos;
          const auto &c = nav.vertices[p.verts[(i + 1) % N]].pos;
          float cross_y = (b.x - a.x) * (c.z - b.z) - (b.z - a.z) * (c.x - b.x);
          if (std::abs(cross_y) > 1e-3f) continue;

          // Erase vertex i from P: remove verts[i] and neighbors[i].
          // The surviving edge at prev_edge now spans A→C with neighbor n_prev.
          p.verts.erase    (p.verts.begin()     + i);
          p.neighbors.erase(p.neighbors.begin() + i);

          // Mirror the removal in the neighbor polygon Q.
          if (n_prev >= 0)
          {
            auto &q = nav.polygons[n_prev];
            if ((int)q.verts.size() > 3)
            {
              const int M = (int)q.verts.size();
              for (int j = 0; j < M; ++j)
              {
                if (q.verts[j] == vi_b)
                {
                  q.verts.erase    (q.verts.begin()     + j);
                  q.neighbors.erase(q.neighbors.begin() + j);
                  break;
                }
              }
            }
          }

          any_removed = true;
          global_removed = true;
          break;
        }
      }
    }
  }

  // ---- Assert: no collinear vertices remain ----
  for (int pi = 0; pi < (int)nav.polygons.size(); ++pi)
  {
    const auto &p = nav.polygons[pi];
    const int N = (int)p.verts.size();
    for (int i = 0; i < N; ++i)
    {
      int prev_edge = (i - 1 + N) % N;
      const auto &a = nav.vertices[p.verts[prev_edge]].pos;
      const auto &b = nav.vertices[p.verts[i]].pos;
      const auto &c = nav.vertices[p.verts[(i + 1) % N]].pos;
      float cross_y = (b.x - a.x) * (c.z - b.z) - (b.z - a.z) * (c.x - b.x);
      if (std::abs(cross_y) <= 1e-3f)
      {
        int n_prev = p.neighbors[prev_edge];
        int n_curr = p.neighbors[i];
        // Collinear vertices with DIFFERENT neighbors on their flanking edges
        // are structurally necessary — they mark T-junctions where adjacency
        // changes along a straight edge. Only same-neighbor cases are bugs.
        if (n_prev != n_curr) continue;

        std::println(stderr, "[navmesh] COLLINEAR VERTEX: poly {} vert {} (idx {}), "
                     "pos=({:.2f},{:.2f},{:.2f}), cross_y={:.6f}, neighbors=[{}, {}]",
                     pi, i, p.verts[i], b.x, b.y, b.z, cross_y,
                     n_prev, n_curr);
        assert(false && "[navmesh] collinear vertex not removed during simplification");
      }
    }
  }

  // Compact vertex list: after collinear removal some vertices may be unused.
  {
    const int nv2 = (int)nav.vertices.size();
    std::vector<bool>    used(nv2, false);
    for (const auto &p : nav.polygons)
      for (int v : p.verts)
        used[v] = true;

    std::vector<int32_t> v2_remap(nv2, -1);
    std::vector<nav_vertex_t> used_verts;
    for (int i = 0; i < nv2; ++i)
      if (used[i]) { v2_remap[i] = (int32_t)used_verts.size(); used_verts.push_back(nav.vertices[i]); }
    nav.vertices = std::move(used_verts);

    for (auto &p : nav.polygons)
      for (auto &v : p.verts)
        v = v2_remap[v];
  }

  std::println("[bake] Simplified: {} → {} polygons, {} vertices.",
               orig_poly_count, (int)nav.polygons.size(), (int)nav.vertices.size());
}

// ---------------------------------------------------------------------------

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

  // --- 6. Quad generation ---
  // Each span emits one flat quad at its floor_y.
  // Vertices are NOT deduplicated across spans — each span has 4 private corners.
  //
  //   Quad corners (CCW from above):
  //     V0 = (cx-half, y, cz-half)  SW
  //     V1 = (cx+half, y, cz-half)  SE
  //     V2 = (cx+half, y, cz+half)  NE
  //     V3 = (cx-half, y, cz+half)  NW
  //
  //   Polygon s: verts [V0, V1, V2, V3]
  //   Edge 0 (SW→SE) = south, Edge 1 (SE→NE) = east,
  //   Edge 2 (NE→NW) = north, Edge 3 (NW→SW) = west

  navmesh_t &nav = map.navmesh;
  nav.vertices.clear();
  nav.polygons.clear();
  nav.vertices.reserve(num_spans * 4);
  nav.polygons.reserve(num_spans);

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

    nav_polygon_t quad;
    quad.verts     = {v0, v0+1, v0+2, v0+3};
    quad.neighbors = {-1, -1, -1, -1}; // filled in step 7
    quad.island = sp.island;

    nav.polygons.push_back(quad);
  }

  // --- 7. Fill cross-span adjacency ---
  // Polygon index == span index. Edge d corresponds to direction d:
  //   0=South(iz-1), 1=East(ix+1), 2=North(iz+1), 3=West(ix-1)
  // Reverse direction is (d+2)%4.

  for (int s = 0; s < num_spans; ++s)
  {
    const nav_span_t &sp = spans[s];

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

      nav.polygons[s].neighbors[d] = best_t;
      nav.polygons[best_t].neighbors[(d + 2) % 4] = s;
    }
  }

  std::println("[bake] Navmesh: {} spans, {} polygons, {} islands.",
               num_spans, (int)nav.polygons.size(), num_islands);

  // --- 8. Pre-simplify invariant check ---
  // bake_map only generates the raw triangle soup.
  // The caller is responsible for calling simplify_navmesh() afterward.
  assert_navmesh_invariants(nav, "pre-simplify", /*pre_only=*/true);

}

} // namespace shared
