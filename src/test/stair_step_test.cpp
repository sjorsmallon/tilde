// A walking hull steps up a stair as Quake does: up, forward, down, and the step is taken when
// it got further than the slide against the riser did.
//
// Pinned: a run at a staircase climbs it without leaving the ground and without losing speed, on
// treads too shallow for the old forward push (8 by 16); a riser taller than pm_step_height
// stops the hull; a lintel over a low step refuses it. Links game_shared alone.
#include "../shared/assets/asset_state.hpp"
#include "../shared/collision/collision_detection.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/map/map_geometry.hpp"
#include "../shared/movement/movement_kernel.hpp"
#include "../shared/movement/movement_settings.hpp"
#include "../shared/movement/player_move.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using cvars::cvar_state_t;

static int failures = 0;

static void check(bool condition, const char* label)
{
  printf("  %s %s\n", condition ? "ok  " : "FAIL", label);
  if (!condition)
    ++failures;
}

static void add_box(std::vector<BVH_Input>& inputs, const vec3& low, const vec3& high)
{
  const shared::mesh_geometry_t box =
      shared::make_box_mesh((low + high) * 0.5f, (high - low) * 0.5f);
  const uint32_t index = (uint32_t)inputs.size();
  for (const shared::collision_piece_t& piece : shared::get_collision_pieces(box, index + 1))
  {
    BVH_Input input;
    input.aabb             = piece.bounds;
    input.id               = {Collision_Id::Type::Static_Geometry, index};
    input.collision_planes = piece.planes;
    input.face_polygons    = piece.face_polygons;
    inputs.push_back(std::move(input));
  }
}

// A floor whose top is y = 0 for x in [-512, 0], then `count` steps rising `riser` every `tread`
// along +x, the last tread running on to x = 1024. A lintel, when asked for, is a slab whose
// underside is `lintel_clearance` above the floor over the first riser.
static Bounding_Volume_Hierarchy stair_world(int count, float riser, float tread,
                                             float lintel_clearance = 0.f)
{
  std::vector<BVH_Input> inputs;
  add_box(inputs, {-512.f, -64.f, -256.f}, {0.f, 0.f, 256.f});
  for (int step = 0; step < count; ++step)
    add_box(inputs, {step * tread, -64.f, -256.f}, {1024.f, (step + 1) * riser, 256.f});
  if (lintel_clearance > 0.f)
    add_box(inputs, {-16.f, lintel_clearance, -256.f}, {tread, lintel_clearance + 64.f, 256.f});
  return build_bvh(inputs);
}

struct run_t
{
  vec3  feet;
  float minimum_speed_after_warmup = 1e9f;
  int   air_ticks                  = 0;
  int   landings_after_first       = 0;
  float peak_rise_per_tick         = 0.f;
  float biggest_drop_per_tick      = 0.f;
  float reported_stair_rise        = 0.f;
};

static run_t run_forward(const cvar_state_t& cvars, const Bounding_Volume_Hierarchy& bvh, vec3 feet,
                         int ticks)
{
  const shared::movement_settings_t settings = shared::movement_settings_from_cvars(cvars);
  const shared::predicted_world_t   world{};
  Move_Input                        forward;
  forward.forward_pressed = true;
  vec3               velocity{};
  entities::Movement movement{};
  run_t              run;
  bool               landed_once = false;
  for (int tick = 0; tick < ticks; ++tick)
  {
    Move_Events                events{};
    const shared::move_state_t moved = player_move(
        settings, bvh, world, {.feet = feet, .velocity = velocity, .movement = movement},
        {.buttons = forward, .front = {1.f, 0.f, 0.f}, .right = {0.f, 0.f, 1.f}, .dt = 1.f / 60.f},
        &events);
    const float rise          = moved.feet.y - feet.y;
    run.peak_rise_per_tick    = std::max(run.peak_rise_per_tick, rise);
    run.biggest_drop_per_tick = std::max(run.biggest_drop_per_tick, -rise);
    run.reported_stair_rise += events.stair_rise;
    feet     = moved.feet;
    velocity = moved.velocity;
    movement = moved.movement;
    if (landed_once)
    {
      if (!movement.is_grounded)
        ++run.air_ticks;
      if (events.landed)
        ++run.landings_after_first;
    }
    landed_once |= events.landed;
    if (tick >= 60)
      run.minimum_speed_after_warmup = std::min(
          run.minimum_speed_after_warmup, std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z));
  }
  run.feet = feet;
  return run;
}

