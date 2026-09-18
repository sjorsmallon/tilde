// mover_def.md ss9 step 1: a mover's pose is a pure function of its trajectory and the chain.
#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "mover_path.hpp"

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

static bool is_near(const linalg::vec3f& a, const linalg::vec3f& b, float tolerance = 1e-4f)
{
  return std::fabs(a.x - b.x) <= tolerance && std::fabs(a.y - b.y) <= tolerance &&
         std::fabs(a.z - b.z) <= tolerance;
}

static constexpr float TICKRATE = 60.0f;

struct node_spec_t
{
  linalg::vec3f    position;
  float            traversal_seconds = 1.0f;
  float            wait_seconds      = 0.0f;
  entities::Easing easing            = entities::Easing::Linear;
};

static std::vector<shared::entity_uid_t> spawn_nodes(shared::Entity_System& system,
                                                     const std::vector<node_spec_t>& specs)
{
  std::vector<shared::entity_uid_t> uids;
  for (const node_spec_t& spec : specs)
  {
    const shared::entity_uid_t uid = system.spawn<entities::Path_Node_Entity>();
    entities::Path_Node_Entity* node = system.get<entities::Path_Node_Entity>(uid);
    node->position          = spec.position;
    node->traversal_seconds = spec.traversal_seconds;
    node->wait_seconds      = spec.wait_seconds;
    node->easing            = spec.easing;
    uids.push_back(uid);
  }
  return uids;
}

static void link(shared::Entity_System& system, shared::entity_uid_t from, shared::entity_uid_t to)
{
  system.get<entities::Path_Node_Entity>(from)->next = to;
}

static linalg::vec3f pose_position(const shared::Entity_System& system,
                                   const shared::path_links_t& links,
                                   const entities::Path_Follow& follow, uint32_t tick)
{
  const std::optional<shared::path_pose_t> pose =
      shared::try_path_pose_at(system, links, follow, tick, TICKRATE);
  return pose ? pose->position : linalg::vec3f{NAN, NAN, NAN};
}

static void run_to(const shared::Entity_System& system, const shared::path_links_t& links,
                   entities::Path_Follow& follow, uint32_t from_tick, uint32_t to_tick,
                   std::vector<shared::entity_uid_t>& reached)
{
  for (uint32_t tick = from_tick; tick <= to_tick; ++tick)
    shared::advance_path_follow(system, links, follow, tick, TICKRATE, reached);
}

static void test_linear_and_smooth()
{
  printf("\n[pin] a segment lerps linearly, or by smoothstep\n");

  shared::path_segment_t segment{
      .from_position   = {0, 0, 0},
      .to_position     = {60, 0, 0},
      .traversal_ticks = 60,
  };
  check(is_near(shared::transform_at(segment, 100, 100).position, {0, 0, 0}), "start is the from node");
  check(is_near(shared::transform_at(segment, 100, 115).position, {15, 0, 0}), "a quarter is a quarter");
  check(is_near(shared::transform_at(segment, 100, 160).position, {60, 0, 0}), "the end is the to node");
  check(is_near(shared::transform_at(segment, 100, 90).position, {0, 0, 0}),
        "before the segment starts it is at the from node");

  segment.easing = entities::Easing::Smooth;
  check(is_near(shared::transform_at(segment, 100, 115).position, {60 * 0.15625f, 0, 0}),
        "smooth at a quarter is smoothstep(0.25)");
  check(is_near(shared::transform_at(segment, 100, 130).position, {30, 0, 0}),
        "smooth passes the midpoint at half time");

  segment.easing           = entities::Easing::Linear;
  segment.from_orientation = linalg::quatf::identity();
  segment.to_orientation   = linalg::from_axis_angle({0, 1, 0}, 120.0f);
  const linalg::quatf halfway = shared::transform_at(segment, 100, 130).orientation;
  const linalg::quatf expected = linalg::from_axis_angle({0, 1, 0}, 60.0f);
  check(std::fabs(std::fabs(linalg::dot(halfway, expected)) - 1.0f) < 1e-4f,
        "orientation slerps: half of a 120 degree turn is 60 degrees");
}

static void test_the_wait_is_the_clamp()
{
  printf("\n[pin] a wait holds the far node with no state saying so\n");

  shared::Entity_System system;
  const std::vector<shared::entity_uid_t> nodes =
      spawn_nodes(system, {{.position = {0, 0, 0}, .traversal_seconds = 1, .wait_seconds = 0.5f},
                           {.position = {10, 0, 0}},
                           {.position = {20, 0, 0}}});
  link(system, nodes[0], nodes[1]);
  link(system, nodes[1], nodes[2]);
  const shared::path_links_t links = shared::derive_path_links(system);

  entities::Path_Follow follow{.from = nodes[0], .segment_start_tick = 1, .direction = 1};
  std::vector<shared::entity_uid_t> reached;

  run_to(system, links, follow, 1, 61, reached);
  check(is_near(pose_position(system, links, follow, 61), {10, 0, 0}), "arrived after one second");
  run_to(system, links, follow, 62, 90, reached);
  check(follow.from == nodes[0] && reached.empty(), "still on the first segment while waiting");
  check(is_near(pose_position(system, links, follow, 90), {10, 0, 0}), "waiting holds the far node");

  run_to(system, links, follow, 91, 91, reached);
  check(follow.from == nodes[1] && reached.size() == 1 && reached[0] == nodes[1],
        "the node is reached when traversal plus wait has elapsed, once");
  check(follow.segment_start_tick == 91, "the next segment starts exactly where the wait ended");
  check(is_near(pose_position(system, links, follow, 121), {15, 0, 0}),
        "the next segment reads its own node's duration");
}

