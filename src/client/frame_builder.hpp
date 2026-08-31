#pragma once

// The mutable counterpart of renderer::view_pass_t.
//
// A pass is a VALUE the renderer reads: spans of draws and emitters, and a
// pointer to a debug list. Somebody has to own that storage and keep it alive
// across the render_frame call, and it should be the same somebody every frame
// so vector capacity stays warm. That owner is this.
//
// States hold one per camera; editor tools and shared draw helpers take a
// `pass_builder_t&`, which is the slot the deleted overlay_renderer_t used to
// occupy -- minus the virtuals, and minus the get_command_buffer() hole that
// leaked the very type the interface existed to hide.

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
  std::vector<renderer::custom_draw_t>                 custom;

  // The baked atlas every lightmapped draw in this pass samples. Set when the
  // map is loaded and left alone by begin_frame -- it belongs to the world, not
  // to the frame, which is why it is not one of the lists cleared above.
  renderer::lightmap_handle_t lightmap;

  // Once per frame, before anything is appended. Note what is NOT cleared:
  // `debug` is RETIRED instead, because entries appended with a lifetime are
  // meant to outlive the frame that made them -- a hitscan trace fires in a
  // fixed tick, not in a render frame, and clearing here would make it visible
  // for one frame, i.e. invisible.
  void begin_frame(float delta_seconds)
  {
    meshes.clear();
    particles.clear();
    custom.clear();
    debug.retire(delta_seconds);
  }

  renderer::view_pass_t to_pass() const
  {
    renderer::view_pass_t pass;
    pass.view      = view;
    pass.draws     = meshes;
    pass.debug     = &debug;
    pass.particles = particles;
    pass.custom    = custom;
    pass.lightmap  = lightmap;
    return pass;
  }
};

} // namespace client
