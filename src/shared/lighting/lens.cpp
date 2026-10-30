#include "lens.hpp"

#include "entities/generated/entities/lens_entity_generated.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entity_system.hpp"
#include "movement/player_constants.hpp"
#include "weapon_instance.hpp"

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
                                              const linalg::vec3f& point, uint32_t held_channels)
{
  uint32_t channels = held_channels != 0 ? held_channels : LENS_CHANNEL_ALL;
  bool     seen     = held_channels != 0;
  for (const lens_t& lens : lenses)
  {
    if (!lens_lies_on_line_of_sight(lens, eye, point))
      continue;
    channels &= get_channel_bits_for_lens_filter(lens.filter);
    seen = true;
  }
  return seen ? channels : 0;
}

void compute_lenses(const Entity_System& system, std::vector<lens_t>& out)
{
  out.clear();
  for (const entities::Lens_Entity& lens : system.entities_of_type<entities::Lens_Entity>())
    if (lens.switch_state.value)
      out.push_back(compute_lens_box(lens));
}

std::optional<entities::Lens_Filter> try_get_held_lens_filter(const Entity_System&           system,
                                                              const entities::Player_Entity& player)
{
  const weapon_definition_t* held = try_find_held_weapon_definition(system, player);
  if (held == nullptr || held->primary_fire.resolution != entities::Fire_Resolution::Lens_View)
    return std::nullopt;
  return held->primary_fire.lens_view.filter;
}

lens_watch_screen_t clamp_lens_watch_screen(const lens_watch_screen_t& view)
{
  return {.fov_degrees = std::clamp(view.fov_degrees, LENS_WATCH_SCREEN_FOV_DEGREES_MIN, LENS_WATCH_SCREEN_FOV_DEGREES_MAX),
          .aspect      = std::clamp(view.aspect, LENS_WATCH_SCREEN_ASPECT_MIN, LENS_WATCH_SCREEN_ASPECT_MAX)};
}

lens_watcher_t compute_lens_watcher(const linalg::vec3f& eye, float yaw_degrees, float pitch_degrees,
                                    const lens_watch_screen_t& view, uint32_t held_channels)
{
  const linalg::vec3f forward = linalg::direction_from_angles(yaw_degrees, pitch_degrees);
  const linalg::vec3f right   = linalg::right_from_forward(forward);
  const linalg::vec3f up      = linalg::cross(right, forward);

  const lens_watch_screen_t bounded    = clamp_lens_watch_screen(view);
  const float       vertical   = linalg::to_radians(bounded.fov_degrees) * 0.5f;
  const float       horizontal = std::atan(std::tan(vertical) * bounded.aspect);
  const float sine_h = std::sin(horizontal), cosine_h = std::cos(horizontal);
  const float sine_v = std::sin(vertical), cosine_v = std::cos(vertical);

  lens_watcher_t watcher = {.eye = eye, .held_channels = held_channels};
  watcher.sides[0]       = {.point = eye, .normal = right * -cosine_h - forward * sine_h};
  watcher.sides[1]       = {.point = eye, .normal = right * cosine_h - forward * sine_h};
  watcher.sides[2]       = {.point = eye, .normal = up * -cosine_v - forward * sine_v};
  watcher.sides[3]       = {.point = eye, .normal = up * cosine_v - forward * sine_v};
  return watcher;
}

bool lens_watcher_faces_point(const lens_watcher_t& watcher, const linalg::vec3f& point)
{
  for (const Plane& side : watcher.sides)
    if (linalg::dot(side.normal, point - side.point) > 0.f)
      return false;
  return true;
}

bool any_lens_watcher_sees_point(Span<const lens_watcher_t> watchers, Span<const lens_t> lenses,
                                 entities::Lens_Filter filter, const linalg::vec3f& point)
{
  const uint32_t wanted = get_channel_bits_for_lens_filter(filter);
  for (const lens_watcher_t& watcher : watchers)
  {
    if (!lens_watcher_faces_point(watcher, point))
      continue;
    if ((compute_channels_seen_through_lenses(lenses, watcher.eye, point, watcher.held_channels) & wanted) != 0)
      return true;
  }
  return false;
}

void compute_lens_watchers(Span<const player_sight_t> sights, std::vector<lens_watcher_t>& out)
{
  out.clear();
  for (const player_sight_t& sight : sights)
  {
    if (!sight.alive)
      continue;
    out.push_back(compute_lens_watcher(player_eye_position(sight.feet), sight.view_entering_tick.yaw,
                                       sight.view_entering_tick.pitch, sight.screen, held_lens_channels(sight)));
  }
}

} // namespace shared
