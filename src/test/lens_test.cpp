// A lens is seen through along the eye's line: the line must enter the box and leave it before the point,
// and the filters of every lens on the line multiply.

#include "assets/asset_state.hpp"
#include "collision/collision_detection.hpp"
#include "collision/lens_panes.hpp"
#include "collision/predicted_world.hpp"
#include "entities/generated/entities/lens_entity_generated.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities/weapon_entity_generated.hpp"
#include "entity_system.hpp"
#include "lighting/lens.hpp"
#include "map/map_geometry.hpp"
#include "movement/movement_kernel.hpp"
#include "movement/player_constants.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static int failure_count = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
}

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  using shared::LENS_CHANNEL_BLUE;
  using shared::LENS_CHANNEL_GREEN;
  using shared::LENS_CHANNEL_RED;

  printf("[pin] the filter's channel bits and colour\n");
  check(shared::get_channel_bits_for_lens_filter(entities::Lens_Filter::Red) == LENS_CHANNEL_RED, "red is bit 0");
  check(shared::get_channel_bits_for_lens_filter(entities::Lens_Filter::Green) == LENS_CHANNEL_GREEN, "green is bit 1");
  check(shared::get_channel_bits_for_lens_filter(entities::Lens_Filter::Blue) == LENS_CHANNEL_BLUE, "blue is bit 2");
  check(shared::get_color_for_lens_filter(entities::Lens_Filter::Red).x > 0.9f, "a red lens is drawn red");

  printf("[pin] the line of sight through one lens\n");
  const shared::lens_t pane = {.box = {.center = {0.f, 0.f, 0.f}, .half_extents = {64.f, 64.f, 4.f}},
                               .filter = entities::Lens_Filter::Red};
  const linalg::vec3f  eye  = {0.f, 0.f, -200.f};
  check(shared::lens_lies_on_line_of_sight(pane, eye, {0.f, 0.f, 200.f}), "a point behind the pane is seen through it");
  check(shared::lens_lies_on_line_of_sight(pane, eye, {60.f, -60.f, 200.f}), "so is one seen through its corner");
  check(!shared::lens_lies_on_line_of_sight(pane, eye, {200.f, 0.f, 200.f}), "a line crossing the pane's plane outside it misses");
  check(!shared::lens_lies_on_line_of_sight(pane, eye, {0.f, 0.f, -100.f}), "a point in front of the pane is not seen through it");
  check(!shared::lens_lies_on_line_of_sight(pane, eye, {0.f, 0.f, 0.f}), "nor is a point inside it");
  check(!shared::lens_lies_on_line_of_sight(pane, eye, {0.f, 0.f, -300.f}), "nor is one behind the eye");
  check(shared::lens_lies_on_line_of_sight(pane, {0.f, 0.f, 0.f}, {0.f, 0.f, 200.f}), "an eye inside the pane looks through it");
  check(!shared::lens_lies_on_line_of_sight(pane, {200.f, 0.f, 0.f}, {200.f, 0.f, 200.f}), "a line parallel to the pane beside it misses");
  check(shared::lens_lies_on_line_of_sight(pane, {0.f, 0.f, -200.f}, {0.f, 0.f, 200.f}), "a line parallel to two of its axes passes through the third");

  printf("[pin] the box is turned by its orientation\n");
  shared::lens_t turned     = pane;
  turned.box.orientation    = linalg::from_axis_angle({0.f, 1.f, 0.f}, 90.f);
  check(!shared::lens_lies_on_line_of_sight(pane, {-200.f, 0.f, 30.f}, {200.f, 0.f, 30.f}), "a line beside the flat pane misses it");
  check(shared::lens_lies_on_line_of_sight(turned, {-200.f, 0.f, 30.f}, {200.f, 0.f, 30.f}), "and crosses the same pane stood on its side");
  check(!shared::lens_lies_on_line_of_sight(turned, {0.f, 0.f, -200.f}, {0.f, 0.f, -100.f}), "a point in front of the turned pane is not seen through it");
  check(shared::lens_lies_on_line_of_sight(turned, {0.f, 0.f, -200.f}, {0.f, 0.f, 200.f}), "a line along the turned pane's face still passes through it");
  shared::lens_t tilted     = pane;
  tilted.box.orientation    = linalg::from_axis_angle({1.f, 0.f, 0.f}, 45.f);
  check(shared::lens_lies_on_line_of_sight(tilted, {0.f, 0.f, -200.f}, {0.f, 0.f, 200.f}), "a tilted pane is seen through at its centre");
  check(!shared::lens_lies_on_line_of_sight(tilted, {0.f, 60.f, -200.f}, {0.f, 60.f, 200.f}), "and missed where the tilt has moved its edge away");
  check(shared::lens_lies_on_line_of_sight(pane, {0.f, 60.f, -200.f}, {0.f, 60.f, 200.f}), "which the flat pane still covers");

  printf("[pin] the filters of stacked lenses multiply\n");
  const shared::lens_t green_pane = {.box = {.center = {0.f, 0.f, 50.f}, .half_extents = {64.f, 64.f, 4.f}},
                                     .filter = entities::Lens_Filter::Green};
  const shared::lens_t red_pane   = {.box = {.center = {0.f, 0.f, 100.f}, .half_extents = {64.f, 64.f, 4.f}},
                                     .filter = entities::Lens_Filter::Red};
  const linalg::vec3f  behind     = {0.f, 0.f, 200.f};
  const shared::lens_t one_red[]  = {pane};
  const shared::lens_t two_red[]  = {pane, red_pane};
  const shared::lens_t mixed[]    = {pane, green_pane};
  const shared::lens_t aside[]    = {pane, {.box    = {.center = {300.f, 0.f, 50.f}, .half_extents = {64.f, 64.f, 4.f}},
                                            .filter = entities::Lens_Filter::Green}};
  check(shared::compute_channels_seen_through_lenses({}, eye, behind, 0) == 0, "nothing is seen through no lens");
  check(shared::compute_channels_seen_through_lenses(one_red, eye, behind, 0) == LENS_CHANNEL_RED, "red through a red pane");
  check(shared::compute_channels_seen_through_lenses(two_red, eye, behind, 0) == LENS_CHANNEL_RED, "red through two red panes");
  check(shared::compute_channels_seen_through_lenses(mixed, eye, behind, 0) == 0, "nothing through a red and a green pane");
  check(shared::compute_channels_seen_through_lenses(aside, eye, behind, 0) == LENS_CHANNEL_RED,
        "a green pane off the line does not darken a red one on it");

  printf("[pin] only a switched-on lens is collected\n");
  {
    shared::Entity_System        system;
    const shared::entity_uid_t   on_uid  = system.spawn(entities::entity_type::Lens_Entity);
    const shared::entity_uid_t   off_uid = system.spawn(entities::entity_type::Lens_Entity);
    entities::Lens_Entity*       on      = system.get<entities::Lens_Entity>(on_uid);
    entities::Lens_Entity*       off     = system.get<entities::Lens_Entity>(off_uid);
    on->position                         = {10.f, 20.f, 30.f};
    on->volume.position                  = {1.f, 2.f, 3.f};
    on->filter                           = entities::Lens_Filter::Blue;
    on->orientation                      = linalg::from_axis_angle({0.f, 1.f, 0.f}, 90.f);
    off->switch_state.value              = false;

    std::vector<shared::lens_t> lenses;
    shared::collect_lenses(system, lenses);
    check(lenses.size() == 1, "one of the two lenses is on");
    check(lenses.size() == 1 && lenses[0].box.center.x == 11.f && lenses[0].box.center.y == 22.f &&
              lenses[0].box.center.z == 33.f,
          "its box sits at the entity plus the volume's offset");
    check(lenses.size() == 1 && std::fabs(lenses[0].box.orientation.y - on->orientation.y) < 1e-6f,
          "turned by the entity's orientation");
    check(lenses.size() == 1 && lenses[0].filter == entities::Lens_Filter::Blue, "and carries its filter");

    std::vector<shared::mover_t> panes;
    shared::collect_lens_panes(system, panes);
    check(panes.size() == 1 && panes[0].uid == on_uid, "the switched-on lens is a pane, a mover named after it");
    check(panes.size() == 1 && linalg::length(panes[0].pose_at_tick_start.position - panes[0].pose_at_tick_end.position) == 0.f &&
              !panes[0].crushes,
          "a pane does not move and crushes nobody");
    const linalg::vec3f pane_reach =
        shared::oriented_box_reach(on->volume.half_extents, on->orientation);
    check(panes.size() == 1 && std::fabs(panes[0].swept_bounds.max.x - (11.f + pane_reach.x)) < 1e-3f &&
              std::fabs(panes[0].swept_bounds.min.z - (33.f - pane_reach.z)) < 1e-3f,
          "its bounds are the turned box the lens is seen through");
  }

  printf("[pin] a lens held to the eye is on every line, and the panes multiply into it\n");
  {
    const shared::lens_t red_pane   = {.box    = {.center = {0.f, 0.f, -100.f}, .half_extents = {64.f, 64.f, 4.f}},
                                       .filter = entities::Lens_Filter::Red};
    const shared::lens_t green_pane = {.box    = {.center = {0.f, 0.f, -100.f}, .half_extents = {64.f, 64.f, 4.f}},
                                       .filter = entities::Lens_Filter::Green};
    const shared::lens_t red[]      = {red_pane};
    const shared::lens_t green[]    = {green_pane};
    const linalg::vec3f  from       = {0.f, 0.f, -300.f};
    const linalg::vec3f  to         = {0.f, 0.f, 0.f};
    check(shared::compute_channels_seen_through_lenses({}, from, to, LENS_CHANNEL_RED) == LENS_CHANNEL_RED,
          "a held red lens sees red with no pane");
    check(shared::compute_channels_seen_through_lenses(red, from, to, LENS_CHANNEL_RED) == LENS_CHANNEL_RED,
          "and through a red pane");
    check(shared::compute_channels_seen_through_lenses(green, from, to, LENS_CHANNEL_RED) == 0,
          "but nothing through a green pane");
    check(shared::compute_channels_seen_through_lenses(green, from, to, 0) == LENS_CHANNEL_GREEN,
          "which alone passes green");
  }

  printf("[pin] a watcher faces a point within the watch half-angle of its forward\n");
  const shared::lens_watcher_t ahead = {.eye = {0.f, 64.f, -300.f}, .forward = {0.f, 0.f, 1.f}};
  check(shared::lens_watcher_faces_point(ahead, {0.f, 64.f, 0.f}), "a point straight ahead is faced");
  check(shared::lens_watcher_faces_point(ahead, {0.f, 0.f, 0.f}), "so is one a little below the line");
  check(!shared::lens_watcher_faces_point(ahead, {0.f, 64.f, -600.f}), "one behind the watcher is not");
  check(!shared::lens_watcher_faces_point(ahead, {400.f, 64.f, -300.f}), "nor one beside it");
  const shared::lens_watcher_t built = shared::compute_lens_watcher({1.f, 2.f, 3.f}, 30.f, -10.f, LENS_CHANNEL_BLUE);
  check(built.eye.x == 1.f && built.eye.y == 2.f && built.eye.z == 3.f &&
            linalg::length(built.forward - linalg::direction_from_angles(30.f, -10.f)) < 1e-6f &&
            built.held_channels == LENS_CHANNEL_BLUE,
        "a watcher is built at the eye, facing its yaw and pitch, holding its lens");

  printf("[pin] a watcher sees a point through a pane of the point's colour\n");
  {
    const shared::lens_t          red_between   = {.box    = {.center = {0.f, 32.f, -150.f}, .half_extents = {64.f, 64.f, 4.f}},
                                                   .filter = entities::Lens_Filter::Red};
    const shared::lens_t          red_aside     = {.box    = {.center = {300.f, 32.f, -150.f}, .half_extents = {64.f, 64.f, 4.f}},
                                                   .filter = entities::Lens_Filter::Red};
    const shared::lens_watcher_t  facing_away   = {.eye = ahead.eye, .forward = {0.f, 0.f, -1.f}};
    const shared::lens_watcher_t  watchers[]    = {ahead};
    const shared::lens_watcher_t  turned_away[] = {facing_away};
    const shared::lens_t          between[]     = {red_between};
    const shared::lens_t          aside[]       = {red_aside};
    const linalg::vec3f           spot          = {0.f, 0.f, 0.f};
    check(shared::any_lens_watcher_sees_point(watchers, between, entities::Lens_Filter::Red, spot),
          "a red spot through a red pane is seen");
    check(!shared::any_lens_watcher_sees_point(watchers, between, entities::Lens_Filter::Green, spot),
          "a green spot through a red pane is not");
    check(!shared::any_lens_watcher_sees_point(watchers, aside, entities::Lens_Filter::Red, spot),
          "nor a red spot past a pane off the line");
    check(!shared::any_lens_watcher_sees_point(watchers, {}, entities::Lens_Filter::Red, spot),
          "nor anything with no pane");
    check(!shared::any_lens_watcher_sees_point(turned_away, between, entities::Lens_Filter::Red, spot),
          "nor anything by a watcher facing away");
    check(!shared::any_lens_watcher_sees_point({}, between, entities::Lens_Filter::Red, spot),
          "nor anything with nobody watching");
    const shared::lens_watcher_t holding_red   = {.eye = ahead.eye, .forward = ahead.forward, .held_channels = LENS_CHANNEL_RED};
    const shared::lens_watcher_t red_holders[] = {holding_red};
    check(shared::any_lens_watcher_sees_point(red_holders, {}, entities::Lens_Filter::Red, spot),
          "a watcher holding a red lens sees a red spot with no pane");
    check(!shared::any_lens_watcher_sees_point(red_holders, {}, entities::Lens_Filter::Green, spot),
          "but not a green one");
  }

  printf("[pin] every alive player watches, but the one the caller predicts\n");
  {
    shared::Entity_System      system;
    const shared::entity_uid_t alive_uid     = system.spawn(entities::entity_type::Player_Entity);
    const shared::entity_uid_t dead_uid      = system.spawn(entities::entity_type::Player_Entity);
    const shared::entity_uid_t predicted_uid = system.spawn(entities::entity_type::Player_Entity);
    entities::Player_Entity*   alive         = system.get<entities::Player_Entity>(alive_uid);
    alive->position                          = {10.f, 0.f, 20.f};
    alive->view_angle_yaw                    = 30.f;
    alive->view_angle_pitch                  = -10.f;
    system.get<entities::Player_Entity>(dead_uid)->health.current_health = 0;

    std::vector<shared::lens_watcher_t> watchers;
    shared::collect_lens_watchers(system, predicted_uid, watchers);
    check(watchers.size() == 1, "one of the three players watches");
    check(watchers.size() == 1 && watchers[0].eye.x == 10.f && watchers[0].eye.y == shared::player_eye_height &&
              watchers[0].eye.z == 20.f,
          "from its eye");
    check(watchers.size() == 1 &&
              linalg::length(watchers[0].forward - linalg::direction_from_angles(30.f, -10.f)) < 1e-6f,
          "facing its view angles");
    shared::collect_lens_watchers(system, shared::null_entity_uid, watchers);
    check(watchers.size() == 2, "the server leaves nobody out");
    check(watchers.size() == 2 && watchers[0].held_channels == 0 && watchers[1].held_channels == 0,
          "and nobody holds a lens");

    const shared::entity_uid_t lens_uid = system.spawn(entities::entity_type::Weapon_Entity);
    system.get<entities::Weapon_Entity>(lens_uid)->weapon_id = entities::Weapon::Red_Lens;
    alive                                                    = system.get<entities::Player_Entity>(alive_uid);
    alive->held_lens_on                                      = true;
    shared::collect_lens_watchers(system, shared::null_entity_uid, watchers);
    check(watchers.size() == 2 && watchers[0].held_channels == 0, "the flag with nothing in hand holds no lens");
    alive->inventory.weapons[entities::Inventory_Slot::Utility_1] = lens_uid;
    alive->inventory.active_slot                                  = entities::Inventory_Slot::Utility_1;
    shared::collect_lens_watchers(system, shared::null_entity_uid, watchers);
    check(watchers.size() == 2 && watchers[0].held_channels == LENS_CHANNEL_RED,
          "the Red Lens in hand with the flag set holds red");
  }

  printf("[pin] a hull collides with solid-where-lensed geometry only where a watcher sees what it touches\n");
  {
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
    const shared::aabb_bounds_t     standing_hull =
        shared::hull_aabb({0.f, shared::player_half_height - 0.01f, 0.f}, shared::player_half_width,
                          shared::player_half_height);

    const shared::lens_t         red_between = {.box    = {.center = {0.f, 32.f, -150.f}, .half_extents = {64.f, 64.f, 4.f}},
                                                .filter = entities::Lens_Filter::Red};
    const shared::lens_t         panes[]     = {red_between};
    const shared::lens_watcher_t watchers[]  = {ahead};
    const auto candidate_count = [&](uint8_t state, Span<const shared::lens_t> lenses,
                                     Span<const shared::lens_watcher_t> watching)
    {
      const shared::predicted_world_t world{
          .disabled_geometry = {&state, 1}, .lenses = lenses, .lens_watchers = watching};
      std::vector<shared::collision_candidate_t> candidates;
      shared::collect_collision_candidates(platform, world, standing_hull, candidates);
      return candidates.size();
    };
    const uint8_t red_when_lensed   = get_geometry_state_solid_where_lensed(entities::Lens_Filter::Red);
    const uint8_t green_when_lensed = get_geometry_state_solid_where_lensed(entities::Lens_Filter::Green);
    check(try_get_lens_filter_for_geometry_state(red_when_lensed) == entities::Lens_Filter::Red &&
              try_get_lens_filter_for_geometry_state(green_when_lensed) == entities::Lens_Filter::Green &&
              !try_get_lens_filter_for_geometry_state(GEOMETRY_SOLID_WHERE_LIT),
          "the byte carries the colour the brush must be seen in");
    check(candidate_count(red_when_lensed, {}, {}) == 0, "a lensed-only brush nobody watches is not there");
    check(candidate_count(red_when_lensed, panes, watchers) == 1,
          "a watcher seeing the surface under the feet through a red pane makes it solid");
    check(candidate_count(green_when_lensed, panes, watchers) == 0,
          "not when the brush wants to be seen in green");
    check(candidate_count(red_when_lensed, {}, watchers) == 0, "nor with the pane switched off");
    check(collision_is_disabled({&red_when_lensed, 1}, {Collision_Id::Type::Static_Geometry, 0}),
          "a ray or a sweep passes a lensed-only brush, watched or not");
  }

  printf(failure_count == 0 ? "lens_test: all passed\n" : "lens_test: %d FAILED\n", failure_count);
  return failure_count == 0 ? 0 : 1;
}
