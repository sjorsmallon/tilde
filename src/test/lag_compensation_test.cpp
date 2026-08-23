// Rewinding the hit test to what the shooter saw — shared/lag_compensation.cpp.
//
// Two halves, and they are separate on purpose:
//
//   classify_bracket              pure arithmetic over what the server knows
//                                 about a client. No rig, no assets.
//   try_pose_players_across_bracket   reads two snapshot frames and poses the
//                                 REAL rig through the blend between them.
//
// The posing half is checked by building a reference pose with
// compute_player_hitboxes directly and comparing volume for volume, rather than
// by re-deriving positions here. That is the point: the rewound silhouette must
// be the one the live path and the client's overlay draw, so the assertion is
// literally "these are the same volumes".
//
// Run from the project root — player_rig() loads resources/models (ctest pins
// WORKING_DIRECTORY for exactly this).
//
// NOT covered here, and it is the one bullet of lag_compensation_def.md §4 that
// is missing: the mutual-trade case (two shooters resolving against each other
// in one tick both land damage). That fix is the input loop deferring damage to
// a pass after itself, and the input loop is Tick() in server_impl.cpp — inside
// the game_server DLL, behind no exported entry point, needing Jolt, a map and
// a socket to reach. There is no function to call. It is listed in todo.md as
// the successor item, and the live check in lag_compensation_def.md §5 is what
// exercises it today.

#include "../shared/asset.hpp"
#include "../shared/hitscan.hpp"
#include "../shared/lag_compensation.hpp"
#include "../shared/network/entity_snapshot.hpp"
#include "../shared/network/snapshot_history.hpp"
#include "../shared/player_rig.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using linalg::vec3f;
using shared::bracket_status_t;
using shared::interpolation_bracket_t;
using shared::posed_players_t;

static int failures = 0;

static void check(bool condition, const char* what)
{
  if (condition)
  {
    printf("  ok   %s\n", what);
  }
  else
  {
    printf("  FAIL %s\n", what);
    ++failures;
  }
}

// --- fixtures ----------------------------------------------------------------

using history_t = network::Snapshot_History<network::snapshot_frame_t>;

static entities::Player_Entity make_player(shared::entity_uid_t uid, vec3f position,
                                           float yaw, int32_t health = 100)
{
  entities::Player_Entity player{};
  player.entity_id        = uid;
  player.position         = position;
  player.body_yaw         = yaw;
  player.view_angle_yaw   = yaw;
  player.view_angle_pitch = 0.f;
  player.health           = health;
  return player;
}

// Stores `players` as the frame for `tick`. Nothing here goes near the wire —
// the ring holds whole values on both ends, which is what makes a rewind a
// lookup rather than a replay.
static void store_frame(history_t& history, uint32_t tick,
                        const std::vector<entities::Player_Entity>& players)
{
  network::snapshot_frame_t frame;
  frame.tick = tick;
  for (const entities::Player_Entity& player : players)
    frame.players[player.entity_id] = player;
  history.slot_for(tick) = frame;
}

// The volumes compute_player_hitboxes places for `pose`, straight from the live
// path's own entry point. Every posing assertion below compares against one of
// these.
static std::vector<assets::posed_hitbox_t> reference_volumes(const shared::player_rig_t& rig,
                                                             const aim_settings_t& settings,
                                                             const shared::player_pose_t& pose)
{
  std::vector<assets::posed_hitbox_t> volumes(rig.volume_count());
  shared::compute_player_hitboxes(rig, pose, settings, volumes);
  return volumes;
}

// Are these the same placed volumes? Compared on the endpoints, which is where
// every pose input lands; the shapes and radii are the rig's and cannot differ.
static bool same_volumes(Span<const assets::posed_hitbox_t> a,
                         Span<const assets::posed_hitbox_t> b, float tolerance = 0.01f)
{
  if (a.size() != b.size())
    return false;
  for (size_t index = 0; index < a.size(); ++index)
  {
    const vec3f start_delta = a[index].start - b[index].start;
    const vec3f end_delta   = a[index].end - b[index].end;
    if (linalg::length(start_delta) > tolerance || linalg::length(end_delta) > tolerance)
      return false;
  }
  return true;
}

static const shared::hitscan_target_t* find_target(const posed_players_t& posed,
                                                   shared::entity_uid_t uid)
{
  for (const shared::hitscan_target_t& target : posed.targets)
    if (target.uid == uid)
      return &target;
  return nullptr;
}

