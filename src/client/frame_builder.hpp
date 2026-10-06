#pragma once

#include "entities/generated/entities/fog_volume_entity_generated.hpp"
#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace client
{

// A reveal cone and the light cast down it: the holder's r_flashlight_* cvars, or a Reveal_Light_Entity's own numbers.
struct lit_reveal_cone_t
{
  shared::reveal_cone_t cone;
  linalg::vec3          light_color     = {1.f, 1.f, 1.f};
  float                 light_intensity = 0.f;
};

// The visible half of a reveal cone: a spot light down the same cone, fading from `inner_fraction` of its half-angle (r_flashlight_inner).
// It casts no shadow, since a held one sits inside its holder's head.
[[nodiscard]] inline shared::scene_light_t build_spot_light_for_reveal_cone(const lit_reveal_cone_t& lit, float inner_fraction)
{
  entities::Light light{};
  light.color     = lit.light_color;
  light.intensity = lit.light_intensity;

  const float half_angle = std::acos(std::clamp(lit.cone.cosine_of_half_angle, -1.f, 1.f));

  shared::scene_light_t spot;
  spot.kind          = shared::light_kind_t::Spot;
  spot.mode          = entities::Light_Mode::Dynamic;
  spot.position      = lit.cone.apex;
  spot.forward       = lit.cone.axis;
  spot.radiance      = shared::compute_radiance(light, shared::light_kind_t::Spot);
  spot.range         = lit.cone.range;
  spot.cos_inner     = std::cos(half_angle * std::clamp(inner_fraction, 0.f, 0.99f));
  spot.cos_outer     = lit.cone.cosine_of_half_angle;
  spot.casts_shadows = false;
  return spot;
}

struct pass_builder_t
{
  renderer::render_view_t                              view;
  std::vector<renderer::mesh_draw_t>                   meshes;
  renderer::debug_draw_list_t                          debug;
  std::vector<renderer::particle_emitter_parameters_t> particles;
  // Two regions, filled together by shared::begin_frame_lights /
  // add_frame_light: the bake's slots first, the analytic tail after.
  shared::frame_lights_t                               lights;
  // Team wall impacts, copied from ctx.visuals each frame (team_wall_ripples.hpp).
  std::vector<shared::wall_ripple_t>                   ripples;
  std::vector<shared::reveal_cone_t>                   reveal_cones;
  // Where `reveal_cones` and their lights come from; the pass reads neither from here.
  std::vector<lit_reveal_cone_t>                       lit_reveal_cones;
  std::vector<shared::shadow_volume_t>                 shadow_volumes;
  std::vector<renderer::fog_volume_t>                  fog_volumes;
  std::vector<renderer::beam_t>                        beams;
  std::vector<renderer::custom_draw_t>                 custom;
  float                                                seconds = 0.0f;

  // The baked atlas every lightmapped draw in this pass samples. Set when the
  // map is loaded and left alone by begin_frame -- it belongs to the world, not
  // to the frame, which is why it is not one of the lists cleared above.
  renderer::lightmap_handle_t lightmap;

  // The sky, for the atlas's reason and with the atlas's lifetime: it belongs
  // to the world rather than to the frame, so begin_frame leaves it alone.
  renderer::skybox_handle_t sky;

  cvars::Debug_Channel debug_channel = cvars::Debug_Channel::off;

  color_t selected_outline_color = colors::white;
  color_t hovered_outline_color  = colors::yellow;

  // Which `meshes` each map object produced, so an overlay can outline it by uid.
  struct object_meshes_t
  {
    shared::entity_uid_t uid;
    uint32_t             first;
    uint32_t             count;
  };
  std::vector<object_meshes_t> object_meshes;

  void record_object_meshes(shared::entity_uid_t uid, size_t first)
  {
    if (meshes.size() > first)
      object_meshes.push_back({uid, (uint32_t)first, (uint32_t)(meshes.size() - first)});
  }

  [[nodiscard]] bool outline_object(shared::entity_uid_t uid, renderer::outline_t outline)
  {
    bool outlined = false;
    for (const object_meshes_t& range : object_meshes)
    {
      if (range.uid != uid)
        continue;
      for (uint32_t index = range.first; index < range.first + range.count; ++index)
        meshes[index].outline = outline;
      outlined = true;
    }
    return outlined;
  }

  // Once per frame, before anything is appended. Note what is NOT cleared:
  // `debug` is RETIRED instead, because entries appended with a lifetime are
  // meant to outlive the frame that made them -- a hitscan trace fires in a
  // fixed tick, not in a render frame, and clearing here would make it visible
  // for one frame, i.e. invisible.
  void begin_frame(float delta_seconds)
  {
    meshes.clear();
    object_meshes.clear();
    particles.clear();
    lights.entries.clear();
    lights.baked_count = 0;
    ripples.clear();
    reveal_cones.clear();
    lit_reveal_cones.clear();
    shadow_volumes.clear();
    fog_volumes.clear();
    beams.clear();
    custom.clear();
    debug.retire(delta_seconds);
    seconds += delta_seconds;
  }

  renderer::view_pass_t to_pass() const
  {
    renderer::view_pass_t pass;
    pass.view      = view;
    pass.draws     = meshes;
    pass.debug     = &debug;
    pass.lights            = lights.entries;
    pass.baked_light_count = lights.baked_count;
    pass.ripples           = ripples;
    pass.reveal_cones      = reveal_cones;
    pass.shadow_volumes    = shadow_volumes;
    pass.fog_volumes       = fog_volumes;
    pass.beams             = beams;
    pass.seconds          = seconds;
    pass.debug_channel = debug_channel;
    pass.particles = particles;
    pass.custom    = custom;
    pass.lightmap  = lightmap;
    pass.sky       = sky;
    pass.selected_outline_color = selected_outline_color;
    pass.hovered_outline_color  = hovered_outline_color;
    return pass;
  }
};

inline void add_fog_volume(pass_builder_t& pass, const entities::Fog_Volume_Entity& fog)
{
  if (!fog.switch_state.value || fog.density <= 0.0f)
    return;

  const linalg::vec3f center = fog.position + fog.volume.position;
  pass.fog_volumes.push_back({.minimum = center - fog.volume.half_extents,
                              .maximum = center + fog.volume.half_extents,
                              .color   = fog.color,
                              .density = fog.density,
                              .edge_softness = fog.edge_softness});
}

} // namespace client