struct stair_case_t
{
  const char* label;
  int         count;
  float       riser;
  float       tread;
};

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  printf("stair_step_test\n");
  const vec3 start{-200.f, 0.f, 0.f};
  const int  ticks = 180;

  for (int model : {0, 1})
  {
    cvar_state_t cvars;
    cvars.pm_model        = (cvars::Locomotion_Model)model;
    const float run_speed = cvars.pm_maxspeed;

    for (const stair_case_t& stair : {stair_case_t{"8 by 16", 12, 8.f, 16.f},
                                      stair_case_t{"16 by 32", 8, 16.f, 32.f},
                                      stair_case_t{"17.5 by 64", 6, 17.5f, 64.f}})
    {
      const Bounding_Volume_Hierarchy bvh = stair_world(stair.count, stair.riser, stair.tread);
      const run_t                     run = run_forward(cvars, bvh, start, ticks);
      const float                     top = stair.count * stair.riser;
      printf("[model %d] stair %s: feet x %.1f y %.1f (top %.1f), min speed after warmup %.1f of "
             "%.0f, air %d, landings %d, peak rise %.2f, biggest drop %.2f\n",
             model, stair.label, run.feet.x, run.feet.y, top, run.minimum_speed_after_warmup,
             run_speed, run.air_ticks, run.landings_after_first, run.peak_rise_per_tick,
             run.biggest_drop_per_tick);
      check(std::fabs(run.feet.y - top) < 0.1f, "the hull reaches the top tread");
      check(run.feet.x > stair.count * stair.tread, "and runs on past the last riser");
      check(run.air_ticks == 0 && run.landings_after_first == 0, "without leaving the ground");
      check(run.minimum_speed_after_warmup > 0.95f * run_speed, "and without losing speed");
      check(run.peak_rise_per_tick <= stair.riser + 0.1f, "each step rises one riser at most");
      check(run.biggest_drop_per_tick < 0.1f, "and the hull never comes back down");
      check(std::fabs(run.reported_stair_rise - top) < 0.1f,
            "every riser is reported as stair_rise exactly once");
    }

    {
      const Bounding_Volume_Hierarchy bvh = stair_world(1, 20.f, 64.f);
      const run_t                     run = run_forward(cvars, bvh, start, ticks);
      printf("[model %d] riser 20: feet x %.2f y %.2f\n", model, run.feet.x, run.feet.y);
      check(run.feet.y < 0.1f, "a riser taller than pm_step_height is a wall");
      check(run.feet.x < 0.f, "the hull stops at its foot");
      check(run.reported_stair_rise == 0.f, "and a run along the flat reports no stair_rise");
    }

    {
      const shared::movement_settings_t settings  = shared::movement_settings_from_cvars(cvars);
      const float                       clearance = 2.f * settings.shared.half_height + 8.f;
      const Bounding_Volume_Hierarchy   bvh       = stair_world(1, 8.f, 64.f, clearance);
      const run_t                       run       = run_forward(cvars, bvh, start, ticks);
      printf("[model %d] lintel %.0f over a riser 8: feet x %.2f y %.2f\n", model, clearance,
             run.feet.x, run.feet.y);
      check(run.feet.y < 0.1f, "a lintel the raised hull would hit refuses the step");
      check(run.feet.x < 0.f, "and the riser stays a wall");
    }
  }

  printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
