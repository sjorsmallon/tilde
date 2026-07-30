#include "shared/map_baker.hpp"
#include "shared/navmesh.hpp"
#include <cassert>
#include <cmath>
#include <print>

// ---------------------------------------------------------------------------
// Helper: check the symmetric-neighbor invariant. Aborts on violation.
static void check_symmetry(const navmesh_t &nav, const char *label)
{
  const int np = (int)nav.polygons.size();
  for (int a = 0; a < np; ++a)
    for (int nb : nav.polygons[a].neighbors)
    {
      if (nb < 0) continue;
      bool found = false;
      for (int back : nav.polygons[nb].neighbors)
        if (back == a) { found = true; break; }
      if (!found)
      {
        std::println(stderr, "[{}] symmetry broken: poly {} → poly {} but not back", label, a, nb);
        std::abort();
      }
    }
}

// Check all vertex indices in range and all neighbor indices valid.
static void check_indices(const navmesh_t &nav, const char *label)
{
  const int np = (int)nav.polygons.size();
  const int nv = (int)nav.vertices.size();
  for (int a = 0; a < np; ++a)
  {
    for (int v : nav.polygons[a].vertices)
    {
      if (v < 0 || v >= nv)
      {
        std::println(stderr, "[{}] poly {} vertex {} out of range [0,{})", label, a, v, nv);
        std::abort();
      }
    }
    for (int nb : nav.polygons[a].neighbors)
    {
      if (nb != -1 && (nb < 0 || nb >= np))
      {
        std::println(stderr, "[{}] poly {} neighbor {} out of range", label, a, nb);
        std::abort();
      }
      if (nb == a)
      {
        std::println(stderr, "[{}] poly {} is its own neighbor", label, a);
        std::abort();
      }
    }
  }
}

// ---------------------------------------------------------------------------
// TEST 1: Single span — one quad, no merging needed.
//
// Raw input (what bake_map emits for 1 span, cell_size=16, at origin):
//   V0=(-8,0,-8) SW  V1=(8,0,-8) SE  V2=(8,0,8) NE  V3=(-8,0,8) NW
//   quad=[V0,V1,V2,V3]  neighbors=[-1,-1,-1,-1]
//
// Expected after simplify_navmesh():
//   1 polygon, 4 vertices, all neighbors == -1.
static void test_single_span()
{
  navmesh_t nav;
  nav.vertices = {
    {{-8.f, 0.f, -8.f}},  // 0 SW
    {{ 8.f, 0.f, -8.f}},  // 1 SE
    {{ 8.f, 0.f,  8.f}},  // 2 NE
    {{-8.f, 0.f,  8.f}},  // 3 NW
  };
  nav.polygons.resize(1);
  nav.polygons[0] = {{0, 1, 2, 3}, {-1, -1, -1, -1}, 0};

  shared::simplify_navmesh(nav);

  check_indices(nav, "single_span");
  check_symmetry(nav, "single_span");

  assert(nav.polygons.size() == 1 && "expected 1 polygon");
  assert(nav.vertices.size() == 4 && "expected 4 vertices");
  assert(nav.polygons[0].vertices.size() == 4 && "polygon must be a quad");
  for (int nb : nav.polygons[0].neighbors)
    assert(nb == -1 && "standalone polygon has no neighbors");

  // Verify CCW winding from above.
  const auto &p = nav.polygons[0];
  const int N = (int)p.vertices.size();
  for (int i = 0; i < N; ++i)
  {
    const auto &prev = nav.vertices[p.vertices[(i - 1 + N) % N]].pos;
    const auto &cur  = nav.vertices[p.vertices[i              ]].pos;
    const auto &next = nav.vertices[p.vertices[(i + 1)     % N]].pos;
    float cross_y = (cur.x - prev.x) * (next.z - cur.z) - (cur.z - prev.z) * (next.x - cur.x);
    assert(cross_y >= -1e-4f && "winding broken after merge");
  }

  std::println("test_single_span PASSED");
}

