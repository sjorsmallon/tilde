#include "crosshair.hpp"

#include <algorithm>
#include <cmath>

namespace client::hud
{

void draw_crosshair(renderer::ui_draw_list_t &list, linalg::vec2 screen, float display_scale,
                    const crosshair_settings_t &settings)
{
  if (screen.x <= 0.f || screen.y <= 0.f) // degenerate (or minimized)
    return;

  const float thickness  = std::max(settings.thickness * display_scale, 1.f);
  const float arm_length = std::max(settings.arm_length * display_scale, 0.f);
  const float gap        = std::max(settings.gap * display_scale, 0.f);

  // align to whole pixels
  const float center_x = std::floor(screen.x * 0.5f);
  const float center_y = std::floor(screen.y * 0.5f);
  const float half_thickness = thickness * 0.5f;

  const color_t color = settings.color;

  // Axis-aligned filled rects rather than lines: they land on exact pixel
  // boundaries, where an anti-aliased line of the same width does not.
  if (settings.draw_dot)
    list.rect({center_x - half_thickness, center_y - half_thickness},
              {center_x + half_thickness, center_y + half_thickness}, color);

  if (arm_length <= 0.f) return;

  // left
  list.rect({center_x - gap - arm_length, center_y - half_thickness},
            {center_x - gap,              center_y + half_thickness}, color);
  // right
  list.rect({center_x + gap,              center_y - half_thickness},
            {center_x + gap + arm_length, center_y + half_thickness}, color);
  // top
  list.rect({center_x - half_thickness, center_y - gap - arm_length},
            {center_x + half_thickness, center_y - gap}, color);
  // bottom
  list.rect({center_x - half_thickness, center_y + gap},
            {center_x + half_thickness, center_y + gap + arm_length}, color);
}

} // namespace client::hud
