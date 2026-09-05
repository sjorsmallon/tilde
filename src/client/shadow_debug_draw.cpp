#include "shadow_debug_draw.hpp"

#include "../shared/array.hpp"
#include "../shared/color.hpp"

namespace client
{

namespace
{

// The same order direct_light.glsl's shadow_cascade_debug tints in.
constexpr Array<color_t, shared::MAX_SHADOW_CASCADES> CASCADE_COLORS = {
    {colors::red, colors::green, colors::blue, colors::yellow}};

// Eight corners as two quads -- first four one end, last four the other.
void draw_wire_hexahedron(renderer::debug_draw_list_t &debug, Span<const linalg::vec3> corners,
                          color_t color)
{
  for (uint32_t corner = 0; corner < 4; ++corner)
  {
    const uint32_t next = (corner + 1) % 4;
    debug.line(corners[corner], corners[next], color, 0.f, 0.f, true);
    debug.line(corners[corner + 4], corners[next + 4], color, 0.f, 0.f, true);
    debug.line(corners[corner], corners[corner + 4], color, 0.f, 0.f, true);
  }
}

} // namespace

void draw_shadow_cascades(renderer::debug_draw_list_t &debug, const shared::shadow_cascades_t &cascades)
{
  for (uint32_t index = 0; index < cascades.count; ++index)
  {
    const shared::shadow_cascade_t &cascade = cascades.cascades[index];
    const color_t                   color   = CASCADE_COLORS[index];

    draw_wire_hexahedron(debug, cascade.slice_corners, color);
    draw_wire_hexahedron(debug, cascade.box_corners, color);
    debug.wire_sphere(cascade.sphere_center, cascade.sphere_radius, color);
    debug.arrow(cascade.sphere_center,
                cascade.sphere_center - cascades.light_direction * cascade.sphere_radius, colors::white,
                0.f, true);
  }
}

void draw_point_shadow_faces(renderer::debug_draw_list_t &debug,
                             Span<const shared::point_shadow_faces_t> lights)
{
  for (const shared::point_shadow_faces_t &light : lights)
  {
    for (const shared::point_shadow_face_t &face : light.faces)
    {
      const color_t color = face.visible ? colors::white : colors::grey;
      for (uint32_t corner = 1; corner <= 4; ++corner)
      {
        const uint32_t next = corner == 4 ? 1 : corner + 1;
        debug.line(face.corners[0], face.corners[corner], color, 0.f, 0.f, true);
        debug.line(face.corners[corner], face.corners[next], color, 0.f, 0.f, true);
      }
    }
    debug.wire_sphere(light.position, light.range, colors::white);
  }
}

} // namespace client