// --- 1. classify_bracket -----------------------------------------------------
//
// No rig, no history: this half decides only whether a REQUEST is one an honest
// client on this connection could have made.

static void test_classify_bracket()
{
  printf("classify_bracket\n");

  constexpr uint32_t held    = 300;
  constexpr uint32_t current = 305;
  constexpr uint32_t max_rewind = 12;

  {
    const shared::bracket_verdict_t verdict =
        shared::classify_bracket({299, 300, 0.4f}, held, current, max_rewind);
    check(verdict.status == bracket_status_t::Ok && verdict.bracket.from_tick == 299 &&
              verdict.bracket.towards_tick == 300 &&
              std::abs(verdict.bracket.fraction - 0.4f) < 1e-6f,
          "an in-range bracket passes through untouched");
  }

  // Either endpoint at 0 is the routine "no blend yet" case, not a complaint:
  // a spectator, or a client that has seen fewer than two snapshots.
  check(shared::classify_bracket({0, 0, 0.f}, held, current, max_rewind).status ==
            bracket_status_t::Absent,
        "a zeroed bracket is Absent, not an error");
  check(shared::classify_bracket({0, 300, 0.5f}, held, current, max_rewind).status ==
            bracket_status_t::Absent,
        "only one snapshot seen (from_tick 0) is Absent");

  // THE fabricated-bracket check. The ring bounds alone would happily accept a
  // tick the server sent to nobody, so this is the one that ties the bracket to
  // what the client demonstrably received.
  check(shared::classify_bracket({300, 301, 0.5f}, held, current, max_rewind).status ==
            bracket_status_t::Unheld,
        "a bracket reaching past held_snapshot_tick is refused");
  check(shared::classify_bracket({300, 400, 0.5f}, /*held*/ 400, current, max_rewind).status ==
            bracket_status_t::Unheld,
        "a bracket naming a tick the server has not reached is refused");

  // Malformed, and rejected rather than repaired: a bracket the server has to
  // fix up is one it cannot claim to reproduce.
  check(shared::classify_bracket({300, 299, 0.5f}, held, current, max_rewind).status ==
            bracket_status_t::Malformed,
        "towards older than from is rejected");
  check(shared::classify_bracket({299, 300, 1.5f}, held, current, max_rewind).status ==
            bracket_status_t::Malformed,
        "a fraction above 1 is rejected");
  check(shared::classify_bracket({299, 300, -0.1f}, held, current, max_rewind).status ==
            bracket_status_t::Malformed,
        "a negative fraction is rejected");
  check(shared::classify_bracket({299, 300, std::nanf("")}, held, current, max_rewind).status ==
            bracket_status_t::Malformed,
        "a NaN fraction is rejected");

  // The policy clamp: pinned to the boundary, never rejected, and never silent
  // (the caller logs it — see get_interpolation_bracket_for_move).
  {
    // Only `from` is past the limit (the boundary is current - max_rewind =
    // 293), so `towards` is left where the client put it.
    const shared::bracket_verdict_t verdict =
        shared::classify_bracket({290, 300, 0.5f}, held, current, max_rewind);
    check(verdict.status == bracket_status_t::Clamped &&
              verdict.bracket.from_tick == current - max_rewind &&
              verdict.bracket.towards_tick == 300,
          "a from_tick past the rewind limit is pinned to the boundary");
  }
  {
    // Both endpoints past the limit: the blend collapses onto the oldest tick
    // the server is willing to be judged against.
    const shared::bracket_verdict_t verdict =
        shared::classify_bracket({250, 260, 0.5f}, /*held*/ 260, current, max_rewind);
    check(verdict.status == bracket_status_t::Clamped &&
              verdict.bracket.from_tick == current - max_rewind &&
              verdict.bracket.towards_tick == current - max_rewind,
          "both endpoints past the limit collapse onto the boundary tick");
  }
  {
    // Early in a server's life the limit reaches past tick 1, which does not
    // exist. Ticks start at 1 because 0 is the "no baseline" sentinel.
    const shared::bracket_verdict_t verdict =
        shared::classify_bracket({1, 2, 0.5f}, /*held*/ 2, /*current*/ 5, max_rewind);
    check(verdict.status == bracket_status_t::Ok && verdict.bracket.from_tick == 1,
          "the clamp does not wrap below tick 1 on a young server");
  }
}

// --- 2. the blend ------------------------------------------------------------

