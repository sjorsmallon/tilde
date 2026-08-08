#include "crosshair.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace client::hud
{

void draw_crosshair(const crosshair_settings_t &settings)
{
  const ImVec2 display_size = ImGui::GetIO().DisplaySize;
  if (display_size.x <= 0.f || display_size.y <= 0.f) // minimized
    return;

  const float thickness  = std::max(settings.thickness, 1.f);
  const float arm_length = std::max(settings.arm_length, 0.f);
  const float gap        = std::max(settings.gap, 0.f);

  // Floor to whole pixels, then bias by half the thickness, so an odd
  // thickness lands on a pixel center instead of straddling two and blurring.
  const float center_x = std::floor(display_size.x * 0.5f);
  const float center_y = std::floor(display_size.y * 0.5f);
  const float half_thickness = thickness * 0.5f;

  // to_abgr is the renderer-boundary packing, and ImGui wants the same order.
  const ImU32 color = to_abgr(settings.color);

  // Filled rects rather than AddLine: axis-aligned bars land on exact pixel
  // boundaries, where an anti-aliased line of the same width does not.
  ImDrawList *draw_list = ImGui::GetForegroundDrawList();

  if (settings.draw_dot)
    draw_list->AddRectFilled({center_x - half_thickness, center_y - half_thickness},
                             {center_x + half_thickness, center_y + half_thickness},
                             color);

  if (arm_length <= 0.f)
    return;

  // left
  draw_list->AddRectFilled({center_x - gap - arm_length, center_y - half_thickness},
                           {center_x - gap,              center_y + half_thickness},
                           color);
  // right
  draw_list->AddRectFilled({center_x + gap,              center_y - half_thickness},
                           {center_x + gap + arm_length, center_y + half_thickness},
                           color);
  // top
  draw_list->AddRectFilled({center_x - half_thickness, center_y - gap - arm_length},
                           {center_x + half_thickness, center_y - gap},
                           color);
  // bottom
  draw_list->AddRectFilled({center_x - half_thickness, center_y + gap},
                           {center_x + half_thickness, center_y + gap + arm_length},
                           color);
}

} // namespace client::hud
