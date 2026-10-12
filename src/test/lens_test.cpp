// A lens is seen through along the eye's line: the line must enter the box and leave it before the point,
// and the filters of every lens on the line multiply.

#include "assets/asset_state.hpp"
#include "entities/generated/entities/lens_entity_generated.hpp"
#include "entity_system.hpp"
#include "lighting/lens.hpp"

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
  check(shared::compute_channels_seen_through_lenses({}, eye, behind) == 0, "nothing is seen through no lens");
  check(shared::compute_channels_seen_through_lenses(one_red, eye, behind) == LENS_CHANNEL_RED, "red through a red pane");
  check(shared::compute_channels_seen_through_lenses(two_red, eye, behind) == LENS_CHANNEL_RED, "red through two red panes");
  check(shared::compute_channels_seen_through_lenses(mixed, eye, behind) == 0, "nothing through a red and a green pane");
  check(shared::compute_channels_seen_through_lenses(aside, eye, behind) == LENS_CHANNEL_RED,
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
  }

  printf(failure_count == 0 ? "lens_test: all passed\n" : "lens_test: %d FAILED\n", failure_count);
  return failure_count == 0 ? 0 : 1;
}