static void test_blend_across_the_bracket(const shared::player_rig_t& rig,
                                          const aim_settings_t& settings)
{
  printf("try_pose_players_across_bracket\n");

  constexpr shared::entity_uid_t UID = 7;
  const vec3f from_position{0.f, 0.f, 0.f};
  const vec3f towards_position{100.f, 0.f, 40.f};

  history_t history;
  store_frame(history, 300, {make_player(UID, from_position, 0.f)});
  store_frame(history, 304, {make_player(UID, towards_position, 90.f)});

  posed_players_t posed;

  // Fractions 0 / 0.5 / 1, each against the pose the live path would build for
  // the same inputs.
  const struct
  {
    float fraction;
    vec3f position;
    float yaw;
    const char* what;
  } cases[] = {
      {0.f, from_position, 0.f, "fraction 0 lands exactly on the `from` frame"},
      {0.5f, {50.f, 0.f, 20.f}, 45.f, "fraction 0.5 lands halfway between the endpoints"},
      {1.f, towards_position, 90.f, "fraction 1 lands exactly on the `towards` frame"},
  };

  for (const auto& one : cases)
  {
    check(shared::try_pose_players_across_bracket(history, rig, settings,
                                                  {300, 304, one.fraction}, posed),
          "both endpoints resolve in the ring");

    const shared::hitscan_target_t* target = find_target(posed, UID);
    const std::vector<assets::posed_hitbox_t> expected = reference_volumes(
        rig, settings,
        {.feet_position = one.position, .body_yaw = one.yaw, .view_yaw = one.yaw});
    check(target != nullptr && same_volumes(target->hitboxes, expected), one.what);
  }

  // --- a fraction outside [0,1] PINS, it does not extrapolate ---------------
  // classify_bracket rejects one as Malformed before the fire path ever gets
  // here, but this is a separate public call and the defensive floor is the
  // point: posing past the newer endpoint puts limbs where no snapshot ever had
  // them -- through walls, under jitter, with nothing logged. shared::lerp is
  // unclamped, so this holds only while the blend goes through the _clamped
  // helpers.
  {
    const struct
    {
      float       fraction;
      vec3f       position;
      float       yaw;
      const char* what;
    } out_of_range[] = {
        {1.5f, towards_position, 90.f, "fraction past 1 pins to the `towards` frame"},
        {-0.5f, from_position, 0.f, "fraction below 0 pins to the `from` frame"},
    };

    for (const auto& one : out_of_range)
    {
      check(shared::try_pose_players_across_bracket(history, rig, settings,
                                                    {300, 304, one.fraction}, posed),
            "both endpoints resolve in the ring");

      const shared::hitscan_target_t*            target = find_target(posed, UID);
      const std::vector<assets::posed_hitbox_t> expected = reference_volumes(
          rig, settings,
          {.feet_position = one.position, .body_yaw = one.yaw, .view_yaw = one.yaw});
      check(target != nullptr && same_volumes(target->hitboxes, expected), one.what);
    }

    // Named explicitly rather than left implied by the pin above: this is the
    // position an unclamped lerp would have produced, and it must not appear.
    const shared::hitscan_target_t*            target = find_target(posed, UID);
    const std::vector<assets::posed_hitbox_t> extrapolated = reference_volumes(
        rig, settings,
        {.feet_position = {-50.f, 0.f, -20.f}, .body_yaw = -45.f, .view_yaw = -45.f});
    check(target != nullptr && !same_volumes(target->hitboxes, extrapolated),
          "...and specifically NOT extrapolated past the endpoint");
  }

  // --- the +/-180 seam -----------------------------------------------------
  // A raw lerp across it spins the model all the way back, which is once per
  // revolution for a continuously turning bot. The client's own interpolation
  // takes the short way; so must this, or the volumes stop being the ones drawn.
  {
    history_t seam_history;
    store_frame(seam_history, 10, {make_player(UID, from_position, 170.f)});
    store_frame(seam_history, 11, {make_player(UID, from_position, -170.f)});

    check(shared::try_pose_players_across_bracket(seam_history, rig, settings, {10, 11, 0.5f},
                                                  posed),
          "the seam fixture resolves");

    const shared::hitscan_target_t* target = find_target(posed, UID);
    const std::vector<assets::posed_hitbox_t> short_way = reference_volumes(
        rig, settings,
        {.feet_position = from_position, .body_yaw = 180.f, .view_yaw = 180.f});
    const std::vector<assets::posed_hitbox_t> long_way = reference_volumes(
        rig, settings, {.feet_position = from_position, .body_yaw = 0.f, .view_yaw = 0.f});

    check(target != nullptr && same_volumes(target->hitboxes, short_way),
          "yaw across the +/-180 seam takes the short way round");
    check(target != nullptr && !same_volumes(target->hitboxes, long_way),
          "...and specifically NOT the long way, which is the bug being guarded");
  }

  // --- membership comes from the `from` frame ------------------------------
  {
    history_t despawn_history;
    store_frame(despawn_history, 20, {make_player(UID, from_position, 0.f)});
    store_frame(despawn_history, 21, {}); // despawned mid-blend

    check(shared::try_pose_players_across_bracket(despawn_history, rig, settings,
                                                  {20, 21, 0.5f}, posed),
          "a frame with no players still resolves");

    const shared::hitscan_target_t* target = find_target(posed, UID);
    const std::vector<assets::posed_hitbox_t> expected = reference_volumes(
        rig, settings, {.feet_position = from_position, .body_yaw = 0.f, .view_yaw = 0.f});
    check(target != nullptr && same_volumes(target->hitboxes, expected),
          "a uid present only in the `from` frame is posed from it, not skipped");
  }

  {
    history_t corpse_history;
    store_frame(corpse_history, 30, {make_player(UID, from_position, 0.f, /*health*/ 0)});
    store_frame(corpse_history, 31, {make_player(UID, from_position, 0.f, /*health*/ 0)});

    check(shared::try_pose_players_across_bracket(corpse_history, rig, settings,
                                                  {30, 31, 0.5f}, posed),
          "a corpse-only fixture resolves");
    check(find_target(posed, UID) == nullptr && posed.targets.empty(),
          "a uid dead in the `from` frame is skipped");
  }

  // --- ring misses ---------------------------------------------------------
  // The one ordinary failure of the four checks: a client stalled past the
  // ring's ~530ms lands here legitimately, and the caller falls back to
  // present-tick poses rather than substituting a nearby tick.
  check(!shared::try_pose_players_across_bracket(history, rig, settings, {299, 304, 0.5f},
                                                 posed),
        "a `from` tick that never entered the ring is declined");
  check(!shared::try_pose_players_across_bracket(history, rig, settings, {300, 999, 0.5f},
                                                 posed),
        "a `towards` tick that never entered the ring is declined");
  {
    // Aged out rather than never stored: the ring is 32 deep, so writing tick
    // 332 overwrites the slot 300 lives in.
    history_t aged = history;
    store_frame(aged, 300 + history_t::CAPACITY, {make_player(UID, from_position, 0.f)});
    check(!shared::try_pose_players_across_bracket(aged, rig, settings, {300, 304, 0.5f},
                                                   posed),
          "an endpoint overwritten by a newer tick is declined");
  }
}

