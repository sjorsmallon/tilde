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

void collect_lenses(const Entity_System& system, std::vector<lens_t>& out)
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

uint32_t compute_held_lens_channels(const Entity_System& system, const entities::Player_Entity& player,
                                    bool held_lens_on)
{
  if (!held_lens_on || !player_watches_lenses(player))
    return 0;
  const std::optional<entities::Lens_Filter> filter = try_get_held_lens_filter(system, player);
  return filter.has_value() ? get_channel_bits_for_lens_filter(*filter) : 0;
}

lens_watcher_t compute_lens_watcher(const linalg::vec3f& eye, float yaw_degrees, float pitch_degrees,
                                    uint32_t held_channels)
{
  return {.eye           = eye,
          .forward       = linalg::direction_from_angles(yaw_degrees, pitch_degrees),
          .held_channels = held_channels};
}

bool lens_watcher_faces_point(const lens_watcher_t& watcher, const linalg::vec3f& point)
{
  const linalg::vec3f to_point = point - watcher.eye;
  const float         reach    = linalg::length(to_point);
  if (reach <= 0.f)
    return true;
  return linalg::dot(to_point, watcher.forward) >=
         std::cos(linalg::to_radians(LENS_WATCH_HALF_ANGLE_DEGREES)) * reach;
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

bool player_watches_lenses(const entities::Player_Entity& player)
{
  return player.health.current_health > 0;
}

void collect_lens_watchers(const Entity_System& system, entity_uid_t predicted_by_caller,
                           std::vector<lens_watcher_t>& out)
{
  out.clear();
  for (const entities::Player_Entity& player : system.entities_of_type<entities::Player_Entity>())
  {
    if (player.entity_id == predicted_by_caller || !player_watches_lenses(player))
      continue;
    out.push_back(compute_lens_watcher(player.position + linalg::vec3f{0.f, player_eye_height, 0.f},
                                       player.view_angle_yaw, player.view_angle_pitch,
                                       compute_held_lens_channels(system, player, player.held_lens_on)));
  }
}

} // namespace shared