// ---------------------------------------------------------------------------
// TEST 2: 2x1 strip — two side-by-side quads sharing an East-West edge.
//
// Span 0: ix=0 iz=0  cx=0  cz=0  y=0
//   V0=(-8,0,-8) V1=(8,0,-8) V2=(8,0,8) V3=(-8,0,8)
//   quad0: [V0,V1,V2,V3]  neighbors=[-1, 1, -1, -1]  (east→quad1)
//
// Span 1: ix=1 iz=0  cx=16 cz=0  y=0
//   V4=(8,0,-8) V5=(24,0,-8) V6=(24,0,8) V7=(8,0,8)
//   quad1: [V4,V5,V6,V7]  neighbors=[-1, -1, -1, 0]  (west→quad0)
//
// After simplify:
//   - vertex dedup: V1==V4 and V2==V7 → 6 unique vertices
//   - merge into 1 polygon, collinear midpoints removed → 4 vertices
static void test_two_span_strip()
{
  navmesh_t nav;
  nav.vertices = {
    {{-8.f, 0.f, -8.f}},  // 0 SW of span0
    {{ 8.f, 0.f, -8.f}},  // 1 SE of span0
    {{ 8.f, 0.f,  8.f}},  // 2 NE of span0
    {{-8.f, 0.f,  8.f}},  // 3 NW of span0
    {{ 8.f, 0.f, -8.f}},  // 4 SW of span1 (dup of 1)
    {{24.f, 0.f, -8.f}},  // 5 SE of span1
    {{24.f, 0.f,  8.f}},  // 6 NE of span1
    {{ 8.f, 0.f,  8.f}},  // 7 NW of span1 (dup of 2)
  };
  nav.polygons.resize(2);
  nav.polygons[0] = {{0, 1, 2, 3}, {-1, 1, -1, -1}, 0};  // quad0: east→quad1
  nav.polygons[1] = {{4, 5, 6, 7}, {-1, -1, -1, 0}, 0};  // quad1: west→quad0

  shared::simplify_navmesh(nav);

  check_indices(nav, "two_span_strip");
  check_symmetry(nav, "two_span_strip");

  // Diagnostic dump.
  std::println("  two_span_strip result: {} polygon(s), {} vertices",
               nav.polygons.size(), nav.vertices.size());
  for (int i = 0; i < (int)nav.polygons.size(); ++i)
  {
    const auto &p = nav.polygons[i];
    std::println("  poly[{}]: island={} verts={} neighbors={}",
                 i, p.island, p.vertices.size(), p.neighbors.size());
    for (int k = 0; k < (int)p.vertices.size(); ++k)
    {
      const auto &v = nav.vertices[p.vertices[k]].pos;
      std::println("    [{}] vi={} pos=({:.1f},{:.1f},{:.1f})  nb={}",
                   k, p.vertices[k], v.x, v.y, v.z, p.neighbors[k]);
    }
  }

  assert(nav.polygons.size() == 1 && "expected 1 merged polygon for 2-span strip");
  assert(nav.vertices.size() == 4 && "expected 4 corner vertices after collinear pruning");
  assert(nav.polygons[0].vertices.size() == 4 && "merged strip must be a quad after collinear pruning");

  // Verify convexity.
  const auto &p = nav.polygons[0];
  const int N = (int)p.vertices.size();
  for (int i = 0; i < N; ++i)
  {
    const auto &prev = nav.vertices[p.vertices[(i - 1 + N) % N]].pos;
    const auto &cur  = nav.vertices[p.vertices[i              ]].pos;
    const auto &next = nav.vertices[p.vertices[(i + 1)     % N]].pos;
    float cross_y = (cur.x - prev.x) * (next.z - cur.z) - (cur.z - prev.z) * (next.x - cur.x);
    assert(cross_y >= -1e-4f && "winding broken in strip");
  }

  // Bounds preserved.
  float min_x = 1e9f, max_x = -1e9f, min_z = 1e9f, max_z = -1e9f;
  for (const auto &v : nav.vertices)
  {
    min_x = std::min(min_x, v.pos.x); max_x = std::max(max_x, v.pos.x);
    min_z = std::min(min_z, v.pos.z); max_z = std::max(max_z, v.pos.z);
  }
  assert(std::abs(min_x - (-8.f)) < 0.01f && "min_x changed");
  assert(std::abs(max_x - 24.f)   < 0.01f && "max_x changed");
  assert(std::abs(min_z - (-8.f)) < 0.01f && "min_z changed");
  assert(std::abs(max_z -   8.f)  < 0.01f && "max_z changed");

  std::println("test_two_span_strip PASSED");
}

