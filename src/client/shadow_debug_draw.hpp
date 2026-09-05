#pragma once

// r_shadow_freeze's picture (lighting_def.md gate 9): with the sun's cascade fit
// frozen at one camera, fly out and see each frustum slice and the ortho box
// fit to it as wireframe in that cascade's tint colour -- the same four the
// shadow_cascades debug channel washes the shaded result with -- plus the sun
// direction out of each slice's sphere centre. Unfrozen, the boxes always
// enclose the view and there is nothing to see. A point light's six face
// frusta draw beside them, white where the face was rendered and grey where
// the frozen camera could not see it and the face was culled.

#include "renderer.hpp"

namespace client
{

void draw_shadow_cascades(renderer::debug_draw_list_t &debug, const shared::shadow_cascades_t &cascades);
void draw_point_shadow_faces(renderer::debug_draw_list_t &debug,
                             Span<const shared::point_shadow_faces_t> lights);

} // namespace client
