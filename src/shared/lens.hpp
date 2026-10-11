#pragma once

// A lens: a window the world is seen through. Geometry whose owner is `revealed_by_lens` is drawn only
// where the line from the eye to it passes through a switched-on lens that passes the owner's colour
// (resources/shaders/reveal.glsl's lens_margin); the filters of every lens on one line multiply, so a red
// and a green stacked pass nothing. The geometry is solid whether or not it is seen.

#include "entities/generated/entities_core_generated.hpp"
#include "linalg.hpp"
#include "span.hpp"

#include <cstdint>
#include <vector>

namespace entities
{
struct Lens_Entity;
}

namespace shared
{

struct Entity_System;

struct lens_t
{
  linalg::vec3f         center       = {0.f, 0.f, 0.f};
  linalg::vec3f         half_extents = {0.f, 0.f, 0.f};
  entities::Lens_Filter filter       = entities::Lens_Filter::Red;
};

inline constexpr uint32_t LENS_CHANNEL_RED   = 1u << 0;
inline constexpr uint32_t LENS_CHANNEL_GREEN = 1u << 1;
inline constexpr uint32_t LENS_CHANNEL_BLUE  = 1u << 2;
inline constexpr uint32_t LENS_CHANNEL_ALL   = LENS_CHANNEL_RED | LENS_CHANNEL_GREEN | LENS_CHANNEL_BLUE;

[[nodiscard]] uint32_t      get_channel_bits_for_lens_filter(entities::Lens_Filter filter);
[[nodiscard]] linalg::vec3f get_color_for_lens_filter(entities::Lens_Filter filter);

[[nodiscard]] lens_t compute_lens_box(const entities::Lens_Entity& lens);

// The segment from `eye` to `point` enters the lens and leaves it before reaching `point`: an eye inside
// the lens looks through it, a point inside the lens or in front of it is not seen through it.
[[nodiscard]] bool lens_lies_on_line_of_sight(const lens_t& lens, const linalg::vec3f& eye,
                                              const linalg::vec3f& point);

// The channels `point` is seen in from `eye`: the product of every lens on the line, 0 when none is on it.
[[nodiscard]] uint32_t compute_channels_seen_through_lenses(Span<const lens_t> lenses, const linalg::vec3f& eye,
                                                            const linalg::vec3f& point);

// Every switched-on Lens_Entity's box.
void collect_lenses(const Entity_System& system, std::vector<lens_t>& out);

} // namespace shared
