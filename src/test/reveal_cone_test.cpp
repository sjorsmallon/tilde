// The reveal cone as collision reads it: eight planes around the round cone the
// shader draws, and a range that is a distance from the eye.

#include "collision_detection.hpp"
#include "entities/generated/entities/mover_entity_generated.hpp"
#include "entities/generated/entities/path_node_entity_generated.hpp"
#include "entities/generated/entities/reveal_light_entity_generated.hpp"
#include "entity_system.hpp"
#include "map_geometry.hpp"
#include "movement_kernel.hpp"
#include "player_constants.hpp"
#include "predicted_world.hpp"
#include "reveal_light.hpp"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>

static int failure_count = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
}

static shared::aabb_bounds_t point_box(const linalg::vec3f& point)
{
  return {point, point};
}

static linalg::vec3f direction_off_axis(const linalg::vec3f& axis, float off_axis_degrees,
                                        float around_degrees)
{
  const linalg::vec3f helper = std::fabs(axis.x) < 0.9f ? linalg::vec3f{1.f, 0.f, 0.f}
                                                        : linalg::vec3f{0.f, 0.f, 1.f};
  const linalg::vec3f first  = linalg::normalize(linalg::cross(axis, helper));
  const linalg::vec3f second = linalg::cross(axis, first);

  const float off_axis = linalg::to_radians(off_axis_degrees);
  const float around   = linalg::to_radians(around_degrees);
  return axis * std::cos(off_axis) +
         (first * std::cos(around) + second * std::sin(around)) * std::sin(off_axis);
}