// ---------------------------------------------------------------------------
// TEST 3: Vertex deduplication only (max_merges = 0).
//
// Same 2x1 strip input as test_two_span_strip but with max_merges=0.
//
// Expected:
//   - 2 polygons (no merges)
//   - 6 unique vertices (V4==V1 and V7==V2 collapsed)
static void test_vertex_dedup()
{
  navmesh_t nav;
  nav.vertices = {
    {{-8.f, 0.f, -8.f}},  // 0
    {{ 8.f, 0.f, -8.f}},  // 1
    {{ 8.f, 0.f,  8.f}},  // 2
    {{-8.f, 0.f,  8.f}},  // 3
    {{ 8.f, 0.f, -8.f}},  // 4 == 1
    {{24.f, 0.f, -8.f}},  // 5
    {{24.f, 0.f,  8.f}},  // 6
    {{ 8.f, 0.f,  8.f}},  // 7 == 2
  };
  nav.polygons.resize(2);
  nav.polygons[0] = {{0, 1, 2, 3}, {-1, 1, -1, -1}, 0};
  nav.polygons[1] = {{4, 5, 6, 7}, {-1, -1, -1, 0}, 0};

  shared::simplify_navmesh(nav, /*max_merges=*/0);

  check_indices(nav, "vertex_dedup");
  check_symmetry(nav, "vertex_dedup");

  assert(nav.polygons.size() == 2 && "dedup must not merge polygons");
  assert(nav.vertices.size() == 6 && "expected 6 unique vertices after dedup");

  // No two vertices within EPS of each other.
  constexpr float EPS = 1e-3f;
  const int nv = (int)nav.vertices.size();
  for (int i = 0; i < nv; ++i)
    for (int j = i + 1; j < nv; ++j)
    {
      const auto &a = nav.vertices[i].pos;
      const auto &b = nav.vertices[j].pos;
      float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
      float dist2 = dx*dx + dy*dy + dz*dz;
      if (dist2 < EPS * EPS)
      {
        std::println(stderr, "vertex_dedup: duplicate vertices {} and {} at ({},{},{})",
                     i, j, a.x, a.y, a.z);
        std::abort();
      }
    }

  std::println("test_vertex_dedup PASSED");
}