// --- 3. the chord, not the truth ---------------------------------------------
//
// The test the whole wire format exists for, and the guard against re-deriving
// the collapsed-moment design (see lag_compensation_def.md's status block).
//
// A client holding only 300 and 304 DREW A STRAIGHT LINE between them. The true
// tick-301 state is not on that line whenever the target strafed. If the server
// poses the truth instead of the chord, the shot misses a crosshair that was
// dead on the drawn model — the exact "I hit that, no reg" this all exists to
// kill.

static void test_the_chord_not_the_truth(const shared::player_rig_t& rig,
                                         const aim_settings_t& settings)
{
  printf("the chord, not the truth\n");

  constexpr shared::entity_uid_t UID = 3;

  // A hard strafe: out to +Z and back. The endpoints sit at z = 0, so the CHORD
  // is the z = 0 line while every tick between it bulges 60 units off that.
  const vec3f path[] = {
      {0.f, 0.f, 0.f},    // 300
      {25.f, 0.f, 60.f},  // 301
      {50.f, 0.f, 80.f},  // 302
      {75.f, 0.f, 60.f},  // 303
      {100.f, 0.f, 0.f},  // 304
  };

  history_t history;
  for (uint32_t offset = 0; offset < 5; ++offset)
    store_frame(history, 300 + offset, {make_player(UID, path[offset], 0.f)});

  posed_players_t posed;
  check(shared::try_pose_players_across_bracket(history, rig, settings, {300, 304, 0.25f},
                                                posed),
        "the bracket the client actually held resolves");

  const shared::hitscan_target_t* target = find_target(posed, UID);

  // 0.25 of the way from 300 to 304 — which is where tick 301 sits in TIME, and
  // 60 units away from where the target really was.
  const std::vector<assets::posed_hitbox_t> on_the_chord = reference_volumes(
      rig, settings, {.feet_position = {25.f, 0.f, 0.f}, .body_yaw = 0.f, .view_yaw = 0.f});
  const std::vector<assets::posed_hitbox_t> the_truth = reference_volumes(
      rig, settings, {.feet_position = path[1], .body_yaw = 0.f, .view_yaw = 0.f});

  check(target != nullptr && same_volumes(target->hitboxes, on_the_chord),
        "the rewound pose lands on the straight line the client drew");
  check(target != nullptr && !same_volumes(target->hitboxes, the_truth),
        "...and NOT on the stored tick-301 state, 60 units off that line");
}

