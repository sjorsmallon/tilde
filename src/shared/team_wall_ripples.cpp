#include "team_wall_ripples.hpp"

#include "disabled_geometry.hpp"
#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "log.hpp"
#include "map_geometry.hpp"

#include <algorithm>
#include <optional>

namespace shared
{

namespace
{

bool bounds_contain(const aabb_bounds_t& bounds, const linalg::vec3f& point)
{
  return point.x >= bounds.min.x && point.x <= bounds.max.x && point.y >= bounds.min.y &&
         point.y <= bounds.max.y && point.z >= bounds.min.z && point.z <= bounds.max.z;
}

// The face the crosser came through: of the pieces holding its centre, the plane
// it is closest to. At the moment of entry the centre has only just passed one
// face, so that face is the nearest by a wide margin on any wall thicker than a
// frame's travel.
std::optional<Plane> try_entry_plane(const geometry_value_t& geometry, entity_uid_t uid,
                                     const linalg::vec3f& center)
{
  constexpr float INSIDE_TOLERANCE = 0.01f;

  std::optional<Plane> entry;
  float                entry_depth = 0.f;
  for (const collision_piece_t& piece : get_collision_pieces(geometry, uid))
  {
    bool  inside       = true;
    float shallowest   = -1e30f;
    const Plane* plane = nullptr;
    for (const Plane& candidate : piece.planes)
    {
      const float depth = linalg::dot(candidate.normal, center - candidate.point);
      if (depth > INSIDE_TOLERANCE)
      {
        inside = false;
        break;
      }
      if (depth > shallowest)
      {
        shallowest = depth;
        plane      = &candidate;
      }
    }
    if (!inside || plane == nullptr)
      continue;
    if (!entry || shallowest > entry_depth)
    {
      entry       = *plane;
      entry_depth = shallowest;
    }
  }
  return entry;
}

} // namespace

void detect_team_wall_crossings(const Entity_System& system, Span<const map_geometry_t> geometry,
                                Span<const entity_uid_t> owner_of,
                                Span<const wall_crosser_t> crossers, wall_ripple_state_t& state)
{
  std::vector<wall_ripple_state_t::inside_t> now;

  for (const wall_crosser_t& crosser : crossers)
  {
    for (uint32_t index = 0; index < geometry.size() && index < owner_of.size(); ++index)
    {
      if (owner_of[index] == null_entity_uid)
        continue;
      const entities::Geometry_Owner_Entity* owner =
          system.get<entities::Geometry_Owner_Entity>(owner_of[index]);
      if (owner == nullptr || !owner->switch_state.value ||
          geometry_owner_blocks(*owner, crosser.team))
        continue;

      const map_geometry_t& entry = geometry[index];
      if (!bounds_contain(get_bounds(entry.value), crosser.center))
        continue;

      now.push_back({crosser.uid, index});

      const bool was_inside =
          std::any_of(state.inside.begin(), state.inside.end(),
                      [&](const wall_ripple_state_t::inside_t& previous)
                      { return previous.crosser == crosser.uid && previous.geometry_index == index; });
      if (was_inside)
        continue;

      const std::optional<Plane> plane = try_entry_plane(entry.value, entry.uid, crosser.center);
      if (!plane)
      {
        // Inside the bound but inside no piece: the corner of a concave brush's
        // box. Not an entry, and the bound test says so again next frame.
        now.pop_back();
        continue;
      }

      const float depth = linalg::dot(plane->normal, crosser.center - plane->point);
      state.ripples.push_back({.center      = crosser.center - plane->normal * depth,
                               .normal      = plane->normal,
                               .age_seconds = 0.f});
    }
  }

  state.inside = std::move(now);
}

void age_wall_ripples(wall_ripple_state_t& state, float dt)
{
  for (wall_ripple_t& ripple : state.ripples)
    ripple.age_seconds += dt;
  std::erase_if(state.ripples, [](const wall_ripple_t& ripple)
                { return ripple.age_seconds > RIPPLE_MAX_AGE_SECONDS; });
}

} // namespace shared
