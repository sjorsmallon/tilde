// resolve_hitscan coverage.
//
// Pure-function tests: no server, no session, and deliberately no
// physics_state_t. The world cast lives in the CALLER (see hitscan.hpp), so
// "a wall blocks the shot" is expressed here as a shorter max_range down the
// exact same code path the real fire path takes -- which is the whole reason
// the split exists.

#include "../shared/hitscan.hpp"
#include "../shared/player_hitboxes.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace shared;

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

// The hitbox table is measured from the FEET and tiles the 72-tall movement
// hull, so a target at the origin has legs 0..30, torso 30..54 and a head
// sphere centered at 63 with radius 9.
constexpr float torso_y = 42.f;
constexpr float head_y  = 63.f;
constexpr float legs_y  = 15.f;

int main()
{
  printf("hitscan_test\n");

  // --- regions -------------------------------------------------------------
  // Fire horizontally along +X at each region's height. Each must report that
  // region and no other: the guard against "first entry in the table wins"
  // instead of nearest.
  {
    const std::vector<hitscan_target_t> targets{{1, {200.f, 0.f, 0.f}}};

    const hitscan_result_t head =
        resolve_hitscan({0.f, head_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, targets);
    check(head.hit_uid == 1 && head.region == hit_region_t::Head, "ray at 63 hits Head");

    const hitscan_result_t torso =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, targets);
    check(torso.hit_uid == 1 && torso.region == hit_region_t::Torso, "ray at 42 hits Torso");

    const hitscan_result_t legs =
        resolve_hitscan({0.f, legs_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, targets);
    check(legs.hit_uid == 1 && legs.region == hit_region_t::Legs, "ray at 15 hits Legs");
  }

  // Torso from behind resolves the same -- the boxes are not yaw-rotated, so
  // direction of approach must not change the region.
  {
    const std::vector<hitscan_target_t> targets{{1, {200.f, 0.f, 0.f}}};
    const hitscan_result_t behind =
        resolve_hitscan({400.f, torso_y, 0.f}, {-1.f, 0.f, 0.f}, 1000.f, targets);
    check(behind.hit_uid == 1 && behind.region == hit_region_t::Torso,
          "torso hit from behind is still Torso");
  }

  // --- nearest wins --------------------------------------------------------
  // Two targets in line. The far one is listed FIRST so a naive
  // first-hit-wins loop would return it.
  {
    const std::vector<hitscan_target_t> targets{{7, {600.f, 0.f, 0.f}},
                                                {3, {200.f, 0.f, 0.f}}};
    const hitscan_result_t hit =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, targets);
    check(hit.hit_uid == 3, "nearer of two in-line targets wins regardless of list order");
    check(std::abs(hit.distance - 186.f) < 1.f, "reported distance is to the near target");
  }

  // --- range / wall clamp --------------------------------------------------
  // Both cases are the same mechanism: max_range shorter than the target.
  // In the real path the wall value comes from the caller's BVH cast.
  {
    const std::vector<hitscan_target_t> targets{{1, {200.f, 0.f, 0.f}}};

    const hitscan_result_t walled =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 100.f, targets);
    check(walled.hit_uid == 0, "wall between origin and target blocks the shot");

    const hitscan_result_t out_of_range =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 50.f, targets);
    check(out_of_range.hit_uid == 0, "target beyond max_range is a miss");

    // A clamp that lands just past the near face must still connect --
    // guards an off-by-one that would make every wall-adjacent shot miss.
    const hitscan_result_t grazing =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 190.f, targets);
    check(grazing.hit_uid == 1, "clamp just past the near face still hits");
  }

  // --- degenerate origins --------------------------------------------------
  {
    // Muzzle already inside the torso: must miss rather than report an impact
    // behind the shooter at a negative distance.
    const std::vector<hitscan_target_t> targets{{1, {0.f, 0.f, 0.f}}};
    const hitscan_result_t inside =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, targets);
    check(inside.hit_uid == 0, "origin inside a hitbox misses, no negative distance");
  }

  {
    // The corpse case: a player omitted from `targets` is not merely
    // undamageable, the ray passes straight through them.
    const std::vector<hitscan_target_t> targets{{9, {600.f, 0.f, 0.f}}};
    const hitscan_result_t through =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, targets);
    check(through.hit_uid == 9,
          "a target absent from the list does not block the one behind it");
  }

  // Empty target list is a clean miss, not a crash.
  {
    const hitscan_result_t nobody =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, {});
    check(nobody.hit_uid == 0, "empty target list misses");
  }

  // --- impact data ---------------------------------------------------------
  {
    const std::vector<hitscan_target_t> targets{{1, {200.f, 0.f, 0.f}}};
    const hitscan_result_t hit =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, targets);
    check(hit.impact_normal.x < -0.9f, "impact normal faces back along the ray");
    check(std::abs(hit.impact_point.y - torso_y) < 0.01f,
          "impact point stays on the ray");
  }

  printf(failures == 0 ? "hitscan_test PASSED\n" : "hitscan_test FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}
