// The pin for the hull sweep: a box swept along a move stops at the first face
// any part of it touches, with that face's normal, through the same candidate
// set the penetration resolver collects. Links game_shared alone.
#include "assets/asset_state.hpp"
#include "collision/collision_detection.hpp"
#include "collision/disabled_geometry.hpp"
#include "collision/movers.hpp"
#include "collision/predicted_world.hpp"
#include "map/map_geometry.hpp"
#include "movement/hull_sweep.hpp"
#include "movement/player_constants.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using linalg::vec3;

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

static bool near(const vec3& a, const vec3& b, float tolerance = 1e-3f)
{
  return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

static void add_piece_inputs(std::vector<BVH_Input>& inputs, const shared::mesh_geometry_t& geometry,
                             uint32_t index)
{
  for (const shared::collision_piece_t& piece : shared::get_collision_pieces(geometry, index + 1))
  {
    BVH_Input input;
    input.aabb             = piece.bounds;
    input.id               = {Collision_Id::Type::Static_Geometry, index};
    input.collision_planes = piece.planes;
    input.face_polygons    = piece.face_polygons;
    inputs.push_back(std::move(input));
  }
}

static Bounding_Volume_Hierarchy build_world(const std::vector<shared::mesh_geometry_t>& solids)
{
  std::vector<BVH_Input> inputs;
  for (uint32_t index = 0; index < solids.size(); ++index)
    add_piece_inputs(inputs, solids[index], index);
  return build_bvh(inputs);
}

static constexpr float half_width  = shared::player_half_width;
static constexpr float half_height = shared::player_half_height;
static constexpr float skin        = hull_sweep_skin;

static std::optional<shared::hull_sweep_hit_t> sweep(const Bounding_Volume_Hierarchy& bvh,
                                                     const shared::predicted_world_t& world,
                                                     const vec3& from, const vec3& to)
{
  return shared::sweep_hull(bvh, world, from, to, half_width, half_height);
}

static vec3 stop_point(const vec3& from, const vec3& to, const shared::hull_sweep_hit_t& hit)
{
  return from + (to - from) * hit.t;
}

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  // Index 0: a floor whose top is y = 0. Index 1: a wall whose -x face is x = 200, rising to y = 128.
  // Index 2: a step 12 tall whose riser faces -x at x = 400.
  const shared::mesh_geometry_t floor = shared::make_box_mesh({0.f, -64.f, 0.f}, {2048.f, 64.f, 2048.f});
  const shared::mesh_geometry_t wall  = shared::make_box_mesh({216.f, 64.f, 0.f}, {16.f, 64.f, 256.f});
  const shared::mesh_geometry_t step  = shared::make_box_mesh({432.f, 6.f, 0.f}, {32.f, 6.f, 256.f});
  const Bounding_Volume_Hierarchy world_bvh = build_world({floor, wall, step});
  const shared::predicted_world_t empty_world{};

  printf("the feet stop on the floor\n");
  {
    const vec3 from{0.f, 200.f, 0.f};
    const vec3 to{0.f, 0.f, 0.f};
    const std::optional<shared::hull_sweep_hit_t> hit = sweep(world_bvh, empty_world, from, to);
    check(hit.has_value(), "a falling hull hits the floor");
    check(hit && near(stop_point(from, to, *hit), {0.f, half_height + skin, 0.f}),
          "the centre stops half_height plus the skin above the top face: the feet are on it");
    check(hit && near(hit->normal, {0.f, 1.f, 0.f}), "the normal is the floor's");
    check(hit && hit->id.type == Collision_Id::Type::Static_Geometry && hit->id.index == 0,
          "the hit names the floor's collision id");
    check(hit && hit->mover_uid == shared::null_entity_uid, "the map is no mover");
  }

  printf("the side stops at a wall\n");
  {
    const vec3 from{0.f, half_height, 0.f};
    const vec3 to{400.f, half_height, 0.f};
    const std::optional<shared::hull_sweep_hit_t> hit = sweep(world_bvh, empty_world, from, to);
    check(hit.has_value(), "a walking hull hits the wall");
    check(hit && near(stop_point(from, to, *hit).x, 200.f - half_width - skin),
          "the centre stops half_width plus the skin short of the face: the side is on it");
    check(hit && near(hit->normal, {-1.f, 0.f, 0.f}), "the normal is the wall's -x face");
    check(hit && hit->id.index == 1, "the hit names the wall");
  }

  printf("a step edge: forward meets the riser, down meets the tread\n");
  {
    const vec3 from{300.f, half_height, 0.f};
    const vec3 to{500.f, half_height, 0.f};
    const std::optional<shared::hull_sweep_hit_t> forward = sweep(world_bvh, empty_world, from, to);
    check(forward && near(forward->normal, {-1.f, 0.f, 0.f}), "walking into the step meets its riser");
    check(forward && near(stop_point(from, to, *forward).x, 400.f - half_width - skin),
          "and stops with the side against it");

    const vec3 raised_from{410.f, half_height + 18.f, 0.f};
    const vec3 raised_to{410.f, half_height - 18.f, 0.f};
    const std::optional<shared::hull_sweep_hit_t> down =
        sweep(world_bvh, empty_world, raised_from, raised_to);
    check(down && near(down->normal, {0.f, 1.f, 0.f}), "dropping onto the step meets its tread");
    check(down && near(stop_point(raised_from, raised_to, *down).y, half_height + 12.f + skin),
          "and rests with the feet on it, 12 above the floor");
    check(down && down->id.index == 2, "the tread is the step's, not the floor's");
  }

  printf("a corner stops on a ramp between the hull's corners\n");
  {
    // A 45 degree wedge: top plane through (0, 0) and (128, 128) for x in [0, 128], facing (-1, 1, 0).
    const vec3 wedge_points[] = {{0.f, -64.f, -256.f},  {0.f, -64.f, 256.f}, {128.f, -64.f, -256.f},
                                 {128.f, -64.f, 256.f}, {0.f, 0.f, -256.f},  {0.f, 0.f, 256.f},
                                 {128.f, 128.f, -256.f}, {128.f, 128.f, 256.f}};
    const Bounding_Volume_Hierarchy ramp_bvh = build_world({*shared::try_make_convex_hull_mesh(wedge_points)});

    const vec3 from{64.f, 300.f, 0.f};
    const vec3 to{64.f, 0.f, 0.f};
    const std::optional<shared::hull_sweep_hit_t> hit = sweep(ramp_bvh, empty_world, from, to);
    check(hit.has_value(), "a hull dropped onto the ramp hits it");
    const float root_half = std::sqrt(0.5f);
    check(hit && near(hit->normal, {-root_half, root_half, 0.f}), "on the ramp face");
    // The lowest corner along the ramp normal is the hull's +x, -y corner, at (80, y - 36).
    // It touches the plane y = x when y - 36 = 80, so the centre rests at y = 116.
    check(hit && near(stop_point(from, to, *hit).y, 116.f + skin / root_half, 0.01f),
          "the centre stops where the hull's low corner touches the ramp, not where its centre line does");
  }

  printf("a thin wall crossed in one move is still hit\n");
  {
    const shared::mesh_geometry_t thin = shared::make_box_mesh({500.f, 64.f, 0.f}, {1.f, 64.f, 256.f});
    const Bounding_Volume_Hierarchy thin_bvh = build_world({thin});
    const vec3 from{0.f, half_height, 0.f};
    const vec3 to{1000.f, half_height, 0.f};
    const std::optional<shared::hull_sweep_hit_t> hit = sweep(thin_bvh, empty_world, from, to);
    check(hit && near(stop_point(from, to, *hit).x, 499.f - half_width - skin),
          "the traversal box lists the wall and the sweep stops at its near face");
  }

  printf("a hull already touching the floor is a hit at zero, one above it is a miss\n");
  {
    const vec3 resting{0.f, half_height + skin, 0.f};
    const std::optional<shared::hull_sweep_hit_t> down =
        sweep(world_bvh, empty_world, resting, resting - vec3{0.f, 1.f, 0.f});
    check(down && down->t == 0.f, "resting on the floor and pushing down: a hit at t = 0");
    check(down && near(down->normal, {0.f, 1.f, 0.f}), "with the floor's normal");

    const vec3 exactly_on{0.f, half_height, 0.f};
    const std::optional<shared::hull_sweep_hit_t> exact_down =
        sweep(world_bvh, empty_world, exactly_on, exactly_on - vec3{0.f, 1.f, 0.f});
    check(exact_down && exact_down->t == 0.f && near(exact_down->normal, {0.f, 1.f, 0.f}),
          "a hull at exactly zero distance pushing down is the same hit");
    const std::optional<shared::hull_sweep_hit_t> exact_along =
        sweep(world_bvh, empty_world, exactly_on, exactly_on + vec3{50.f, 0.f, 0.f});
    check(!exact_along.has_value(), "and sliding along at exactly zero distance enters nothing");

    const vec3 hovering{0.f, half_height + 1.f, 0.f};
    const std::optional<shared::hull_sweep_hit_t> short_drop =
        sweep(world_bvh, empty_world, hovering, hovering - vec3{0.f, 0.5f, 0.f});
    check(!short_drop.has_value(), "nothing past the move's end is reported");

    const std::optional<shared::hull_sweep_hit_t> up =
        sweep(world_bvh, empty_world, resting, resting + vec3{0.f, 10.f, 0.f});
    check(!up.has_value(), "resting on the floor and moving up: the floor is behind, no hit");

    const std::optional<shared::hull_sweep_hit_t> along =
        sweep(world_bvh, empty_world, resting, resting + vec3{50.f, 0.f, 0.f});
    check(!along.has_value(), "sliding along the floor at zero distance does not enter it");
  }

  printf("a zero-length move answers nothing\n");
  {
    const vec3 at{0.f, 100.f, 0.f};
    check(!sweep(world_bvh, empty_world, at, at).has_value(), "no direction to enter along");
  }

  printf("a disabled solid is not there\n");
  {
    shared::disabled_geometry_t disabled(2, 0);
    disabled[1] = 1;
    shared::predicted_world_t world{};
    world.disabled_geometry = disabled;
    const vec3 from{0.f, half_height, 0.f};
    const vec3 to{300.f, half_height, 0.f};
    const std::optional<shared::hull_sweep_hit_t> hit = sweep(world_bvh, world, from, to);
    check(!hit.has_value(), "the sweep walks through the switched-off wall");
  }

  printf("a mover stops the sweep at its end pose and names itself\n");
  {
    constexpr shared::entity_uid_t platform_uid = 77;
    shared::mover_t                platform;
    platform.uid                         = platform_uid;
    platform.pose_at_tick_start.position = {0.f, 100.f, 0.f};
    platform.pose_at_tick_end.position   = {0.f, 100.f, 0.f};
    platform.pieces = shared::get_collision_pieces(shared::make_box_mesh({0.f, 100.f, 0.f}, {64.f, 8.f, 64.f}),
                                                   platform_uid);
    platform.swept_bounds = platform.pieces.front().bounds;
    std::vector<shared::mover_t> movers = {platform};
    shared::predicted_world_t    world{};
    world.movers = movers;

    const vec3 from{0.f, 300.f, 0.f};
    const vec3 to{0.f, 0.f, 0.f};
    const std::optional<shared::hull_sweep_hit_t> hit = sweep(world_bvh, world, from, to);
    check(hit.has_value(), "the falling hull hits the platform");
    check(hit && near(stop_point(from, to, *hit).y, 108.f + half_height + skin),
          "the feet rest on the platform's top, not the floor");
    check(hit && hit->mover_uid == platform_uid, "the hit names the mover");

    const vec3 beside_from{300.f, 300.f, 0.f};
    const vec3 beside_to{300.f, 0.f, 0.f};
    const std::optional<shared::hull_sweep_hit_t> beside = sweep(world_bvh, world, beside_from, beside_to);
    check(beside && beside->mover_uid == shared::null_entity_uid &&
              near(stop_point(beside_from, beside_to, *beside).y, half_height + skin),
          "beside the platform the floor answers");
  }

  if (failure_count == 0)
    printf("hull_sweep_test: all checks passed\n");
  else
    printf("hull_sweep_test: %d FAILED\n", failure_count);
  return failure_count == 0 ? 0 : 1;
}