// ---------------------------------------------------------------------------
// TEST 4: 2x2 grid — four quads arranged in a square.
//
// Span layout (ix, iz):
//   (0,1) (1,1)
//   (0,0) (1,0)
//
// cell_size=16, half=8, origin at (0,0):
//   Span 0 (0,0): V0=(-8,0,-8) V1=(8,0,-8) V2=(8,0,8) V3=(-8,0,8)
//   Span 1 (1,0): V4=(8,0,-8) V5=(24,0,-8) V6=(24,0,8) V7=(8,0,8)
//   Span 2 (0,1): V8=(-8,0,8) V9=(8,0,8) V10=(8,0,24) V11=(-8,0,24)
//   Span 3 (1,1): V12=(8,0,8) V13=(24,0,8) V14=(24,0,24) V15=(8,0,24)
//
// Quad neighbors (south=0, east=1, north=2, west=3):
//   q0: [-1, 1, 2, -1]   q1: [-1, -1, 3, 0]
//   q2: [0, 3, -1, -1]   q3: [1, -1, -1, 2]
//
// After simplify: expect 1 polygon with 4 corner vertices.
static void test_2x2_grid()
{
  navmesh_t nav;
  nav.vertices = {
    {{-8.f, 0.f, -8.f}},  //  0 SW s0
    {{ 8.f, 0.f, -8.f}},  //  1 SE s0
    {{ 8.f, 0.f,  8.f}},  //  2 NE s0
    {{-8.f, 0.f,  8.f}},  //  3 NW s0
    {{ 8.f, 0.f, -8.f}},  //  4 SW s1 (dup of 1)
    {{24.f, 0.f, -8.f}},  //  5 SE s1
    {{24.f, 0.f,  8.f}},  //  6 NE s1
    {{ 8.f, 0.f,  8.f}},  //  7 NW s1 (dup of 2)
    {{-8.f, 0.f,  8.f}},  //  8 SW s2 (dup of 3)
    {{ 8.f, 0.f,  8.f}},  //  9 SE s2 (dup of 2)
    {{ 8.f, 0.f, 24.f}},  // 10 NE s2
    {{-8.f, 0.f, 24.f}},  // 11 NW s2
    {{ 8.f, 0.f,  8.f}},  // 12 SW s3 (dup of 2)
    {{24.f, 0.f,  8.f}},  // 13 SE s3 (dup of 6)
    {{24.f, 0.f, 24.f}},  // 14 NE s3
    {{ 8.f, 0.f, 24.f}},  // 15 NW s3 (dup of 10)
  };

  nav.polygons.resize(4);
  nav.polygons[0] = {{ 0,  1,  2,  3}, {-1,  1,  2, -1}, 0};  // s0
  nav.polygons[1] = {{ 4,  5,  6,  7}, {-1, -1,  3,  0}, 0};  // s1
  nav.polygons[2] = {{ 8,  9, 10, 11}, { 0,  3, -1, -1}, 0};  // s2
  nav.polygons[3] = {{12, 13, 14, 15}, { 1, -1, -1,  2}, 0};  // s3

  check_indices(nav, "2x2_pre");
  check_symmetry(nav, "2x2_pre");

  shared::simplify_navmesh(nav);

  check_indices(nav, "2x2_post");
  check_symmetry(nav, "2x2_post");

  // Diagnostic dump.
  std::println("  2x2_grid result: {} polygon(s), {} vertices",
               nav.polygons.size(), nav.vertices.size());
  for (int i = 0; i < (int)nav.polygons.size(); ++i)
  {
    const auto &p = nav.polygons[i];
    std::println("  poly[{}]: island={} verts={} neighbors={}",
                 i, p.island, p.vertices.size(), p.neighbors.size());
    for (int k = 0; k < (int)p.vertices.size(); ++k)
    {
      const auto &v = nav.vertices[p.vertices[k]].pos;
      std::println("    [{}] vi={} pos=({:.1f},{:.1f},{:.1f})  nb={}",
                   k, p.vertices[k], v.x, v.y, v.z, p.neighbors[k]);
    }
  }

  assert(nav.polygons.size() == 1 && "expected 1 merged polygon for 2x2 grid");
  assert(nav.vertices.size() == 4 && "expected 4 corner vertices");
  assert(nav.polygons[0].vertices.size() == 4 && "merged polygon must be a quad");

  for (int nb : nav.polygons[0].neighbors)
    assert(nb == -1 && "standalone polygon has no neighbors");

  // Verify convexity.
  const auto &p = nav.polygons[0];
  const int N = (int)p.vertices.size();
  for (int i = 0; i < N; ++i)
  {
    const auto &prev = nav.vertices[p.vertices[(i - 1 + N) % N]].pos;
    const auto &cur  = nav.vertices[p.vertices[i              ]].pos;
    const auto &next = nav.vertices[p.vertices[(i + 1)     % N]].pos;
    float cross_y = (cur.x - prev.x) * (next.z - cur.z) - (cur.z - prev.z) * (next.x - cur.x);
    assert(cross_y >= -1e-4f && "winding broken in 2x2 grid");
  }

  // Bounds preserved.
  float min_x = 1e9f, max_x = -1e9f, min_z = 1e9f, max_z = -1e9f;
  for (const auto &v : nav.vertices)
  {
    min_x = std::min(min_x, v.pos.x); max_x = std::max(max_x, v.pos.x);
    min_z = std::min(min_z, v.pos.z); max_z = std::max(max_z, v.pos.z);
  }
  assert(std::abs(min_x - (-8.f)) < 0.01f && "min_x changed");
  assert(std::abs(max_x - 24.f)   < 0.01f && "max_x changed");
  assert(std::abs(min_z - (-8.f)) < 0.01f && "min_z changed");
  assert(std::abs(max_z - 24.f)   < 0.01f && "max_z changed");

  std::println("test_2x2_grid PASSED");
}

