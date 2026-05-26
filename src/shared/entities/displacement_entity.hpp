#pragma once

#include "../asset.hpp"
#include "../entity.hpp"
#include "../shapes.hpp"
#include <string>

namespace network
{

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

  SCHEMA_FIELD_DEFAULT(int32, active_face,
                       Schema_Flags::Editable | Schema_Flags::Saveable, -1);

  SCHEMA_FIELD_DEFAULT(int32, subdivision_level,
                       Schema_Flags::Editable | Schema_Flags::Saveable, 4);

  SCHEMA_FIELD(displacement_array_t, displacements,
               Schema_Flags::Saveable);

  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);

  // Initialize displacement grid to all zeros for the given face and subdivision.
  void init_displacement(int face, int subdiv);

  // Number of vertices along one edge of the grid: subdiv + 1
  int grid_size() const { return subdivision_level + 1; }

  // Total number of vertices in the grid
  int vertex_count() const
  {
    int gs = grid_size();
    return gs * gs;
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
// If active_face < 0, generates a simple box mesh.
// Otherwise generates the subdivided displaced face + 5 remaining box faces.
assets::mesh_asset_t generate_displacement_mesh(const Displacement_Entity &ent);

} // namespace network
