#define ENTITIES_WANT_INCLUDES
#include "../shared/entities/generated/entities/spot_light_entity_generated.hpp"
#include "spot_beam.hpp"

#include "../shared/asset_types.hpp"
#include "../shared/color.hpp"
#include "../shared/log.hpp"

#include <algorithm>
#include <cmath>

namespace client
{

namespace
{

// shared/lighting.cpp's spot_shadow_projection clamps the cone the same way: tan runs away past this.
constexpr float MIN_OUTER_DEGREES = 1.f;
constexpr float MAX_OUTER_DEGREES = 85.f;

struct spot_beam_resources_t
{
  renderer::mesh_handle_t     edges;
  renderer::material_handle_t edge;
};

// Apex at the origin, the cap at x = 1 with corners at y, z = +-1: x is the light's forward, y its up,
// z its right, the right-handed order linalg::basis_from's three come in.
constexpr vec3f UNIT_PYRAMID_APEX       = {0.f, 0.f, 0.f};
constexpr vec3f UNIT_PYRAMID_CORNERS[4] = {{1.f, -1.f, -1.f}, {1.f, 1.f, -1.f}, {1.f, 1.f, 1.f}, {1.f, -1.f, 1.f}};

// The four apex-to-corner edges, each a quad mesh_beam_edge.vert widens on screen: a vertex holds its
// own end of the edge, the other end in the normal slot, the side it sits on in uv.x and the end in uv.y.
assets::mesh_asset_t make_unit_beam_edges()
{
  assets::mesh_asset_t edges{};
  for (const vec3f& corner : UNIT_PYRAMID_CORNERS)
  {
    const uint32_t base = (uint32_t)edges.vertices.size();
    edges.vertices.push_back({UNIT_PYRAMID_APEX, corner, {-1.f, 0.f}});
    edges.vertices.push_back({UNIT_PYRAMID_APEX, corner, {1.f, 0.f}});
    edges.vertices.push_back({corner, UNIT_PYRAMID_APEX, {1.f, 1.f}});
    edges.vertices.push_back({corner, UNIT_PYRAMID_APEX, {-1.f, 1.f}});
    edges.indices.insert(edges.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }
  return edges;
}

const spot_beam_resources_t& spot_beam_resources()
{
  static const spot_beam_resources_t resources = []
  {
    renderer::material_t material{};
    material.pipeline_state = {.shader      = renderer::shader_t::beam_edge,
                               .blend_mode  = renderer::blend_mode_t::alpha,
                               .cull_mode   = renderer::cull_mode_t::none,
                               .depth_test  = true,
                               .depth_write = false};
    spot_beam_resources_t result{};
    result.edges = renderer::register_mesh(make_unit_beam_edges());
    result.edge  = renderer::register_material(material);
    return result;
  }();
  return resources;
}

} // namespace

spot_beam_t build_spot_beam_for_spot_light(const entities::Spot_Light_Entity& spot,
                                           const shared::path_pose_t& pose)
{
  return spot_beam_t{.apex          = pose.position,
                     .basis         = linalg::basis_from(pose.orientation),
                     .range         = spot.range,
                     .outer_degrees = spot.outer_degrees,
                     .color         = spot.light.color,
                     .light         = spot.entity_id};
}

void draw_spot_beam(pass_builder_t& scene, const spot_beam_t& beam)
{
  if (beam.range <= 0.f)
    return;

  const float outer_degrees = std::clamp(beam.outer_degrees, MIN_OUTER_DEGREES, MAX_OUTER_DEGREES);
  const float tangent       = std::tan(linalg::to_radians(outer_degrees));
  const float half_width    = beam.range * tangent;
  const vec3f forward       = linalg::normalize(beam.basis.forward);
  const vec3f up            = linalg::normalize(beam.basis.up);
  const vec3f right         = linalg::normalize(beam.basis.right);

  scene.beams.push_back(renderer::beam_t{.apex                   = beam.apex,
                                         .forward                = forward,
                                         .up                     = up,
                                         .right                  = right,
                                         .range                  = beam.range,
                                         .tangent_of_outer_angle = tangent,
                                         .color                  = beam.color,
                                         .light                  = beam.light});

  const spot_beam_resources_t& resources = spot_beam_resources();
  if (!resources.edges.valid() || !resources.edge.valid())
  {
    log_error("draw_spot_beam: the beam edges or their material failed to register");
    return;
  }

  linalg::mat4f transform = linalg::mat4f::identity();
  transform[0] = {forward.x * beam.range, forward.y * beam.range, forward.z * beam.range, 0.f};
  transform[1] = {up.x * half_width, up.y * half_width, up.z * half_width, 0.f};
  transform[2] = {right.x * half_width, right.y * half_width, right.z * half_width, 0.f};
  transform[3] = {beam.apex.x, beam.apex.y, beam.apex.z, 1.f};

  renderer::mesh_draw_t edges{};
  edges.mesh               = resources.edges;
  edges.transform          = transform;
  edges.material_overrides = {&resources.edge, 1};
  edges.tint               = color_from_vec3(beam.color);
  edges.shadow_caster      = renderer::shadow_caster_t::none;
  scene.meshes.push_back(edges);
}

} // namespace client