// ---------------------------------------------------------------------------
// TEST 5: 3x1 strip — tests interior collinear vertex removal with neighbors.
//
// Three quads in a row (east-west). After merging left pair, the middle
// collinear vertices have real neighbors. The collinear removal must handle
// them correctly.
//
// Span 0: V0=(-8,0,-8) V1=(8,0,-8) V2=(8,0,8) V3=(-8,0,8)
// Span 1: V4=(8,0,-8) V5=(24,0,-8) V6=(24,0,8) V7=(8,0,8)
// Span 2: V8=(24,0,-8) V9=(40,0,-8) V10=(40,0,8) V11=(24,0,8)
//
// After simplify: 1 polygon, 4 vertices: (-8,0,-8),(40,0,-8),(40,0,8),(-8,0,8)
static void test_3x1_strip()
{
  navmesh_t nav;
  nav.vertices = {
    {{-8.f, 0.f, -8.f}},  // 0 SW s0
    {{ 8.f, 0.f, -8.f}},  // 1 SE s0
    {{ 8.f, 0.f,  8.f}},  // 2 NE s0
    {{-8.f, 0.f,  8.f}},  // 3 NW s0
    {{ 8.f, 0.f, -8.f}},  // 4 SW s1 (dup of 1)
    {{24.f, 0.f, -8.f}},  // 5 SE s1
    {{24.f, 0.f,  8.f}},  // 6 NE s1
    {{ 8.f, 0.f,  8.f}},  // 7 NW s1 (dup of 2)
    {{24.f, 0.f, -8.f}},  // 8 SW s2 (dup of 5)
    {{40.f, 0.f, -8.f}},  // 9 SE s2
    {{40.f, 0.f,  8.f}},  // 10 NE s2
    {{24.f, 0.f,  8.f}},  // 11 NW s2 (dup of 6)
  };

  nav.polygons.resize(3);
  nav.polygons[0] = {{0, 1, 2, 3},   {-1, 1, -1, -1}, 0};  // east→q1
  nav.polygons[1] = {{4, 5, 6, 7},   {-1, 2, -1,  0}, 0};  // east→q2, west→q0
  nav.polygons[2] = {{8, 9, 10, 11}, {-1, -1, -1, 1}, 0};  // west→q1

  check_indices(nav, "3x1_pre");
  check_symmetry(nav, "3x1_pre");

  shared::simplify_navmesh(nav);

  check_indices(nav, "3x1_post");
  check_symmetry(nav, "3x1_post");

  std::println("  3x1_strip result: {} polygon(s), {} vertices",
               nav.polygons.size(), nav.vertices.size());
  for (int i = 0; i < (int)nav.polygons.size(); ++i)
  {
    const auto &p = nav.polygons[i];
    std::println("  poly[{}]: island={} verts={} neighbors={}",
                 i, p.island, p.vertices.size(), p.neighbors.size());
    for (int k = 0; k < (int)p.vertices.size(); ++k)
    {
      const auto &v = nav.vertices[p.vertices[k]].pos;
      std::println("    [{}] vi={} pos=({:.1f},{:.1f},{:.1f})  nb={}",
                   k, p.vertices[k], v.x, v.y, v.z, p.neighbors[k]);
    }
  }

  assert(nav.polygons.size() == 1 && "expected 1 merged polygon for 3x1 strip");
  assert(nav.vertices.size() == 4 && "expected 4 corner vertices");
  assert(nav.polygons[0].vertices.size() == 4 && "merged polygon must be a quad");

  for (int nb : nav.polygons[0].neighbors)
    assert(nb == -1 && "standalone polygon has no neighbors");

  // Bounds preserved.
  float min_x = 1e9f, max_x = -1e9f, min_z = 1e9f, max_z = -1e9f;
  for (const auto &v : nav.vertices)
  {
    min_x = std::min(min_x, v.pos.x); max_x = std::max(max_x, v.pos.x);
    min_z = std::min(min_z, v.pos.z); max_z = std::max(max_z, v.pos.z);
  }
  assert(std::abs(min_x - (-8.f)) < 0.01f && "min_x changed");
  assert(std::abs(max_x - 40.f)   < 0.01f && "max_x changed");
  assert(std::abs(min_z - (-8.f)) < 0.01f && "min_z changed");
  assert(std::abs(max_z -   8.f)  < 0.01f && "max_z changed");

  std::println("test_3x1_strip PASSED");
}

int main()
{
  test_single_span();
  test_two_span_strip();
  test_vertex_dedup();
  test_2x2_grid();
  test_3x1_strip();
  std::println("All navmesh tests passed.");
  return 0;
}
