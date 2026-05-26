#pragma once

#include "../asset.hpp"
#include "../entity.hpp"
#include "../shapes.hpp"
#include <array>
#include <cassert>
#include <string>

namespace network
{

// Identifies one of the six faces of an axis-aligned box.
// Underlying int32 + Invalid = -1 lets it ride the schema's Int32 storage path.
enum class box_face_t : int32_t
{
  Invalid = -1,
  Plus_X = 0,
  Minus_X = 1,
  Plus_Y = 2,
  Minus_Y = 3,
  Plus_Z = 4,
  Minus_Z = 5,
};

inline constexpr size_t box_face_count = 6;

inline constexpr std::array<vec3f, box_face_count> box_face_normals = {{
    vec3f{ 1, 0, 0},
    vec3f{-1, 0, 0},
    vec3f{ 0, 1, 0},
    vec3f{ 0,-1, 0},
    vec3f{ 0, 0, 1},
    vec3f{ 0, 0,-1},
}};

// Tangent (u, v) vectors that span the face plane. Both faces along a given
// axis share the same tangents, so this array is indexed by axis (0=X, 1=Y, 2=Z).
struct box_face_tangents
{
  vec3f u;
  vec3f v;
};

inline constexpr std::array<box_face_tangents, 3> box_face_tangents_by_axis = {{
    {{0, 0, 1}, {0, 1, 0}}, // X faces: grid on YZ plane
    {{1, 0, 0}, {0, 0, 1}}, // Y faces: grid on XZ plane
    {{1, 0, 0}, {0, 1, 0}}, // Z faces: grid on XY plane
}};

inline int box_face_axis(box_face_t face)
{
  assert(face != box_face_t::Invalid);
  return static_cast<int>(face) / 2;
}

inline bool box_face_is_positive(box_face_t face)
{
  assert(face != box_face_t::Invalid);
  return (static_cast<int>(face) % 2) == 0;
}

inline vec3f get_box_face_normal(box_face_t face)
{
  assert(face != box_face_t::Invalid);
  return box_face_normals[static_cast<size_t>(face)];
}

inline box_face_tangents get_box_face_tangents(box_face_t face)
{
  return box_face_tangents_by_axis[box_face_axis(face)];
}

// Schema serializes box_face_t through the Int32 path (matching layout).
template <>
struct Schema_Type_Info<box_face_t>
{
  static constexpr Field_Type type = Field_Type::Int32;
};

// Maximum subdivision level 32 → (32+1)^2 = 1089 vertices → 3267 floats (3 per vertex)
using displacement_array_t = schema_array_t<float32, 3267>;

class Displacement_Entity : public Entity
{
public:
  bool is_collision_geometry() const override { return true; }

  SCHEMA_FIELD(shared::box_volume_t, volume,
               Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable);

  shared::box_volume_t *get_box_volume() override { return &volume; }
  const shared::box_volume_t *get_box_volume() const override { return &volume; }

  SCHEMA_FIELD_DEFAULT(box_face_t, active_face,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       box_face_t::Invalid);

  SCHEMA_FIELD_DEFAULT(int32, subdivision_level,
                       Schema_Flags::Editable | Schema_Flags::Saveable, 4);

  SCHEMA_FIELD(displacement_array_t, displacements,
               Schema_Flags::Saveable);

  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);

  // Initialize displacement grid to all zeros for the given face and subdivision.
  void init_displacement(box_face_t face, int subdiv);

  // Number of vertices along one edge of the grid: subdiv + 1
  int grid_size() const { return subdivision_level + 1; }

  // Total number of vertices in the grid
  int vertex_count() const
  {
    int grid_size = this->grid_size();
    return grid_size * grid_size;
  }

  // Get the face normal direction (unit vector along the displaced face axis).
  vec3f get_face_normal() const;

  // Get the two tangent axes for the active face (span the grid plane).
  void get_face_axes(vec3f &out_u, vec3f &out_v) const;

  // Get the base position (before displacement) of grid vertex (i,j) in local space.
  vec3f get_base_vertex_local(int i, int j) const;

  // Get displacement vector for vertex (i,j) by reinterpreting the float array as vec3f.
  vec3f get_displacement(int i, int j) const;
  void set_displacement(int i, int j, const vec3f &d);

  // Get world-space position of displaced vertex (i,j).
  vec3f get_vertex_world(int i, int j) const;

  DECLARE_SCHEMA(Displacement_Entity)
};

SCHEMA_NAME_FOR_TYPE(Displacement_Entity)

// Generate a mesh_asset_t from a displacement entity's current state.
// If active_face is Invalid, generates a simple box mesh.
// Otherwise generates the subdivided displaced face + 5 remaining box faces.
assets::mesh_asset_t generate_displacement_mesh(const Displacement_Entity &ent);

} // namespace network
