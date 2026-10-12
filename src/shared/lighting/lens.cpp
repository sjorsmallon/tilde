#include "lens.hpp"

#include "entities/generated/entities/lens_entity_generated.hpp"
#include "entity_system.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace shared
{

uint32_t get_channel_bits_for_lens_filter(entities::Lens_Filter filter)
{
  switch (filter)
  {
  case entities::Lens_Filter::Red: return LENS_CHANNEL_RED;
  case entities::Lens_Filter::Green: return LENS_CHANNEL_GREEN;
  case entities::Lens_Filter::Blue: return LENS_CHANNEL_BLUE;
  }
  return 0;
}

linalg::vec3f get_color_for_lens_filter(entities::Lens_Filter filter)
{
  switch (filter)
  {
  case entities::Lens_Filter::Red: return {1.f, 0.15f, 0.15f};
  case entities::Lens_Filter::Green: return {0.15f, 1.f, 0.15f};
  case entities::Lens_Filter::Blue: return {0.2f, 0.35f, 1.f};
  }
  return {1.f, 1.f, 1.f};
}

lens_t compute_lens_box(const entities::Lens_Entity& lens)
{
  return {.box    = {.center       = lens.position + lens.volume.position,
                     .half_extents = lens.volume.half_extents,
                     .orientation  = lens.orientation},
          .filter = lens.filter};
}

bool lens_lies_on_line_of_sight(const lens_t& lens, const linalg::vec3f& eye, const linalg::vec3f& point)
{
  constexpr float PARALLEL_EPSILON = 1e-6f;

  const linalg::quatf into_box   = linalg::conjugate(lens.box.orientation);
  const linalg::vec3f origin_box = linalg::rotate(into_box, eye - lens.box.center);
  const linalg::vec3f line       = linalg::rotate(into_box, point - eye);
  float               enter      = 0.f;
  float               exit       = std::numeric_limits<float>::infinity();
  for (int axis = 0; axis < 3; ++axis)
  {
    const float origin    = origin_box[axis];
    const float direction = line[axis];
    const float half      = lens.box.half_extents[axis];
    if (std::fabs(direction) < PARALLEL_EPSILON)
    {
      if (std::fabs(origin) > half)
        return false;
      continue;
    }
    float low  = (-half - origin) / direction;
    float high = (half - origin) / direction;
    if (low > high)
      std::swap(low, high);
    enter = std::max(enter, low);
    exit  = std::min(exit, high);
  }
  return enter < exit && exit < 1.f;
}

uint32_t compute_channels_seen_through_lenses(Span<const lens_t> lenses, const linalg::vec3f& eye,
                                              const linalg::vec3f& point)
{
  uint32_t channels = LENS_CHANNEL_ALL;
  bool     seen     = false;
  for (const lens_t& lens : lenses)
  {
    if (!lens_lies_on_line_of_sight(lens, eye, point))
      continue;
    channels &= get_channel_bits_for_lens_filter(lens.filter);
    seen = true;
  }
  return seen ? channels : 0;
}

void collect_lenses(const Entity_System& system, std::vector<lens_t>& out)
{
  out.clear();
  for (const entities::Lens_Entity& lens : system.entities_of_type<entities::Lens_Entity>())
    if (lens.switch_state.value)
      out.push_back(compute_lens_box(lens));
}

} // namespace shared
