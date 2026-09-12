#pragma once

#include "renderer.hpp"

#include <vector>

namespace client
{

struct pass_builder_t
{
  renderer::render_view_t                              view;
  std::vector<renderer::mesh_draw_t>                   meshes;
  renderer::debug_draw_list_t                          debug;
  std::vector<renderer::particle_emitter_parameters_t> particles;
  // Two regions, filled together by shared::begin_frame_lights /
  // add_frame_light: the bake's slots first, the analytic tail after.
  shared::frame_lights_t                               lights;
  std::vector<renderer::custom_draw_t>                 custom;

  // The baked atlas every lightmapped draw in this pass samples. Set when the
  // map is loaded and left alone by begin_frame -- it belongs to the world, not
  // to the frame, which is why it is not one of the lists cleared above.
  renderer::lightmap_handle_t lightmap;

  // The sky, for the atlas's reason and with the atlas's lifetime: it belongs
  // to the world rather than to the frame, so begin_frame leaves it alone.
  renderer::skybox_handle_t sky;

  cvars::Debug_Channel debug_channel = cvars::Debug_Channel::off;

  // Once per frame, before anything is appended. Note what is NOT cleared:
  // `debug` is RETIRED instead, because entries appended with a lifetime are
  // meant to outlive the frame that made them -- a hitscan trace fires in a
  // fixed tick, not in a render frame, and clearing here would make it visible
  // for one frame, i.e. invisible.
  void begin_frame(float delta_seconds)
  {
    meshes.clear();
    particles.clear();
    lights.entries.clear();
    lights.baked_count = 0;
    custom.clear();
    debug.retire(delta_seconds);
  }

  renderer::view_pass_t to_pass() const
  {
    renderer::view_pass_t pass;
    pass.view      = view;
    pass.draws     = meshes;
    pass.debug     = &debug;
    pass.lights            = lights.entries;
    pass.baked_light_count = lights.baked_count;
    pass.debug_channel = debug_channel;
    pass.particles = particles;
    pass.custom    = custom;
    pass.lightmap  = lightmap;
    pass.sky       = sky;
    return pass;
  }
};

} // namespace client
