// The pin for collision_world_plan.md step 1: a swept sphere stops where a
// player's hull would, through the same disabled set and the same mover list.
#include "collision_detection.hpp"
#include "disabled_geometry.hpp"
#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "player_constants.hpp"
#include "map_geometry.hpp"
#include "movers.hpp"
#include "predicted_world.hpp"
#include "projectile_sweep.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using linalg::vec3f;

static int failure_count = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
}

static bool near(float a, float b, float tolerance = 1e-3f)
{
  return std::fabs(a - b) <= tolerance;
}

static bool near(const vec3f& a, const vec3f& b, float tolerance = 1e-3f)
{
  return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

// One box brush per entry, at consecutive geometry indices.
static Bounding_Volume_Hierarchy world_of(const std::vector<shared::geometry_value_t>& boxes)
{
  std::vector<BVH_Input> inputs;
  for (uint32_t index = 0; index < boxes.size(); ++index)
  {
    for (const shared::collision_piece_t& piece :
         shared::get_collision_pieces(boxes[index], index + 1))
    {
      BVH_Input input;
      input.aabb             = piece.bounds;
      input.id               = {Collision_Id::Type::Static_Geometry, index};
      input.collision_planes = piece.planes;
      input.face_polygons    = piece.face_polygons;
      inputs.push_back(std::move(input));
    }
  }
  return build_bvh(inputs);
}

// A floor whose top face is y = 0, spanning 2048 either way.
static Bounding_Volume_Hierarchy floor_world()
{
  return world_of({shared::make_box_brush({0.f, -64.f, 0.f}, {2048.f, 64.f, 2048.f})});
}

static constexpr shared::entity_uid_t platform_uid = 77;

static shared::mover_t platform_resting_at(const vec3f& center, const vec3f& half_extents)
{
  shared::mover_t mover;
  mover.uid                         = platform_uid;
  mover.pose_at_tick_start.position = center;
  mover.pose_at_tick_end.position   = center;
  mover.pieces =
      shared::get_collision_pieces(shared::make_box_brush(center, half_extents), platform_uid);
  mover.swept_bounds = mover.pieces.front().bounds;
  return mover;
}

int main()
{
  const Bounding_Volume_Hierarchy floor = floor_world();

  printf("a sphere stops at a face by its radius\n");
  {
    const std::optional<shared::projectile_hit_t> hit =
        shared::sweep_projectile(floor, {}, {0.f, 100.f, 0.f}, {0.f, -100.f, 0.f}, 8.f);
    check(hit.has_value(), "the sweep hits the floor");
    check(hit && near(hit->position, {0.f, 8.f, 0.f}),
          "the center stops one radius above the top face");
    check(hit && near(hit->t, 0.46f), "t is along the segment");
    check(hit && near(hit->normal, {0.f, 1.f, 0.f}), "the normal is the face entered");
    check(hit && hit->entity_uid == shared::null_entity_uid, "the map names no entity");
  }

  printf("a zero radius is the ray\n");
  {
    const std::optional<shared::projectile_hit_t> hit =
        shared::sweep_projectile(floor, {}, {0.f, 100.f, 0.f}, {0.f, -100.f, 0.f}, 0.f);
    check(hit && near(hit->t, 0.5f), "a zero-radius sweep stops on the face itself");
  }

  printf("a disabled brush is not there\n");
  {
    shared::disabled_geometry_t disabled(1, 1);
    shared::predicted_world_t   world{};
    world.disabled_geometry = disabled;
    const std::optional<shared::projectile_hit_t> hit =
        shared::sweep_projectile(floor, world, {0.f, 100.f, 0.f}, {0.f, -100.f, 0.f}, 8.f);
    check(!hit.has_value(), "the sweep passes through the switched-off floor");
  }

  printf("a sweep that reaches nothing answers nothing\n");
  {
    const std::optional<shared::projectile_hit_t> hit =
        shared::sweep_projectile(floor, {}, {0.f, 100.f, 0.f}, {500.f, 100.f, 0.f}, 8.f);
    check(!hit.has_value(), "a level sweep above the floor misses");

    const std::optional<shared::projectile_hit_t> short_hit =
        shared::sweep_projectile(floor, {}, {0.f, 100.f, 0.f}, {0.f, 50.f, 0.f}, 8.f);
    check(!short_hit.has_value(), "nothing past the segment's end is reported");

    const std::optional<shared::projectile_hit_t> still =
        shared::sweep_projectile(floor, {}, {0.f, 100.f, 0.f}, {0.f, 100.f, 0.f}, 8.f);
    check(!still.has_value(), "a zero-length sweep has no direction to enter along");
  }

  printf("an origin inside a solid is a hit at zero\n");
  {
    const std::optional<shared::projectile_hit_t> hit =
        shared::sweep_projectile(floor, {}, {0.f, -10.f, 0.f}, {0.f, -50.f, 0.f}, 8.f);
    check(hit.has_value(), "a sweep starting inside the floor stops");
    check(hit && hit->t == 0.f, "at t = 0");
    check(hit && near(hit->position, {0.f, -10.f, 0.f}), "where it started");
    check(hit && near(hit->normal, {0.f, 1.f, 0.f}),
          "with the normal of the face it would have entered through");
  }

  printf("the edge is conservative, the face beside it is exact\n");
  {
    // Down alongside the floor's +x side face, the center 4 units outside it: the
    // inflated corner catches it, since the sphere overlaps the edge region.
    const std::optional<shared::projectile_hit_t> grazing =
        shared::sweep_projectile(floor, {}, {2052.f, 100.f, 0.f}, {2052.f, -100.f, 0.f}, 8.f);
    check(grazing.has_value(), "a sweep within a radius of the side face is caught at the top");
    check(grazing && near(grazing->normal, {0.f, 1.f, 0.f}), "on the top face");

    const std::optional<shared::projectile_hit_t> clear =
        shared::sweep_projectile(floor, {}, {2060.f, 100.f, 0.f}, {2060.f, -100.f, 0.f}, 8.f);
    check(!clear.has_value(), "a sweep more than a radius beside the face passes");
  }

  printf("a mover stops the sweep at its end pose and names itself\n");
  {
    std::vector<shared::mover_t> movers = {
        platform_resting_at({0.f, 50.f, 0.f}, {32.f, 8.f, 32.f})};
    shared::predicted_world_t world{};
    world.movers = movers;

    const std::optional<shared::projectile_hit_t> hit =
        shared::sweep_projectile(floor, world, {0.f, 200.f, 0.f}, {0.f, 0.f, 0.f}, 8.f);
    check(hit.has_value(), "the sweep hits the platform");
    check(hit && near(hit->position, {0.f, 66.f, 0.f}),
          "one radius above the platform's top, not the floor");
    check(hit && hit->entity_uid == platform_uid, "the hit names the mover");

    // Beside the platform the floor answers: the mover was a nearer hit, not a replacement.
    const std::optional<shared::projectile_hit_t> beside =
        shared::sweep_projectile(floor, world, {100.f, 200.f, 0.f}, {100.f, 0.f, 0.f}, 8.f);
    check(beside && beside->entity_uid == shared::null_entity_uid && near(beside->position.y, 8.f),
          "beside the platform the floor stops the sweep");

    // The mover moved this tick: the sweep collides with where it ENDS.
    movers.front().pose_at_tick_start.position = {0.f, 0.f, 0.f};
    movers.front().swept_bounds.min.y          = -8.f;
    const std::optional<shared::projectile_hit_t> moved =
        shared::sweep_projectile(floor, world, {0.f, 200.f, 0.f}, {0.f, 0.f, 0.f}, 8.f);
    check(moved && near(moved->position.y, 66.f), "a mover is collided with at its end pose");
  }

  // The hitscan's world test IS this sweep at radius zero (contact_effect_plan.md D5), so a
  // raised platform stops a round and the round names the platform it stopped at.
  printf("a hitscan ray stops at a mover\n");
  {
    std::vector<shared::mover_t> movers = {
        platform_resting_at({0.f, 50.f, 0.f}, {32.f, 8.f, 32.f})};
    shared::predicted_world_t world{};
    world.movers = movers;

    const std::optional<shared::projectile_hit_t> ray =
        shared::sweep_projectile(floor, world, {0.f, 200.f, 0.f}, {0.f, 0.f, 0.f}, 0.f);
    check(ray.has_value(), "a ray from above the platform to the floor stops");
    check(ray && ray->entity_uid == platform_uid, "at the platform, which it names");
    check(ray && near(ray->position, {0.f, 58.f, 0.f}), "on the platform's top face itself");
    check(ray && near(ray->t, 142.f / 200.f), "t is where the face sits along the segment");

    const std::optional<shared::projectile_hit_t> without =
        shared::sweep_projectile(floor, {}, {0.f, 200.f, 0.f}, {0.f, 0.f, 0.f}, 0.f);
    check(without && without->entity_uid == shared::null_entity_uid && near(without->position.y, 0.f),
          "with the platform absent the same ray reaches the floor");
  }

  printf("two solids: the nearer wins whatever the primitive order\n");
  {
    const Bounding_Volume_Hierarchy two = world_of({
        shared::make_box_brush({0.f, -64.f, 0.f}, {2048.f, 64.f, 2048.f}),
        shared::make_box_brush({0.f, 40.f, 0.f}, {16.f, 8.f, 16.f}),
    });
    const std::optional<shared::projectile_hit_t> hit =
        shared::sweep_projectile(two, {}, {0.f, 200.f, 0.f}, {0.f, -100.f, 0.f}, 8.f);
    check(hit && near(hit->position.y, 56.f),
          "the block above the floor is what the sweep stops at");

    shared::disabled_geometry_t disabled = {0, 1};
    shared::predicted_world_t   world{};
    world.disabled_geometry = disabled;
    const std::optional<shared::projectile_hit_t> through =
        shared::sweep_projectile(two, world, {0.f, 200.f, 0.f}, {0.f, -100.f, 0.f}, 8.f);
    check(through && near(through->position.y, 8.f),
          "with the block switched off the floor does");
  }

  printf("a target is a box, and the shooter is not one\n");
  {
    shared::Entity_System system;
    const shared::entity_uid_t shooter_uid = system.spawn<entities::Player_Entity>();
    const shared::entity_uid_t victim_uid  = system.spawn<entities::Player_Entity>();
    entities::Player_Entity*   shooter     = system.get<entities::Player_Entity>(shooter_uid);
    entities::Player_Entity*   victim      = system.get<entities::Player_Entity>(victim_uid);
    shooter->position = {0.f, 0.f, 0.f};
    victim->position  = {200.f, 0.f, 0.f};
    shooter->health.current_health = 100;
    victim->health.current_health  = 100;

    std::vector<shared::projectile_target_t> targets;
    shared::collect_projectile_targets(system, targets);
    check(targets.size() == 2, "both living players are targets");

    // A hook fired from the shooter's own eye, through its own hull, toward the victim.
    const vec3f from{0.f, 60.f, 0.f};
    const vec3f to{400.f, 60.f, 0.f};
    const std::optional<shared::projectile_hit_t> hit =
        shared::sweep_sphere_against_targets(targets, from, to, 4.f, shooter_uid);
    check(hit.has_value(), "the hook lands");
    check(hit && hit->entity_uid == victim_uid, "on the other player, not the shooter");
    check(hit && near(hit->position.x, 200.f - shared::player_half_width - 4.f),
          "one radius short of the victim's hull face");
    check(hit && near(hit->normal, {-1.f, 0.f, 0.f}), "with the face's normal");

    victim->health.current_health = 0;
    shared::collect_projectile_targets(system, targets);
    check(targets.size() == 1, "a dead player is not a target");
  }

  printf("a crate in front of a wall is what the rocket stops at\n");
  {
    shared::Entity_System        system;
    const shared::entity_uid_t   crate_uid = system.spawn<entities::Physics_Body_Entity>();
    entities::Physics_Body_Entity* crate   = system.get<entities::Physics_Body_Entity>(crate_uid);
    crate->position = {100.f, 8.f, 0.f};
    crate->size     = {16.f, 16.f, 16.f};

    std::vector<shared::projectile_target_t> targets;
    shared::collect_projectile_targets(system, targets);
    check(targets.size() == 1, "a physics body is a target by its size");

    const Bounding_Volume_Hierarchy wall = world_of({
        shared::make_box_brush({200.f, 64.f, 0.f}, {8.f, 64.f, 64.f}),
    });
    const vec3f from{0.f, 8.f, 0.f};
    const vec3f to{300.f, 8.f, 0.f};
    std::optional<shared::projectile_hit_t> hit =
        shared::sweep_projectile(wall, {}, from, to, 12.f);
    check(hit && near(hit->position.x, 180.f), "alone, the wall stops the sweep");

    const std::optional<shared::projectile_hit_t> crate_hit =
        shared::sweep_sphere_against_targets(targets, from, to, 12.f, shared::null_entity_uid);
    check(crate_hit && crate_hit->t < hit->t && crate_hit->entity_uid == crate_uid,
          "the crate is the nearer hit and names itself");
    check(crate_hit && near(crate_hit->position.x, 100.f - 8.f - 12.f),
          "one radius short of the crate's face");
  }

  if (failure_count == 0)
    printf("projectile_sweep_test: all passed\n");
  else
    printf("projectile_sweep_test: %d FAILED\n", failure_count);
  return failure_count == 0 ? 0 : 1;
}