static void test_dead_ends_flip_at_both_ends()
{
  printf("\n[pin] an open chain oscillates, flipping at both ends\n");

  shared::Entity_System system;
  const std::vector<shared::entity_uid_t> nodes =
      spawn_nodes(system, {{.position = {0, 0, 0}, .traversal_seconds = 1, .wait_seconds = 1},
                           {.position = {10, 0, 0}, .traversal_seconds = 2}});
  link(system, nodes[0], nodes[1]);
  const shared::path_links_t links = shared::derive_path_links(system);

  entities::Path_Follow follow{.from = nodes[0], .segment_start_tick = 1, .direction = 1};
  std::vector<shared::entity_uid_t> reached;

  run_to(system, links, follow, 1, 121, reached);
  check(follow.from == nodes[1] && reached.size() == 1, "reached the far end");
  run_to(system, links, follow, 122, 122, reached);
  check(follow.direction == -1, "the far end has no next, so the mover turns back");
  check(is_near(pose_position(system, links, follow, 151), {5, 0, 0}),
        "the way back reads the first node's duration, not the dead end's");

  run_to(system, links, follow, 123, 241, reached);
  check(follow.from == nodes[0] && reached.size() == 2 && reached[1] == nodes[0],
        "back at the start after the same traversal plus the same wait");
  run_to(system, links, follow, 242, 242, reached);
  check(follow.direction == 1, "the start has no previous, so the mover turns forward again");
}

static void test_a_loop_never_flips()
{
  printf("\n[pin] a closed chain loops and never flips\n");

  shared::Entity_System system;
  const std::vector<shared::entity_uid_t> nodes = spawn_nodes(
      system, {{.position = {0, 0, 0}}, {.position = {10, 0, 0}}, {.position = {10, 0, 10}}});
  link(system, nodes[0], nodes[1]);
  link(system, nodes[1], nodes[2]);
  link(system, nodes[2], nodes[0]);
  const shared::path_links_t links = shared::derive_path_links(system);

  entities::Path_Follow follow{.from = nodes[0], .segment_start_tick = 1, .direction = 1};
  std::vector<shared::entity_uid_t> reached;
  run_to(system, links, follow, 1, 1 + 60 * 7, reached);

  bool order_holds = reached.size() == 7;
  for (size_t index = 0; order_holds && index < reached.size(); ++index)
    order_holds = reached[index] == nodes[(index + 1) % 3];
  check(order_holds, "seven segments visit the three nodes in chain order");
  check(follow.direction == 1, "the direction never changed");
}

static void test_previous_is_derived()
{
  printf("\n[pin] previous is derived, and null where two nodes name one\n");

  shared::Entity_System system;
  const std::vector<shared::entity_uid_t> nodes = spawn_nodes(
      system, {{.position = {0, 0, 0}}, {.position = {10, 0, 0}}, {.position = {0, 0, 10}},
               {.position = {10, 0, 10}}});
  link(system, nodes[0], nodes[1]);
  link(system, nodes[2], nodes[3]);
  link(system, nodes[3], nodes[1]);
  const shared::path_links_t links = shared::derive_path_links(system);

  check(shared::previous_node_of(links, nodes[3]) == nodes[2], "a single predecessor is the previous");
  check(shared::previous_node_of(links, nodes[0]) == shared::null_entity_uid,
        "a head has no previous");
  check(shared::previous_node_of(links, nodes[1]) == shared::null_entity_uid,
        "a node named by two has no single previous");
  check(links.named_by_several.size() == 1 && links.named_by_several[0] == nodes[1],
        "and is reported for the load to say so");

  entities::Path_Follow follow{.from = nodes[1], .segment_start_tick = 1, .direction = -1};
  std::vector<shared::entity_uid_t> reached;
  shared::advance_path_follow(system, links, follow, 1, TICKRATE, reached);
  check(follow.direction == -1 && follow.from == nodes[1],
        "an ambiguous node with no next either leaves the mover parked");
}