// --- 4. end to end -----------------------------------------------------------
//
// The case that motivates the whole change, and its converse. One-sidedness is
// the failure mode worth guarding: a rewind that only ever adds hits is not a
// rewind, it is a wallhack.

static void test_hit_then_and_not_now(const shared::player_rig_t& rig,
                                      const aim_settings_t& settings)
{
  printf("end to end: a target crossing at speed\n");

  constexpr shared::entity_uid_t UID = 11;

  // Then: standing at the origin. Now: 120 units down +Z, well clear of its own
  // silhouette. Both facing the shooter, who is 300 units down -X.
  const vec3f then_position{0.f, 0.f, 0.f};
  const vec3f now_position{0.f, 0.f, 120.f};
  constexpr float FACING = 180.f;

  history_t history;
  store_frame(history, 100, {make_player(UID, then_position, FACING)});
  store_frame(history, 101, {make_player(UID, then_position, FACING)});

  const std::vector<assets::posed_hitbox_t> now_volumes = reference_volumes(
      rig, settings,
      {.feet_position = now_position, .body_yaw = FACING, .view_yaw = FACING});
  const std::vector<shared::hitscan_target_t> present_targets{{UID, now_volumes}};

  posed_players_t rewound;
  check(shared::try_pose_players_across_bracket(history, rig, settings, {100, 101, 0.5f},
                                                rewound),
        "the shooter's bracket resolves");

  // A torso-height ray at each place. Reading the height off a posed volume
  // keeps this a test of the path rather than of the authored rig numbers.
  float torso_height = 0.f;
  for (const assets::posed_hitbox_t& volume : now_volumes)
    if (volume.region == shared::hit_region_t::Torso)
    {
      torso_height = 0.5f * (volume.start.y + volume.end.y);
      break;
    }

  const vec3f toward_plus_x{1.f, 0.f, 0.f};
  const vec3f at_then{-300.f, torso_height, then_position.z};
  const vec3f at_now{-300.f, torso_height, now_position.z};

  check(shared::resolve_hitscan(at_then, toward_plus_x, 1000.f, present_targets).hit_uid == 0,
        "aiming where the shooter SAW the target misses the present-tick pose");
  check(shared::resolve_hitscan(at_then, toward_plus_x, 1000.f, rewound.targets).hit_uid == UID,
        "...and hits the rewound one — the whole point");

  // The converse. Without it the change is one-sided: rewinding would only ever
  // add hits, never move them.
  check(shared::resolve_hitscan(at_now, toward_plus_x, 1000.f, present_targets).hit_uid == UID,
        "aiming where the target IS hits the present-tick pose");
  check(shared::resolve_hitscan(at_now, toward_plus_x, 1000.f, rewound.targets).hit_uid == 0,
        "...and misses the rewound one — a rewind moves hits, it does not add them");
}

int main()
{
  printf("lag_compensation_test\n");

  test_classify_bracket();

  // player_rig() goes through the asset cache, which reads through the byte
  // layer; one state for the whole file, mounted as a launcher would.
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::mount_asset_source();

  const shared::player_rig_t& rig = shared::player_rig();
  const aim_settings_t        settings;

  test_blend_across_the_bracket(rig, settings);
  test_the_chord_not_the_truth(rig, settings);
  test_hit_then_and_not_now(rig, settings);

  printf(failures == 0 ? "lag_compensation_test PASSED\n"
                       : "lag_compensation_test FAILED (%d)\n",
         failures);
  return failures == 0 ? 0 : 1;
}
