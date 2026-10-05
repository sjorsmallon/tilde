#include "reveal_light.hpp"
#include "cvars/generated/cvars_generated.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities/reveal_light_entity_generated.hpp"
#include "entities/generated/entities/weapon_entity_generated.hpp"

#include "entity_system.hpp"
#include "player_constants.hpp"
#include "weapons.hpp"

namespace shared
{

reveal_cone_settings_t reveal_cone_settings_from_cvars(const cvars::cvar_state_t& cvars)
{
  return {.range              = cvars.sv_reveal_light_range,
          .half_angle_degrees = cvars.sv_reveal_light_half_angle,
          .overhead_height    = cvars.sv_reveal_light_overhead_height};
}

std::optional<entities::Reveal_Cone_Kind> try_reveal_light_in_hand(const Entity_System&           system,
                                                                    const entities::Player_Entity& player)
{
  if (player.health.current_health <= 0)
    return std::nullopt;

  const uint32_t* weapon_uid = player.inventory.weapons.try_get(player.inventory.active_slot);
  if (weapon_uid == nullptr || *weapon_uid == null_entity_uid)
    return std::nullopt;

  const entities::Weapon_Entity* held = system.get<entities::Weapon_Entity>(*weapon_uid);
  if (held == nullptr || static_cast<uint32_t>(held->weapon_id) >= WEAPON_DEFINITIONS.size())
    return std::nullopt;

  const entities::Fire_Resolution primary = get_weapon_definition(held->weapon_id).primary_fire.resolution;
  if (primary == entities::Fire_Resolution::Reveal_Light)
    return entities::Reveal_Cone_Kind::Reveals;
  if (primary == entities::Fire_Resolution::Erase_Light)
    return entities::Reveal_Cone_Kind::Erases;
  return std::nullopt;
}

bool reveal_light_is_in_hand(const Entity_System& system, const entities::Player_Entity& player)
{
  return try_reveal_light_in_hand(system, player).has_value();
}

bool reveal_light_is_on(const Entity_System& system, const entities::Player_Entity& player)
{
  return player.reveal_light_on && reveal_light_is_in_hand(system, player);
}

path_pose_t reveal_light_pose_at(const Entity_System& system, const path_links_t& links,
                                 const mover_rests_t& rests, const entities::Reveal_Light_Entity& light,
                                 uint32_t tick, float tickrate)
{
  const path_pose_t placed = {.position = light.position, .orientation = light.orientation};
  const entities::Mover_Entity* mover = system.get<entities::Mover_Entity>(light.follows);
  if (mover == nullptr)
    return placed;

  const auto        found = rests.find(mover->entity_id);
  const path_pose_t rest  = found != rests.end() ? found->second.frame : mover_rest_frame(system, *mover);
  return carry_pose_by_mover(rest, mover_pose_at(system, links, *mover, rest, tick, tickrate), placed);
}

reveal_cone_t compute_light_reveal_cone(const entities::Reveal_Light_Entity& light, const path_pose_t& pose)
{
  return {.apex                 = pose.position,
          .axis                 = linalg::forward(pose.orientation),
          .range                = light.range,
          .cosine_of_half_angle = cosine_of_reveal_half_angle({.half_angle_degrees = light.half_angle_degrees}),
          .kind                 = light.kind};
}

reveal_cone_planes_t planes_of_reveal_cone(const reveal_cone_t& cone)
{
  const linalg::vec3f axis   = linalg::normalize(cone.axis);
  const linalg::vec3f helper = std::fabs(axis.y) < 0.9f ? linalg::vec3f{0.f, 1.f, 0.f}
                                                        : linalg::vec3f{1.f, 0.f, 0.f};
  const linalg::vec3f right  = linalg::normalize(linalg::cross(axis, helper));
  const linalg::vec3f up     = linalg::cross(right, axis);

  const float cosine = cone.cosine_of_half_angle;
  const float sine   = std::sqrt(std::max(0.f, 1.f - cosine * cosine));

  reveal_cone_planes_t planes{.apex = cone.apex, .range = cone.range};
  planes.axis                 = axis;
  planes.cosine_of_half_angle = cosine;
  planes.kind                 = cone.kind;
  for (uint32_t side = 0; side < REVEAL_CONE_SIDE_COUNT; ++side)
  {
    const float around = 2.f * linalg::PI * static_cast<float>(side) /
                         static_cast<float>(REVEAL_CONE_SIDE_COUNT);
    const linalg::vec3f away_from_axis = right * std::cos(around) + up * std::sin(around);
    planes.sides[side] = {.point = cone.apex, .normal = away_from_axis * cosine - axis * sine};
  }
  return planes;
}

bool reveal_cone_touches_box(const reveal_cone_planes_t& cone, const aabb_bounds_t& box)
{
  const linalg::vec3f nearest = {std::clamp(cone.apex.x, box.min.x, box.max.x),
                                 std::clamp(cone.apex.y, box.min.y, box.max.y),
                                 std::clamp(cone.apex.z, box.min.z, box.max.z)};
  if (linalg::length(nearest - cone.apex) > cone.range)
    return false;

  const linalg::vec3f center       = get_aabb_center(box);
  const linalg::vec3f half_extents = (box.max - box.min) * 0.5f;
  for (const Plane& side : cone.sides)
  {
    const float reach = half_extents.x * std::fabs(side.normal.x) +
                        half_extents.y * std::fabs(side.normal.y) +
                        half_extents.z * std::fabs(side.normal.z);
    if (linalg::dot(center - side.point, side.normal) > reach)
      return false;
  }
  return true;
}

bool any_reveal_cone_touches_box(Span<const reveal_cone_planes_t> cones, const aabb_bounds_t& box)
{
  for (const reveal_cone_planes_t& cone : cones)
    if (cone.kind == entities::Reveal_Cone_Kind::Reveals && reveal_cone_touches_box(cone, box))
      return true;
  return false;
}

bool reveal_cone_contains_box(const reveal_cone_planes_t& cone, const aabb_bounds_t& box)
{
  for (uint32_t corner_index = 0; corner_index < 8; ++corner_index)
  {
    const linalg::vec3f corner = {(corner_index & 1) != 0 ? box.max.x : box.min.x,
                                  (corner_index & 2) != 0 ? box.max.y : box.min.y,
                                  (corner_index & 4) != 0 ? box.max.z : box.min.z};
    const linalg::vec3f to_corner = corner - cone.apex;
    const float         reach     = linalg::length(to_corner);
    if (reach > cone.range || linalg::dot(to_corner, cone.axis) < cone.cosine_of_half_angle * reach)
      return false;
  }
  return true;
}

bool any_erase_cone_contains_box(Span<const reveal_cone_planes_t> cones, const aabb_bounds_t& box)
{
  for (const reveal_cone_planes_t& cone : cones)
    if (cone.kind == entities::Reveal_Cone_Kind::Erases && reveal_cone_contains_box(cone, box))
      return true;
  return false;
}

void collect_reveal_cones(const Entity_System& system, const path_links_t& links,
                          const mover_rests_t& rests, const reveal_cone_settings_t& settings,
                          uint32_t tick, float tickrate,
                          entity_uid_t predicted_by_caller, std::vector<reveal_cone_planes_t>& out)
{
  out.clear();
  for (const entities::Player_Entity& player : system.entities_of_type<entities::Player_Entity>())
  {
    const std::optional<entities::Reveal_Cone_Kind> kind = try_reveal_light_in_hand(system, player);
    if (player.entity_id == predicted_by_caller || !player.reveal_light_on || !kind)
      continue;
    out.push_back(planes_of_reveal_cone(
        compute_player_reveal_cone(player.position + linalg::vec3f{0.f, player_eye_height, 0.f},
                       player.view_angle_yaw, player.view_angle_pitch,
                       player.reveal_light_overhead, *kind, settings)));
  }

  for (const entities::Reveal_Light_Entity& light : system.entities_of_type<entities::Reveal_Light_Entity>())
    if (light.switch_state.value)
      out.push_back(planes_of_reveal_cone(
          compute_light_reveal_cone(light, reveal_light_pose_at(system, links, rests, light, tick, tickrate))));
}

} // namespace shared
