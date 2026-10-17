// A walking hull keeps its feet across a slope and its seams.
//
// Two guards, both found on maps/new_map.source (one brush: a box whose top corner
// was pulled down, so the seam between the flat top and the slope is a convex edge
// of a single hull):
//
//   1. resolve_collisions must push along the LEAST penetrated plane. The ground
//      bias once compared a biased candidate against the unbiased winner, so the
//      flat top's plane, extended past the seam, beat the slope's plane whenever
//      it was listed later: a hull resting on the slope near the seam was lifted
//      to the flat's height, found itself over nothing, fell two ticks and was
//      lifted again, forever.
//   2. A step that opens on the ground and ends over nothing looks for ground
//      within pm_step_height below (snap_to_ground_below). Without it a hull
//      walking off the flat onto the slope, or down an axis-aligned decline under
//      quake (the overbounce leaves 0.1% of the velocity pointing away from the
//      surface), floats until gravity catches up and lands hard.
//
// Every scenario asserts: once the hull has landed, it never leaves the ground and
// never lands again. Links game_shared alone.
#include "../shared/assets/asset_state.hpp"
#include "../shared/collision/collision_detection.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/map/map_geometry.hpp"
#include "../shared/movement/movement_kernel.hpp"
#include "../shared/movement/player_move.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using cvars::cvar_state_t;

static int failures = 0;

static void add_piece_inputs(std::vector<BVH_Input>& inputs, const shared::geometry_value_t& geometry,
                             shared::entity_uid_t uid, uint32_t index)
{
  for (const shared::collision_piece_t& piece : shared::get_collision_pieces(geometry, uid, {}))
  {
    BVH_Input input;
    input.aabb             = piece.bounds;
    input.id               = {Collision_Id::Type::Static_Geometry, index};
    input.collision_planes = piece.planes;
    input.face_polygons    = piece.face_polygons;
    inputs.push_back(std::move(input));
  }
}

// A wedge from x = -512 (top at `rise`) down to x = 0 (top at 0), on a floor whose top is y = 0 for x in [0, 2048].
static Bounding_Volume_Hierarchy wedge_world(float rise)
{
  shared::brush_geometry_t wedge;
  wedge.hull_points = {{-512.f, -64.f, -256.f}, {-512.f, -64.f, 256.f}, {0.f, -64.f, -256.f},
                       {0.f, -64.f, 256.f},     {-512.f, rise, -256.f}, {-512.f, rise, 256.f},
                       {0.f, 0.f, -256.f},      {0.f, 0.f, 256.f}};
  const shared::geometry_value_t floor = shared::make_box_brush({1024.f, -32.f, 0.f}, {1024.f, 32.f, 256.f});
  std::vector<BVH_Input> inputs;
  add_piece_inputs(inputs, shared::geometry_value_t{wedge}, 1, 0);
  add_piece_inputs(inputs, floor, 2, 1);
  return build_bvh(inputs);
}

// The one brush of maps/new_map.source. Its slope surface is y = 0.2 (x - z) below the
// seam, which runs from (-640, 0, -640) to (640, 0, 640); the flat top is y = 0 above it.
static Bounding_Volume_Hierarchy map_brush_world()
{
  shared::brush_geometry_t brush;
  brush.hull_points = {{-640.f, -256.f, 640.f}, {-640.f, -128.f, -640.f}, {-640.f, 0.f, -640.f},
                       {-627.750305f, -250.496887f, 627.750305f}, {640.f, -128.f, -640.f},
                       {640.f, -128.f, 640.f}, {640.f, 0.f, -640.f}, {640.f, 0.f, 640.f}};
  std::vector<BVH_Input> inputs;
  add_piece_inputs(inputs, shared::geometry_value_t{brush}, 1, 0);
  return build_bvh(inputs);
}

struct walk_result_t
{
  int air_ticks_after_landing = 0;
  int landings_after_first    = 0;
  int grounded_ticks          = 0;
};

static walk_result_t walk(const cvar_state_t& cvars, const Bounding_Volume_Hierarchy& bvh, vec3 feet,
                          Move_Input input, const vec3& front, int ticks)
{
  const shared::movement_settings_t settings = shared::movement_settings_from_cvars(cvars);
  const shared::predicted_world_t   world{};
  const vec3                        right{-front.z, 0.f, front.x};
  vec3                              velocity{};
  entities::Movement                movement{};
  const float                       dt = 1.f / 60.f;
  walk_result_t                     result;
  bool                              landed_once = false;
  for (int tick = 0; tick < ticks; ++tick)
  {
    Move_Events events{};
    const shared::move_state_t moved =
        player_move(settings, bvh, world, {.feet = feet, .velocity = velocity, .movement = movement},
                    {.buttons = input, .front = front, .right = right, .dt = dt}, &events);
    feet     = moved.feet;
    velocity = moved.velocity;
    movement = moved.movement;
    if (landed_once)
    {
      if (!movement.is_grounded)
        ++result.air_ticks_after_landing;
      if (events.landed)
        ++result.landings_after_first;
    }
    if (movement.is_grounded)
      ++result.grounded_ticks;
    landed_once |= events.landed;
  }
  return result;
}

