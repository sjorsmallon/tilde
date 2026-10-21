#pragma once

#include "linalg.hpp"
#include "span.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>


namespace shared
{

struct geometry_surface_t
{
  std::string mesh_path;

  linalg::vec3 color{1.f, 1.f, 1.f};
  float roughness = 0.5f;

  bool visible = true;
  bool is_wireframe = false;
};

struct face_uv_channel_t
{
  linalg::vec3 u_axis{0.f, 0.f, 0.f};
  linalg::vec3 v_axis{0.f, 0.f, 0.f};
  float u_shift = 0.f;
  float v_shift = 0.f;

  // world units per texture repeat.
  float u_scale = 128.f;
  float v_scale = 128.f;
};

face_uv_channel_t default_face_uv(const linalg::vec3 &normal);


struct face_surface_t
{
  // a face is identified by it's normal.
  linalg::vec3 key_normal{0.f, 0.f, 0.f};
  float key_distance = 0.f;

  // Indices into map_t::materials, never paths. A material is one per FACE and
  // faces are the most numerous thing in a map; geometry_def.md ss4 has the
  // three ways a per-face string compounds.
  //
  // LAYER 0 and LAYER 1 of a blended face. Read them through
  // face_layer_material rather than by name: that is what makes a third layer a
  // field plus an arm rather than an edit at every site (vertex.hpp owns the
  // count).
  uint16_t material = 0;
  uint16_t blend_material = 0;

  // What "nodraw" was. A flag rather than a magic material name, so a face can
  // be switched off without losing the material it had.
  bool emits_geometry = true;

  face_uv_channel_t uv;

  // AUTHORED inputs with no consumer yet, and deliberately here anyway. Adding
  // runtime state later is free; adding an AUTHORED field later means migrating
  // every map or silently defaulting it, and a wrong lightmap density is not
  // noticed until the bake looks bad everywhere. geometry_def.md ss10.
  float lightmap_scale = 1.f;
  int32_t smoothing_group = 0;

  // TESSELLATION. 0 is flat, which is what every face that predates Track D is
  // and is why none of them needed converting. n cuts the face into an n x n
  // grid of cells, so the grid has (n+1)^2 vertices, row-major, i fastest.
  //
  // This is what displacement_geometry_t became: a displacement was a box with
  // one subdivided face, and it is now a brush with one subdivided face. The
  // exception is gone and the feature is not -- and because it lives on the
  // FACE, every face of every brush has it, which is what makes blending a
  // tessellation feature rather than a displacement one (geometry_def.md
  // Track E).
  //
  // Only a QUAD face can carry a grid: a grid is a bilinear patch over four
  // corners and there is no such thing over five. A subdivided face that stops
  // being a quad loses its grid loudly in sync_face_surfaces.
  int32_t subdivision_level = 0;

  // Per grid vertex, and both are either empty (flat / unpainted) or exactly
  // face_grid_vertex_count(subdivision_level) long. resize_face_grid is what
  // keeps that true across a subdivision change.
  std::vector<linalg::vec3> offsets; // world-space, added to the base vertex

  // The weights of layers 1..BLEND_LAYER_COUNT-1, one per grid vertex; layer
  // 0's is what is left over. That implied-first-layer form is the N-layer
  // scheme evaluated at N = 2, which is why a third layer is another array
  // beside this one and needs no map converted: an absent array reads as zero
  // weight, and a file written before it existed means what it always meant.
  std::vector<float> blend;
};

// --- Blend layers ------------------------------------------------------------
//
// Everything that composes a blended face goes through these, in LAYER terms
// rather than by field name. geometry_def.md ss4 argues two layers on cost
// grounds and ss8 keeps more than two open; this is that door, and the whole of
// it: raising BLEND_LAYER_COUNT (vertex.hpp) adds an arm to each of these and
// moves no caller.

uint16_t face_layer_material(const face_surface_t &face, int layer);
void set_face_layer_material(face_surface_t &face, int layer, uint16_t material);

// The weight of `layer` at one grid vertex, in 0..1. Layer 0 is the remainder,
// so the set always sums to 1 even on a face that has never been painted.
float face_layer_weight(const face_surface_t &face, size_t grid_vertex, int layer);

// Move one grid vertex `amount` toward `layer`, taking what it gains from the
// others in proportion so the weights still sum to 1. Painting toward layer 0
// IS the eraser -- which is why there is no subtract flag and why the tool
// carries a target layer rather than a sign.
void paint_face_layer_weight(face_surface_t &face, size_t grid_vertex, int layer,
                             float amount);

// Whether this face draws through the blend pipeline: a usable grid, weights
// sized for it, and some layer above the base naming a DIFFERENT material.
// Two layers of one material is a blend nobody can see and a second texture
// fetch nobody should pay for.
bool face_is_blended(const face_surface_t &face);

// Grid vertices along one edge of a face subdivided n times, and over the whole
// face. n cells per edge means n+1 vertices per edge.
inline int face_grid_size(int subdivision_level) { return subdivision_level + 1; }
inline int face_grid_vertex_count(int subdivision_level)
{
  const int size = face_grid_size(subdivision_level);
  return size * size;
}

// Whether this face carries a usable grid: subdivided, and with both arrays
// sized for it. A face that answers false is drawn and collided as one flat
// polygon, which is exactly what it was before Track D.
bool face_is_subdivided(const face_surface_t &face);

// Resize `offsets` and `blend` for a new subdivision level, resampling what is
// already there by nearest grid position so changing the level keeps the shape
// the author sculpted instead of flattening it. Level 0 clears both.
void resize_face_grid(face_surface_t &face, int new_subdivision_level);

// A quad face's four corners in canonical grid order: c00, c10, c11, c01, i.e.
// (u,v) = (0,0), (1,0), (1,1), (0,1) in the face's own tangent basis.
//
// CANONICAL is the whole point. Faces are derived and rewound on every hull
// rebuild, so the polygon's own first vertex means nothing across an edit -- the
// grid is anchored to the tangent basis instead (brush_face_grid_tangents), with
// c00 the corner lowest in v then in u. That is what keeps offsets[3] naming the
// same corner of the same face after a vertex drag.
struct face_grid_t
{
  linalg::vec3 corners[4];
};

// Empty unless `polygon` is exactly four points -- a grid is a bilinear patch
// over four corners and there is no such thing over five.
[[nodiscard]] std::optional<face_grid_t> try_face_grid(Span<const linalg::vec3> polygon,
                                                       const linalg::vec3 &normal);

// Undisplaced position of grid vertex (i, j): the bilinear patch over the four
// corners. Out-of-range indices clamp rather than read past the grid.
linalg::vec3 face_grid_base_vertex(const face_grid_t &grid, int subdivision_level,
                                   int i, int j);

// Bit-exact, for "did this edit change anything?" (geometry_values_equal).
bool surfaces_equal(const geometry_surface_t &lhs, const geometry_surface_t &rhs);
bool face_surfaces_identical(const face_surface_t &lhs, const face_surface_t &rhs);

} // namespace shared