static void test_reverse_retraces_from_mid_segment()
{
  printf("\n[pin] Reverse retraces from where the mover is\n");

  shared::Entity_System system;
  const std::vector<shared::entity_uid_t> nodes = spawn_nodes(
      system, {{.position = {0, 0, 0}, .traversal_seconds = 1, .easing = entities::Easing::Smooth},
               {.position = {60, 0, 0}},
               {.position = {120, 0, 0}}});
  link(system, nodes[0], nodes[1]);
  link(system, nodes[1], nodes[2]);
  const shared::path_links_t links = shared::derive_path_links(system);

  entities::Path_Follow follow{.from = nodes[0], .segment_start_tick = 101, .direction = 1};
  const linalg::vec3f before = pose_position(system, links, follow, 121);

  check(shared::try_reverse_path_follow(system, links, follow, 121, TICKRATE), "Reverse is accepted");
  check(follow.from == nodes[1] && follow.direction == -1, "the mover now heads back to where it came from");
  check(is_near(pose_position(system, links, follow, 121), before), "reversing does not move it");
  check(is_near(pose_position(system, links, follow, 131),
             pose_position(system, links,
                           entities::Path_Follow{.from = nodes[0], .segment_start_tick = 101,
                                                 .direction = 1},
                           111)),
        "ten ticks later it is where it was ten ticks before the reverse");

  std::vector<shared::entity_uid_t> reached;
  run_to(system, links, follow, 122, 141, reached);
  check(reached.size() == 1 && reached[0] == nodes[0] && follow.from == nodes[0],
        "it reaches the start after the twenty ticks it had travelled");

  entities::Path_Follow go_to{.from = nodes[0], .segment_start_tick = 101, .direction = 1};
  check(shared::try_path_follow_go_to(system, links, go_to, nodes[1], 121, TICKRATE) &&
            go_to.from == nodes[0] && go_to.direction == 1,
        "Go_To the destination changes nothing");
  check(shared::try_path_follow_go_to(system, links, go_to, nodes[0], 121, TICKRATE) &&
            go_to.from == nodes[1] && go_to.direction == -1,
        "Go_To the node it left is a Reverse");
  check(!shared::try_path_follow_go_to(system, links, go_to, nodes[2], 121, TICKRATE),
        "Go_To a node that is not adjacent is refused");
}

static void test_a_pose_reads_past_an_unadvanced_boundary()
{
  printf("\n[pin] a pose past the segment end is the pose the advanced follow gives\n");

  shared::Entity_System system;
  const std::vector<shared::entity_uid_t> nodes =
      spawn_nodes(system, {{.position = {0, 0, 0}}, {.position = {60, 0, 0}}, {.position = {60, 60, 0}}});
  link(system, nodes[0], nodes[1]);
  link(system, nodes[1], nodes[2]);
  const shared::path_links_t links = shared::derive_path_links(system);

  const entities::Path_Follow written{.from = nodes[0], .segment_start_tick = 1, .direction = 1};
  entities::Path_Follow advanced = written;
  std::vector<shared::entity_uid_t> reached;
  run_to(system, links, advanced, 1, 91, reached);

  check(advanced.from == nodes[1], "the server's follow has moved on to the second segment");
  check(is_near(pose_position(system, links, written, 91), {60, 30, 0}),
        "the follow as the snapshot left it reads halfway up the second segment");
  check(is_near(pose_position(system, links, written, 91), pose_position(system, links, advanced, 91)),
        "and agrees with the advanced one, so a client predicts across the boundary");
}

static void test_a_frozen_mover_holds_and_resumes_where_it_stopped()
{
  printf("\n[pin] a switched-off mover holds its pose and resumes without a jump\n");

  shared::Entity_System system;
  const std::vector<shared::entity_uid_t> nodes =
      spawn_nodes(system, {{.position = {0, 0, 0}}, {.position = {60, 0, 0}}});
  link(system, nodes[0], nodes[1]);
  const shared::path_links_t links = shared::derive_path_links(system);

  entities::Path_Follow follow{.from = nodes[0], .segment_start_tick = 1, .direction = 1};
  shared::freeze_path_follow(follow, 21);

  check(is_near(pose_position(system, links, follow, 21), {20, 0, 0}), "frozen at tick 21, 20 ticks in");
  check(is_near(pose_position(system, links, follow, 500), {20, 0, 0}), "and still there 479 ticks later");

  std::vector<shared::entity_uid_t> reached;
  run_to(system, links, follow, 22, 500, reached);
  check(reached.empty(), "advancing a frozen follow by its clock reaches nothing");

  shared::resume_path_follow(follow, 500);
  check(follow.frozen_at_tick == 0, "resuming clears the freeze");
  check(is_near(pose_position(system, links, follow, 500), {20, 0, 0}), "the resume tick holds the frozen pose");
  check(is_near(pose_position(system, links, follow, 510), {30, 0, 0}), "and ten ticks later it has moved ten");
}

int main()
{
  test_linear_and_smooth();
  test_the_wait_is_the_clamp();
  test_dead_ends_flip_at_both_ends();
  test_a_loop_never_flips();
  test_previous_is_derived();
  test_reverse_retraces_from_mid_segment();
  test_a_pose_reads_past_an_unadvanced_boundary();
  test_a_frozen_mover_holds_and_resumes_where_it_stopped();

  printf(failure_count == 0 ? "\nmover_path_test: all passed\n"
                            : "\nmover_path_test: %d FAILED\n",
         failure_count);
  return failure_count == 0 ? 0 : 1;
}