static void check_walk(const char* label, const walk_result_t& result, int ticks)
{
  const bool ok = result.air_ticks_after_landing == 0 && result.landings_after_first == 0 &&
                  result.grounded_ticks > ticks / 2;
  printf("  %s %-70s air after landing %2d, landings after first %2d, grounded %3d/%d\n",
         ok ? "ok  " : "FAIL", label, result.air_ticks_after_landing, result.landings_after_first,
         result.grounded_ticks, ticks);
  if (!ok)
    ++failures;
}

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  Move_Input forward;
  forward.forward_pressed = true;
  Move_Input backward;
  backward.backward_pressed = true;
  Move_Input strafe_left;
  strafe_left.left_pressed = true;
  Move_Input strafe_right;
  strafe_right.right_pressed = true;
  Move_Input forward_right;
  forward_right.forward_pressed = true;
  forward_right.right_pressed   = true;
  Move_Input none;

  printf("slope_seam_test\n");

  printf("[wedge] an axis-aligned decline onto a floor, default cvars\n");
  for (float rise : {128.f, 256.f})
  {
    const Bounding_Volume_Hierarchy bvh   = wedge_world(rise);
    const float                     slope = rise / 512.f;
    const vec3                      start{-200.f, slope * 200.f + 1.f, 0.f};
    char                            label[96];
    for (int model : {0, 1})
    {
      cvar_state_t cvars;
      cvars.pm_model = (cvars::Locomotion_Model)model;
      const int ticks = 80;
      snprintf(label, sizeof label, "rise %.0f model %d forward down onto the floor", rise, model);
      check_walk(label, walk(cvars, bvh, start, forward, {1.f, 0.f, 0.f}, ticks), ticks);
      snprintf(label, sizeof label, "rise %.0f model %d backward down, facing uphill", rise, model);
      check_walk(label, walk(cvars, bvh, start, backward, {-1.f, 0.f, 0.f}, ticks), ticks);
      snprintf(label, sizeof label, "rise %.0f model %d strafe right down, facing -z", rise, model);
      check_walk(label, walk(cvars, bvh, start, strafe_right, {0.f, 0.f, -1.f}, ticks), ticks);
    }
  }

  printf("[map brush] the seam of maps/new_map.source, its cvars (g_gravity 600, pm_maxspeed 500)\n");
  const Bounding_Volume_Hierarchy map_world = map_brush_world();
  const float                     r = 0.70710678f;
  const vec3                      downhill{-r, 0.f, r};
  const vec3                      uphill{r, 0.f, -r};
  const vec3                      along_seam{r, 0.f, r};
  // `across` is measured from the seam toward the flat; the surface below the seam is y = 0.283 across.
  const auto on_surface = [&](float across) {
    const float surface = across < 0.f ? 0.2f * 2.f * across * r : 0.f;
    return vec3{across * r, surface + 1.f, -across * r};
  };
  for (int model : {0, 1})
  {
    cvar_state_t cvars;
    cvars.pm_model    = (cvars::Locomotion_Model)model;
    cvars.g_gravity   = 600.f;
    cvars.pm_maxspeed = 500.f;
    char label[96];
    for (float across : {-44.f, -36.f, -24.f, -12.f, 0.f, 12.f})
    {
      snprintf(label, sizeof label, "model %d standing %.0f across the seam", model, across);
      check_walk(label, walk(cvars, map_world, on_surface(across), none, uphill, 40), 40);
    }
    const int ticks = 45;
    snprintf(label, sizeof label, "model %d up onto the flat: forward", model);
    check_walk(label, walk(cvars, map_world, on_surface(-150.f), forward, uphill, ticks), ticks);
    snprintf(label, sizeof label, "model %d up onto the flat: backward", model);
    check_walk(label, walk(cvars, map_world, on_surface(-150.f), backward, downhill, ticks), ticks);
    snprintf(label, sizeof label, "model %d up onto the flat: strafe left", model);
    check_walk(label, walk(cvars, map_world, on_surface(-150.f), strafe_left, along_seam, ticks), ticks);
    snprintf(label, sizeof label, "model %d down off the flat: forward", model);
    check_walk(label, walk(cvars, map_world, on_surface(100.f), forward, downhill, ticks), ticks);
    snprintf(label, sizeof label, "model %d down off the flat: backward", model);
    check_walk(label, walk(cvars, map_world, on_surface(100.f), backward, uphill, ticks), ticks);
    snprintf(label, sizeof label, "model %d down off the flat: strafe right", model);
    check_walk(label, walk(cvars, map_world, on_surface(100.f), strafe_right, along_seam, ticks), ticks);
    snprintf(label, sizeof label, "model %d down off the flat: forward+right facing +z", model);
    check_walk(label, walk(cvars, map_world, on_surface(100.f), forward_right, {0.f, 0.f, 1.f}, ticks), ticks);
  }

  printf(failures == 0 ? "slope_seam_test passed\n" : "slope_seam_test FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}
