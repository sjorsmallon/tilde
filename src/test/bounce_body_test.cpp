// The pin for collision_world_plan.md step 5: a bounce body falls, returns by
// its restitution, slides to rest by its friction, passes a switched-off brush,
// lands on a mover, and wakes when written to.
#include "bounce_body.hpp"
#include "collision_detection.hpp"
#include "disabled_geometry.hpp"
#include "map_geometry.hpp"
#include "movers.hpp"
#include "predicted_world.hpp"

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

static bool near(float a, float b, float tolerance)
{
  return std::fabs(a - b) <= tolerance;
}

static constexpr float TICK    = 1.f / 60.f;
static constexpr float GRAVITY = 800.f;
static constexpr float RADIUS  = 8.f;

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

// The floor's top is y = 0; a second floor's top is y = -300 below it.
static Bounding_Volume_Hierarchy two_floors()
{
  return world_of({shared::make_box_brush({0.f, -64.f, 0.f}, {2048.f, 64.f, 2048.f}),
                   shared::make_box_brush({0.f, -364.f, 0.f}, {2048.f, 64.f, 2048.f})});
}

static shared::bounce_body_t dropped_from(float height, float restitution, float friction)
{
  shared::bounce_body_t body;
  body.position           = {0.f, height, 0.f};
  body.bounce.restitution = restitution;
  body.bounce.friction    = friction;
  return body;
}

// Runs until rest or the tick budget; reports the highest point after the first contact.
struct run_t
{
  shared::bounce_body_t body;
  int                   ticks_run       = 0;
  float                 apex_after_first_contact = -1.f;
};

static run_t run(const Bounding_Volume_Hierarchy& bvh, const shared::predicted_world_t& world,
                 shared::bounce_body_t body, int max_ticks)
{
  run_t result{.body = body};
  bool  contacted = false;
  float previous_y = body.position.y;
  for (int tick = 0; tick < max_ticks && !result.body.bounce.at_rest; ++tick)
  {
    result.body = shared::bounce_step(bvh, world, result.body, RADIUS, GRAVITY, TICK);
    ++result.ticks_run;
    if (!contacted && result.body.bounce.velocity.y > 0.f)
      contacted = true;
    if (contacted)
      result.apex_after_first_contact = std::max(result.apex_after_first_contact, result.body.position.y);
    previous_y = result.body.position.y;
  }
  (void)previous_y;
  return result;
}

int main()
{
  const Bounding_Volume_Hierarchy floors = two_floors();

  printf("a dropped body returns by its restitution and comes to rest on the floor\n");
  {
    const run_t result = run(floors, {}, dropped_from(100.f, 0.5f, 4.f), 1200);
    check(result.body.bounce.at_rest, "the body comes to rest");
    check(near(result.body.position.y, RADIUS, 0.5f), "one radius above the floor");
    check(result.body.bounce.velocity.x == 0.f && result.body.bounce.velocity.y == 0.f,
          "with no velocity left");
    // Energy scales with restitution squared: a quarter of the 92-unit fall above rest.
    check(near(result.apex_after_first_contact, RADIUS + 0.25f * 92.f, 6.f),
          "the first return reaches a quarter of the drop");
  }

  printf("a body with no restitution stops on the first contact\n");
  {
    const run_t result = run(floors, {}, dropped_from(100.f, 0.f, 4.f), 1200);
    check(result.body.bounce.at_rest && result.apex_after_first_contact < RADIUS + 1.f,
          "no return, at rest at once");
  }

  printf("a body thrown along the floor slides and stops by its friction\n");
  {
    shared::bounce_body_t body = dropped_from(RADIUS, 0.3f, 4.f);
    shared::wake_bounce_body(body.bounce, {400.f, 0.f, 0.f});
    const run_t result = run(floors, {}, body, 1200);
    check(result.body.bounce.at_rest, "the slide ends");
    check(result.body.position.x > 20.f && result.body.position.x < 400.f,
          "somewhere along the floor, not where it started and not forever");
    check(near(result.body.position.y, RADIUS, 0.5f), "still on the floor");

    shared::bounce_body_t frictionless = dropped_from(RADIUS, 0.3f, 0.f);
    shared::wake_bounce_body(frictionless.bounce, {400.f, 0.f, 0.f});
    const run_t sliding = run(floors, {}, frictionless, 120);
    check(!sliding.body.bounce.at_rest && sliding.body.position.x > result.body.position.x,
          "without friction it keeps going");
  }

  printf("a switched-off brush is not there for a body either\n");
  {
    shared::disabled_geometry_t disabled = {1, 0};
    shared::predicted_world_t   world{};
    world.disabled_geometry = disabled;
    const run_t result = run(floors, world, dropped_from(100.f, 0.3f, 4.f), 1200);
    check(result.body.bounce.at_rest, "the body comes to rest");
    check(near(result.body.position.y, -300.f + RADIUS, 0.5f),
          "on the lower floor, through the switched-off one");
  }

  printf("a body lands on a mover at its end pose\n");
  {
    shared::mover_t platform;
    platform.uid                         = 5;
    platform.pose_at_tick_start.position = {0.f, 50.f, 0.f};
    platform.pose_at_tick_end.position   = {0.f, 50.f, 0.f};
    platform.pieces = shared::get_collision_pieces(
        shared::make_box_brush({0.f, 50.f, 0.f}, {32.f, 8.f, 32.f}), platform.uid);
    platform.swept_bounds = platform.pieces.front().bounds;
    std::vector<shared::mover_t> movers = {platform};
    shared::predicted_world_t    world{};
    world.movers = movers;

    const run_t result = run(floors, world, dropped_from(200.f, 0.3f, 4.f), 1200);
    check(result.body.bounce.at_rest && near(result.body.position.y, 58.f + RADIUS, 0.5f),
          "at rest on the platform's top, not the floor");
    // Riding a mover is NOT built: a resting body stays where it stopped when the mover leaves.
    movers.front().pose_at_tick_end.position = {200.f, 50.f, 0.f};
    const shared::bounce_body_t after =
        shared::bounce_step(floors, world, result.body, RADIUS, GRAVITY, TICK);
    check(after.position.x == result.body.position.x, "a resting body does not ride a mover (not built)");
  }

  printf("a resting body wakes when written to, and a spinning one turns\n");
  {
    run_t rested = run(floors, {}, dropped_from(100.f, 0.3f, 4.f), 1200);
    check(rested.body.bounce.at_rest, "at rest first");
    shared::wake_bounce_body(rested.body.bounce, {0.f, 300.f, 0.f});
    check(!rested.body.bounce.at_rest, "a velocity write takes it out of rest");
    const shared::bounce_body_t up =
        shared::bounce_step(floors, {}, rested.body, RADIUS, GRAVITY, TICK);
    check(up.position.y > rested.body.position.y + 1.f, "and the next step moves it");

    shared::bounce_body_t spinning = dropped_from(500.f, 0.3f, 4.f);
    spinning.bounce.angular_velocity = shared::throw_spin_for(7, 3, 360.f);
    check(near(linalg::length(spinning.bounce.angular_velocity), 360.f, 1e-2f),
          "the throw spin has the asked-for rate");
    const shared::bounce_body_t turned =
        shared::bounce_step(floors, {}, spinning, RADIUS, GRAVITY, TICK);
    check(std::fabs(turned.orientation.w - 1.f) > 1e-4f, "one step turns the orientation");
  }

  if (failure_count == 0)
    printf("bounce_body_test: all passed\n");
  else
    printf("bounce_body_test: %d FAILED\n", failure_count);
  return failure_count == 0 ? 0 : 1;
}