int main()
{
  printf("[pin] a reveal cone's planes hold everything the shader draws and little else\n");

  constexpr float HALF_ANGLE_DEGREES = 25.f;
  constexpr float RANGE              = 1024.f;

  const float corner_degrees =
      std::atan(std::tan(linalg::to_radians(HALF_ANGLE_DEGREES)) /
                std::cos(linalg::PI / static_cast<float>(shared::REVEAL_CONE_SIDE_COUNT))) *
      (180.f / linalg::PI);

  const linalg::vec3f axes[] = {linalg::normalize(linalg::vec3f{1.f, 0.f, 0.f}),
                                linalg::normalize(linalg::vec3f{0.f, -1.f, 0.f}),
                                linalg::normalize(linalg::vec3f{0.f, 1.f, 0.f}),
                                linalg::normalize(linalg::vec3f{0.3f, -0.6f, 0.74f})};
  const linalg::vec3f apex   = {40.f, 72.f, -16.f};

  bool every_drawn_point_is_held   = true;
  bool nothing_past_corner_is_held = true;
  bool nothing_behind_is_held      = true;
  for (const linalg::vec3f& axis : axes)
  {
    const shared::reveal_cone_planes_t planes = shared::planes_of_reveal_cone(
        {.apex                 = apex,
         .axis                 = axis,
         .range                = RANGE,
         .cosine_of_half_angle = std::cos(linalg::to_radians(HALF_ANGLE_DEGREES))});

    for (float around = 0.f; around < 360.f; around += 5.f)
    {
      for (float reach : {1.f, 300.f, RANGE - 1.f})
      {
        for (float off_axis : {0.f, 12.f, HALF_ANGLE_DEGREES - 0.1f})
          if (!shared::reveal_cone_touches_box(
                  planes, point_box(apex + direction_off_axis(axis, off_axis, around) * reach)))
            every_drawn_point_is_held = false;

        for (float off_axis : {corner_degrees + 0.1f, 45.f, 90.f})
          if (shared::reveal_cone_touches_box(
                  planes, point_box(apex + direction_off_axis(axis, off_axis, around) * reach)))
            nothing_past_corner_is_held = false;
      }
    }

    if (shared::reveal_cone_touches_box(planes, point_box(apex - axis * 4.f)))
      nothing_behind_is_held = false;
  }
  check(every_drawn_point_is_held, "every point inside the round cone is inside the planes");
  check(nothing_past_corner_is_held, "no point past the pyramid's corner angle is inside the planes");
  check(nothing_behind_is_held, "a point behind the eye is outside");
  check(corner_degrees - HALF_ANGLE_DEGREES < 2.f, "the corners stand less than two degrees past the cone");

  const linalg::vec3f                forward = {1.f, 0.f, 0.f};
  const shared::reveal_cone_planes_t level   = shared::planes_of_reveal_cone(
      {.apex                 = {0.f, 64.f, 0.f},
       .axis                 = forward,
       .range                = RANGE,
       .cosine_of_half_angle = std::cos(linalg::to_radians(HALF_ANGLE_DEGREES))});

  check(shared::reveal_cone_touches_box(level, point_box({RANGE - 1.f, 64.f, 0.f})),
        "a point just short of the range is inside");
  check(!shared::reveal_cone_touches_box(level, point_box({RANGE + 1.f, 64.f, 0.f})),
        "a point just past the range is outside");
  check(!shared::reveal_cone_touches_box(
            level, point_box(linalg::vec3f{0.f, 64.f, 0.f} +
                             direction_off_axis(forward, HALF_ANGLE_DEGREES - 1.f, 0.f) * (RANGE + 1.f))),
        "the far end is round: past the range off the axis is outside, however near the cap plane would be");

  check(shared::reveal_cone_touches_box(level, {{200.f, -400.f, -16.f}, {232.f, 64.f, 16.f}}),
        "a box that straddles a side is touched");
  check(!shared::reveal_cone_touches_box(level, {{200.f, -400.f, -16.f}, {232.f, -100.f, 16.f}}),
        "a box wholly below the cone is not");
  check(shared::reveal_cone_touches_box(level, {{-8.f, 56.f, -8.f}, {8.f, 72.f, 8.f}}),
        "a box that holds the eye is touched");
  check(!shared::reveal_cone_touches_box(level, {{RANGE + 10.f, 0.f, -500.f}, {RANGE + 50.f, 128.f, 500.f}}),
        "a box whose nearest point is past the range is not");

  const shared::aabb_bounds_t        floor_under_feet = {{-16.f, -0.01f, -16.f}, {16.f, 0.f, 16.f}};
  const shared::reveal_cone_planes_t looking_down     = shared::planes_of_reveal_cone(
      {.apex                 = {0.f, 64.f, 0.f},
       .axis                 = {0.f, -1.f, 0.f},
       .range                = RANGE,
       .cosine_of_half_angle = std::cos(linalg::to_radians(HALF_ANGLE_DEGREES))});
  check(!shared::reveal_cone_touches_box(level, floor_under_feet),
        "looking level, the holder's cone does not touch the floor under its own feet");
  check(shared::reveal_cone_touches_box(looking_down, floor_under_feet),
        "looking straight down, it does");

  const shared::reveal_cone_settings_t overhead_settings = {
      .range = RANGE, .half_angle_degrees = HALF_ANGLE_DEGREES, .overhead_height = 128.f};
  const linalg::vec3f         eye      = {0.f, 64.f, 0.f};
  const shared::reveal_cone_t overhead = shared::compute_player_reveal_cone(eye, 0.f, 0.f, true, entities::Reveal_Cone_Kind::Reveals, overhead_settings);
  const shared::reveal_cone_t down_aim = shared::compute_player_reveal_cone(eye, 0.f, 0.f, false, entities::Reveal_Cone_Kind::Reveals, overhead_settings);
  check(linalg::length(overhead.apex - linalg::vec3f{0.f, 192.f, 0.f}) < 1e-4f &&
            linalg::length(overhead.axis - linalg::vec3f{0.f, -1.f, 0.f}) < 1e-4f,
        "an overhead cone hangs its height above the eye and points straight down");
  check(shared::reveal_cone_touches_box(shared::planes_of_reveal_cone(overhead), floor_under_feet),
        "so it lights the floor under its holder's feet while the holder looks level");
  check(linalg::length(down_aim.apex - eye) < 1e-4f &&
            !shared::reveal_cone_touches_box(shared::planes_of_reveal_cone(down_aim), floor_under_feet),
        "not overhead, the same call is the cone from the eye down the aim");

  const shared::reveal_cone_t too_wide =
      shared::reveal_cone_from_eye({0.f, 0.f, 0.f}, 0.f, 0.f, {.range = RANGE, .half_angle_degrees = 140.f});
  check(std::fabs(too_wide.cosine_of_half_angle -
                  std::cos(linalg::to_radians(shared::MAX_REVEAL_HALF_ANGLE_DEGREES))) < 1e-5f,
        "a half angle past the limit is built at the limit");

  printf("\n[pin] a hull collides with solid-where-lit geometry only where a cone touches what it touches\n");

  std::vector<BVH_Input> inputs;
  for (const shared::collision_piece_t& piece :
       shared::get_collision_pieces(shared::make_box_brush({0.f, -8.f, 0.f}, {128.f, 8.f, 128.f}), 1))
  {
    BVH_Input input;
    input.aabb             = piece.bounds;
    input.id               = {Collision_Id::Type::Static_Geometry, 0};
    input.collision_planes = piece.planes;
    input.face_polygons    = piece.face_polygons;
    inputs.push_back(std::move(input));
  }
  const Bounding_Volume_Hierarchy platform = build_bvh(inputs);

  const shared::aabb_bounds_t standing_hull =
      shared::hull_aabb({0.f, shared::player_half_height - 0.01f, 0.f}, shared::player_half_width,
                        shared::player_half_height);

  const auto candidate_count = [&](uint8_t state, Span<const shared::reveal_cone_planes_t> cones)
  {
    const shared::predicted_world_t world{.disabled_geometry = {&state, 1}, .reveal_cones = cones};
    std::vector<shared::collision_candidate_t> candidates;
    shared::collect_collision_candidates(platform, world, standing_hull, candidates);
    return candidates.size();
  };

  const float                        cosine         = std::cos(linalg::to_radians(HALF_ANGLE_DEGREES));
  const linalg::vec3f                teammate_eye   = {-200.f, 64.f, 0.f};
  const shared::reveal_cone_planes_t lights_feet    = shared::planes_of_reveal_cone(
      {.apex  = teammate_eye,
       .axis  = linalg::normalize(linalg::vec3f{0.f, 0.f, 0.f} - teammate_eye),
       .range = RANGE,
       .cosine_of_half_angle = cosine});
  const shared::reveal_cone_planes_t lights_head = shared::planes_of_reveal_cone(
      {.apex = {-60.f, 64.f, 0.f}, .axis = {1.f, 0.f, 0.f}, .range = RANGE, .cosine_of_half_angle = cosine});

  check(candidate_count(GEOMETRY_SOLID, {}) == 1, "a plain brush is a candidate with no light at all");
  check(candidate_count(GEOMETRY_NOT_THERE, {&lights_feet, 1}) == 0,
        "a switched-off brush is not, however lit");
  check(candidate_count(GEOMETRY_SOLID_WHERE_LIT, {}) == 0, "a lit-only brush in the dark is not there");
  check(candidate_count(GEOMETRY_SOLID_WHERE_LIT, {&lights_feet, 1}) == 1,
        "a cone on the surface under the feet makes it solid");
  check(shared::reveal_cone_touches_box(lights_head, standing_hull) &&
            candidate_count(GEOMETRY_SOLID_WHERE_LIT, {&lights_head, 1}) == 0,
        "a cone on the hull's head and not on the surface it touches does not");

  printf("\n[pin] a hull passes solid-unless-erased geometry only where an erase cone holds all it touches\n");

  const shared::reveal_cone_planes_t erases_from_above = shared::planes_of_reveal_cone(
      {.apex                 = {0.f, 256.f, 0.f},
       .axis                 = {0.f, -1.f, 0.f},
       .range                = RANGE,
       .cosine_of_half_angle = cosine,
       .kind                 = entities::Reveal_Cone_Kind::Erases});
  shared::reveal_cone_planes_t erases_feet = lights_feet;
  erases_feet.kind                         = entities::Reveal_Cone_Kind::Erases;
  const shared::reveal_cone_planes_t erases_a_spot = shared::planes_of_reveal_cone(
      {.apex                 = {0.f, 256.f, 0.f},
       .axis                 = {0.f, -1.f, 0.f},
       .range                = RANGE,
       .cosine_of_half_angle = std::cos(linalg::to_radians(2.f)),
       .kind                 = entities::Reveal_Cone_Kind::Erases});
  shared::reveal_cone_planes_t reveals_from_above = erases_from_above;
  reveals_from_above.kind                         = entities::Reveal_Cone_Kind::Reveals;

  check(candidate_count(GEOMETRY_SOLID_UNLESS_ERASED, {}) == 1, "an erasable brush with no cone on it is solid");
  check(candidate_count(GEOMETRY_SOLID_UNLESS_ERASED, {&erases_from_above, 1}) == 0,
        "an erase cone that holds everything the hull touches lets it through");
  check(candidate_count(GEOMETRY_SOLID_UNLESS_ERASED, {&erases_a_spot, 1}) == 1,
        "one that holds only a spot of what the hull touches does not");
  check(candidate_count(GEOMETRY_SOLID_UNLESS_ERASED, {&reveals_from_above, 1}) == 1,
        "a Flashlight's cone erases nothing");
  check(candidate_count(GEOMETRY_SOLID_WHERE_LIT, {&erases_feet, 1}) == 0,
        "and an Eraser's cone makes nothing solid");

  const uint8_t erasable = GEOMETRY_SOLID_UNLESS_ERASED;
  check(!collision_is_disabled({&erasable, 1}, {Collision_Id::Type::Static_Geometry, 0}),
        "a ray or a sweep meets an erasable brush wherever the cones are");

  printf("\n[pin] a map-placed light is one cone from where it stands, only while switched on\n");
  {
    shared::Entity_System system;
    const shared::entity_uid_t     light_uid = system.spawn(entities::entity_type::Reveal_Light_Entity);
    entities::Reveal_Light_Entity* light     = system.get<entities::Reveal_Light_Entity>(light_uid);
    light->position           = {40.f, 72.f, -16.f};
    light->kind               = entities::Reveal_Cone_Kind::Erases;
    light->range              = 300.f;
    light->half_angle_degrees = 10.f;

    const shared::reveal_cone_settings_t player_settings = {.range = 1024.f, .half_angle_degrees = 25.f};
    std::vector<shared::reveal_cone_planes_t> collected;
    shared::collect_reveal_cones(system, {}, {}, player_settings, 1, 60.f, shared::null_entity_uid, collected);
    check(collected.size() == 1, "a switched-on light is one cone");
    check(collected.size() == 1 && linalg::length(collected[0].apex - light->position) < 1e-4f &&
              linalg::length(collected[0].axis - linalg::forward(light->orientation)) < 1e-4f,
          "from its position, down its orientation");
    check(collected.size() == 1 && collected[0].kind == entities::Reveal_Cone_Kind::Erases &&
              collected[0].range == 300.f &&
              std::fabs(collected[0].cosine_of_half_angle - std::cos(linalg::to_radians(10.f))) < 1e-6f,
          "with its own kind, range and half-angle, not the players' settings");

    light->switch_state.value = false;
    shared::collect_reveal_cones(system, {}, {}, player_settings, 1, 60.f, shared::null_entity_uid, collected);
    check(collected.empty(), "a switched-off light is no cone");
  }

  printf("\n[pin] a light that follows a mover rides it rigidly from where it was placed\n");
  {
    shared::Entity_System system;
    const shared::entity_uid_t first_uid  = system.spawn(entities::entity_type::Path_Node_Entity);
    const shared::entity_uid_t second_uid = system.spawn(entities::entity_type::Path_Node_Entity);
    const shared::entity_uid_t mover_uid  = system.spawn(entities::entity_type::Mover_Entity);
    const shared::entity_uid_t light_uid  = system.spawn(entities::entity_type::Reveal_Light_Entity);

    const linalg::quatf turned = linalg::from_view_angles(90.f, 0.f);

    entities::Path_Node_Entity* first = system.get<entities::Path_Node_Entity>(first_uid);
    first->position          = {0.f, 100.f, 0.f};
    first->next              = second_uid;
    first->traversal_seconds = 1.f;
    entities::Path_Node_Entity* second = system.get<entities::Path_Node_Entity>(second_uid);
    second->position    = {200.f, 100.f, 0.f};
    second->orientation = turned;

    system.get<entities::Mover_Entity>(mover_uid)->follow = {.from = first_uid, .segment_start_tick = 1, .direction = 1};

    entities::Reveal_Light_Entity* light = system.get<entities::Reveal_Light_Entity>(light_uid);
    light->position = {0.f, 150.f, 0.f};
    light->follows  = mover_uid;

    const shared::path_links_t links = shared::derive_path_links(system);
    const shared::reveal_cone_settings_t player_settings = {.range = 1024.f, .half_angle_degrees = 25.f};
    std::vector<shared::reveal_cone_planes_t> collected;

    shared::collect_reveal_cones(system, links, {}, player_settings, 1, 60.f, shared::null_entity_uid, collected);
    check(collected.size() == 1 && linalg::length(collected[0].apex - light->position) < 1e-3f,
          "with its mover at rest it is where it was placed");

    shared::collect_reveal_cones(system, links, {}, player_settings, 31, 60.f, shared::null_entity_uid, collected);
    check(collected.size() == 1 &&
              linalg::length(collected[0].apex - linalg::vec3f{100.f, 150.f, 0.f}) < 1e-3f,
          "halfway through the segment it has moved half the segment, keeping its offset");

    shared::collect_reveal_cones(system, links, {}, player_settings, 61, 60.f, shared::null_entity_uid, collected);
    check(collected.size() == 1 &&
              linalg::length(collected[0].apex - linalg::vec3f{200.f, 150.f, 0.f}) < 1e-3f &&
              linalg::length(collected[0].axis - linalg::forward(turned * light->orientation)) < 1e-3f,
          "at the far node it has turned as the mover has");

    light->switch_state.value = false;
    shared::collect_reveal_cones(system, links, {}, player_settings, 31, 60.f, shared::null_entity_uid, collected);
    check(collected.empty(), "switched off it is no cone, wherever its mover has it");

    light->switch_state.value = true;
    light->follows            = first_uid;
    shared::collect_reveal_cones(system, links, {}, player_settings, 31, 60.f, shared::null_entity_uid, collected);
    check(collected.size() == 1 && linalg::length(collected[0].apex - light->position) < 1e-3f,
          "following something that is not a mover, it stays where it was placed");
  }

  printf(failure_count == 0 ? "\nALL PASSED\n" : "\n%d FAILED\n", failure_count);
  return failure_count == 0 ? 0 : 1;
}
