#pragma once
#include <cmath>

namespace editor
{

// Major grid (always drawn)
constexpr float MAJOR_GRID_STEP = 128.f;
constexpr int MAJOR_GRID_COUNT = 50;

// Defaults
constexpr float MIN_EXTENT = 1.f;
constexpr float ROTATION_SNAP = 15.f;
constexpr float DEFAULT_HALF_EXTENT = 64.f; // 128-unit cube (one grid cell)

// Default floor (surface at y=0, center at -half_h)
constexpr float DEFAULT_FLOOR_Y = -64.f;
constexpr float DEFAULT_FLOOR_HALF_H = 64.f;
constexpr float DEFAULT_FLOOR_HALF_W = 640.f; // 5 grid cells

// Grid hover indicator (selection tool)
constexpr float GRID_INDICATOR_HALF_W = 64.f;
constexpr float GRID_INDICATOR_HALF_H = 1.f;

struct grid_settings_t
{
  static constexpr float levels[] = {1, 2, 4, 8, 16, 32, 64, 128};
  static constexpr int num_levels = 8;
  int level_index = 7; // default = 128

  float step() const { return levels[level_index]; }
  void increase()
  {
    if (level_index < num_levels - 1)
      level_index++;
  }
  void decrease()
  {
    if (level_index > 0)
      level_index--;
  }
};

inline float snap(float v, float interval)
{
  return std::round(v / interval) * interval;
}

} // namespace editor
