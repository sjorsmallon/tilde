#define ENTITIES_WANT_INCLUDES
#include "lighting.hpp"

#include "entities/entity_reflection.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

std::optional<scene_light_t> try_light_of(const entities::Entity &entity)
{
  if (const entities::Point_Light_Entity* point =
          entities::entity_as<entities::Point_Light_Entity>(&entity))
  {
    scene_light_t light;
    light.kind = light_kind_t::Point;
    light.mode = point->light.mode;
    light.position = point->position;
    light.radiance = radiance_of(point->light);
    light.range = point->range;
    light.source_radius = std::max(point->light.source_radius, 0.f);
    light.casts_shadows = point->light.casts_shadows;
    return light;
  }

  if (const entities::Spot_Light_Entity* spot =
          entities::entity_as<entities::Spot_Light_Entity>(&entity))
  {
    scene_light_t light;
    light.kind = light_kind_t::Spot;
    light.mode = spot->light.mode;
    light.position = spot->position;
    light.forward = linalg::basis_from(spot->orientation).forward;
    light.radiance = radiance_of(spot->light);
    light.range = spot->range;
    light.cos_inner = std::cos(linalg::to_radians(spot->inner_degrees));
    light.cos_outer = std::cos(linalg::to_radians(spot->outer_degrees));
    light.source_radius = std::max(spot->light.source_radius, 0.f);
    light.casts_shadows = spot->light.casts_shadows;
    return light;
  }

  if (const entities::Directional_Light_Entity* directional =
          entities::entity_as<entities::Directional_Light_Entity>(&entity))
  {
    scene_light_t light;
    light.kind = light_kind_t::Directional;
    light.mode = directional->light.mode;
    light.forward  = linalg::basis_from(directional->orientation).forward;
    light.radiance = radiance_of(directional->light);

    // The sun's softness is an ANGLE, so it converts to the one radius everything
    // downstream takes by being measured at unit distance -- which is exactly the
    // distance a directional arrival reports. Clamped below a right angle because
    // tan runs away there, and a half-angle at 90 degrees is not a sun, it is an
    // ambient term with a direction attached.
    const float angular_radius_degrees =
        std::clamp(directional->angular_diameter_degrees * 0.5f, 0.f, 89.f);
    light.source_radius = std::tan(linalg::to_radians(angular_radius_degrees));
    light.casts_shadows = directional->light.casts_shadows;
    return light;
  }

  return {};
}

shadow_projection_t spot_shadow_projection(const scene_light_t &light, uint32_t resolution)
{
  const float cos_outer  = std::clamp(light.cos_outer, std::cos(linalg::to_radians(85.f)),
                                      std::cos(linalg::to_radians(1.f)));
  const float half_angle = std::acos(cos_outer);

  const linalg::vec3 forward = linalg::normalize(light.forward);
  const linalg::vec3 up      = std::abs(forward.y) < 0.99f ? linalg::vec3{0.f, 1.f, 0.f}
                                                           : linalg::vec3{1.f, 0.f, 0.f};
  const float far_plane = std::max(light.range, SHADOW_NEAR_PLANE * 2.f);

  shadow_projection_t projection;
  projection.view_projection =
      linalg::perspective(half_angle * 2.f, 1.f, SHADOW_NEAR_PLANE, far_plane) *
      linalg::look_at(light.position, light.position + forward, up);
  projection.texel_size_at_unit_distance =
      2.f * std::tan(half_angle) / (float)std::max<uint32_t>(resolution, 1u);
  return projection;
}

void begin_frame_lights(frame_lights_t &frame, const lightmap_t &lightmap)
{
  frame.baked_count = (uint32_t)lightmap.light_uids.size();
  frame.entries.assign(frame.baked_count, scene_light_t{});

  // Zero radiance and not the default white: an unclaimed slot has to contribute
  // NOTHING, and radiance is the one field that guarantees it whatever the
  // shader does with the rest.
  for (scene_light_t &light : frame.entries) light.radiance = {0.f, 0.f, 0.f};
}

void add_frame_light(frame_lights_t &frame, const lightmap_t &lightmap,
                     entity_uid_t uid, const entities::Entity &entity)
{
  const std::optional<scene_light_t> gathered = try_light_of(entity);
  if (!gathered) return;

  scene_light_t light = *gathered;
  light.uid           = uid;

  // The resolve the bake's uid table exists for, and the one place it happens:
  // an entity is what the frame has and a slot is what the shader needs.
  light.baked_slot = find_baked_light_slot(lightmap, uid);

  const bool has_slot = light.baked_slot != LIGHTMAP_NO_LIGHT_SLOT &&
                        (uint32_t)light.baked_slot < frame.baked_count;

  // A light the bake DID see goes at its slot whatever its mode, because that
  // entry is what a chart's stored channel resolves to and the visibility beside
  // it is the shadow the runtime cannot compute.
  if (has_slot) frame.entries[(uint32_t)light.baked_slot] = light;

  // ...and the tail carries every ANALYTIC light, including the second copy of a
  // Mixed one. That copy is not redundancy: a surface with no chart has no slots
  // to read and sees only the tail, so this is how one light gets a baked shadow
  // on the wall and none on the player standing in front of it. A lightmapped
  // surface skips any tail entry carrying a slot, having already shaded it.
  //
  // A pure Baked light is deliberately NOT in the tail. It lights what the bake
  // measured and nothing else, which is exactly what it meant before it became
  // analytic -- lighting dynamic objects with it is lighting_def.md ss7's probes,
  // and not this.
  if (light_is_analytic(light.mode)) frame.entries.push_back(light);
}

} // namespace shared
