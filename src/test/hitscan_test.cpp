// resolve_hitscan coverage, and the ray-vs-volume math under it.
//
// Pure-function tests: no server, no session, no physics_state_t, and no
// skeleton. The volumes here are HAND-BUILT rather than posed off `rig.hitboxes`
// -- resolve_hitscan takes placed volumes precisely so it does not need a rig,
// and a test that loaded one would be testing the content of a model file. What
// places real volumes is shared::compute_player_hitboxes; what this covers is
// everything downstream of it.
//
// The world cast lives in the CALLER (see hitscan.hpp), so "a wall blocks the
// shot" is expressed here as a shorter max_range down the exact same code path
// the real fire path takes -- which is the whole reason the split exists.

#include "../shared/hitscan.hpp"
#include "../shared/hit_region.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace shared;
using assets::hitbox_shape_t;
using assets::posed_hitbox_t;

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

// A stand-in player at `feet`, with one volume of each shape so every branch of
// intersect_ray_hitbox is on a ray somewhere below. Dimensions echo the movement
// hull (32 x 32 x 72, origin at the feet) so the heights read the way they do
// everywhere else, but nothing here is the authored rig.
constexpr float head_y  = 63.f;
constexpr float torso_y = 42.f;
constexpr float legs_y  = 15.f;
// The arm points along +Z, clear of the body, so a ray at its height hits only
// it.
constexpr float arm_y      = 50.f;
constexpr float arm_near_z = 10.f;
constexpr float arm_far_z  = 20.f;
constexpr float arm_radius = 3.f;

static std::vector<posed_hitbox_t> make_volumes(linalg::vec3f feet,
                                                hitbox_shape_t arm_shape = hitbox_shape_t::Cylinder)
{
  return {
      // Head: a sphere on the vertical axis.
      posed_hitbox_t{.shape  = hitbox_shape_t::Sphere,
                     .start  = feet + linalg::vec3f{0.f, head_y, 0.f},
                     .end    = feet + linalg::vec3f{0.f, head_y, 0.f},
                     .radius = 9.f,
                     .region = hit_region_t::Head},
      // Torso: a box spanning 30..54, in the default (world-axis) frame.
      posed_hitbox_t{.shape        = hitbox_shape_t::Box,
                     .start        = feet + linalg::vec3f{0.f, 30.f, 0.f},
                     .end          = feet + linalg::vec3f{0.f, 54.f, 0.f},
                     .half_extents = {14.f, 12.f, 14.f},
                     .region       = hit_region_t::Torso},
      // Legs: a capsule, so the round caps are on a ray too.
      posed_hitbox_t{.shape  = hitbox_shape_t::Capsule,
                     .start  = feet + linalg::vec3f{0.f, 4.f, 0.f},
                     .end    = feet + linalg::vec3f{0.f, 26.f, 0.f},
                     .radius = 8.f,
                     .region = hit_region_t::Legs},
      // An arm out along +Z. Cylinder by default; the caller flips it to Capsule
      // to compare flat caps against round ones.
      posed_hitbox_t{.shape  = arm_shape,
                     .start  = feet + linalg::vec3f{0.f, arm_y, arm_near_z},
                     .end    = feet + linalg::vec3f{0.f, arm_y, arm_far_z},
                     .radius = arm_radius,
                     .region = hit_region_t::Torso},
  };
}

int main()
{
  printf("hitscan_test\n");

  const std::vector<posed_hitbox_t> volumes = make_volumes({200.f, 0.f, 0.f});
  const std::vector<hitscan_target_t> targets{{1, volumes}};

  // --- regions -------------------------------------------------------------
  // Fire horizontally along +X at each region's height. Each must report that
  // region and no other: the guard against "first volume in the list wins"
  // instead of nearest.
  {
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

  // The arm is a separate VOLUME costing Torso damage -- ten volumes, three
  // regions, and this is the pair coming apart.
  {
    const hitscan_result_t arm =
        resolve_hitscan({0.f, arm_y, arm_near_z + 2.f}, {1.f, 0.f, 0.f}, 1000.f, targets);
    check(arm.hit_uid == 1 && arm.region == hit_region_t::Torso,
          "a limb volume off the body axis is hit, and costs Torso");
  }

  // Torso from behind resolves the same: the volume is symmetric about the
  // approach axis, so direction must not change the region.
  {
    const hitscan_result_t behind =
        resolve_hitscan({400.f, torso_y, 0.f}, {-1.f, 0.f, 0.f}, 1000.f, targets);
    check(behind.hit_uid == 1 && behind.region == hit_region_t::Torso,
          "torso hit from behind is still Torso");
  }

  // --- nearest wins --------------------------------------------------------
  // Two targets in line. The far one is listed FIRST so a naive
  // first-hit-wins loop would return it.
  {
    const std::vector<posed_hitbox_t> far_volumes = make_volumes({600.f, 0.f, 0.f});
    const std::vector<hitscan_target_t> two{{7, far_volumes}, {3, volumes}};

    const hitscan_result_t hit =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, two);
    check(hit.hit_uid == 3, "nearer of two in-line targets wins regardless of list order");
    check(std::abs(hit.distance - 186.f) < 1.f, "reported distance is to the near target");
  }

  // --- range / wall clamp --------------------------------------------------
  // Both cases are the same mechanism: max_range shorter than the target.
  // In the real path the wall value comes from the caller's BVH cast.
  {
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
  // Every shape reports its ENTRY point, so a muzzle already inside one misses
  // rather than reporting an impact behind the shooter. One case per shape,
  // because each has its own quadratic to get the sign wrong in.
  {
    const std::vector<posed_hitbox_t> here = make_volumes({0.f, 0.f, 0.f});
    const std::vector<hitscan_target_t> inside_targets{{1, here}};

    const hitscan_result_t in_box =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, inside_targets);
    check(in_box.hit_uid == 0, "origin inside the box volume misses");

    const hitscan_result_t in_sphere =
        resolve_hitscan({0.f, head_y, 0.f}, {0.f, 1.f, 0.f}, 1000.f, inside_targets);
    check(in_sphere.hit_uid == 0, "origin inside the sphere volume misses");

    const hitscan_result_t in_capsule =
        resolve_hitscan({0.f, legs_y, 0.f}, {0.f, 0.f, 1.f}, 1000.f, inside_targets);
    check(in_capsule.hit_uid == 0, "origin inside the capsule volume misses");

    const hitscan_result_t in_cylinder = resolve_hitscan(
        {0.f, arm_y, arm_near_z + 5.f}, {0.f, 0.f, 1.f}, 1000.f, inside_targets);
    check(in_cylinder.hit_uid == 0, "origin inside the cylinder volume misses");
  }

  {
    // The corpse case: a player omitted from `targets` is not merely
    // undamageable, the ray passes straight through them.
    const std::vector<posed_hitbox_t> far_volumes = make_volumes({600.f, 0.f, 0.f});
    const std::vector<hitscan_target_t> only_far{{9, far_volumes}};
    const hitscan_result_t through =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, only_far);
    check(through.hit_uid == 9,
          "a target absent from the list does not block the one behind it");
  }

  // Empty target list is a clean miss, not a crash.
  {
    const hitscan_result_t nobody =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, {});
    check(nobody.hit_uid == 0, "empty target list misses");
  }

  // --- ignore_uid ----------------------------------------------------------
  // The shooter is IN the list: the server poses every player once per tick and
  // every shot that tick shares that one list, so skipping yourself is this
  // parameter's job rather than the caller's filter. Two things to get wrong --
  // failing to skip, and skipping so eagerly that the shot stops there.
  {
    const std::vector<posed_hitbox_t>   shooter_volumes = make_volumes({0.f, 0.f, 0.f});
    const std::vector<posed_hitbox_t>   far_volumes     = make_volumes({600.f, 0.f, 0.f});
    const std::vector<hitscan_target_t> with_shooter{{5, shooter_volumes}, {9, far_volumes}};

    // ONE ray for both halves, fired from outside the shooter's own volumes and
    // straight through them at the far target. Firing from inside them would
    // pass either way -- every shape reports its ENTRY point, so an origin
    // within a volume misses on its own.
    const linalg::vec3f through_self{-100.f, torso_y, 0.f};
    const linalg::vec3f toward_far{1.f, 0.f, 0.f};

    const hitscan_result_t unignored =
        resolve_hitscan(through_self, toward_far, 1000.f, with_shooter);
    check(unignored.hit_uid == 5,
          "without ignore_uid the shooter's own volumes are squarely in the way");

    const hitscan_result_t ignored =
        resolve_hitscan(through_self, toward_far, 1000.f, with_shooter, 5);
    check(ignored.hit_uid == 9,
          "ignore_uid skips the shooter and the ray carries on to the target behind");
  }

  // --- caps: what makes a cylinder not a capsule ---------------------------
  // Straight down the arm's own axis. The flat cap stops the ray at the
  // endpoint; the hemisphere stops it a radius earlier. Same ray, same
  // dimensions, one field different.
  {
    const linalg::vec3f along_arm_origin{200.f, arm_y, arm_far_z + 20.f};
    const linalg::vec3f toward_arm{0.f, 0.f, -1.f};

    const hitscan_result_t flat = resolve_hitscan(along_arm_origin, toward_arm, 1000.f, targets);
    check(flat.hit_uid == 1 && std::abs(flat.distance - 20.f) < 0.01f,
          "a cylinder's flat cap stops the ray at the endpoint");
    check(flat.impact_normal.z > 0.9f, "the cap normal points back along the axis");

    const std::vector<posed_hitbox_t> round_volumes =
        make_volumes({200.f, 0.f, 0.f}, hitbox_shape_t::Capsule);
    const std::vector<hitscan_target_t> round_targets{{1, round_volumes}};
    const hitscan_result_t round = resolve_hitscan(along_arm_origin, toward_arm, 1000.f,
                                                   round_targets);
    check(round.hit_uid == 1 && std::abs(round.distance - (20.f - arm_radius)) < 0.01f,
          "a capsule's hemisphere reaches a radius further than the endpoint");
  }

  // A ray that clears the tube but is still inside its end sphere: the caps are
  // tested, not just the cropped body.
  {
    const std::vector<posed_hitbox_t> arm_only{
        make_volumes({200.f, 0.f, 0.f}, hitbox_shape_t::Capsule)[3]};
    const std::vector<hitscan_target_t> arm_target{{1, arm_only}};

    const hitscan_result_t past_the_end =
        resolve_hitscan({0.f, arm_y, arm_far_z + 1.f}, {1.f, 0.f, 0.f}, 1000.f, arm_target);
    check(past_the_end.hit_uid == 1, "a capsule's cap is hit past the end of its span");
  }

  // --- an oriented box is not an AABB --------------------------------------
  // Turned 45 degrees about the vertical, a 4-deep slab no longer covers the
  // corner its world-axis bounds would.
  {
    constexpr float ROOT_HALF = 0.70710678f;
    posed_hitbox_t  turned{.shape        = hitbox_shape_t::Box,
                           .start        = {200.f, torso_y, 0.f},
                           .end          = {200.f, torso_y, 0.f},
                           .half_extents = {20.f, 20.f, 4.f},
                           .frame        = {.right   = {ROOT_HALF, 0.f, ROOT_HALF},
                                            .up      = {0.f, 1.f, 0.f},
                                            .forward = {-ROOT_HALF, 0.f, ROOT_HALF}},
                           .region       = hit_region_t::Torso};

    const std::vector<posed_hitbox_t>   slab{turned};
    const std::vector<hitscan_target_t> slab_target{{1, slab}};

    const hitscan_result_t centre =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, slab_target);
    check(centre.hit_uid == 1, "the turned slab is hit through its middle");

    // 15 out along +Z is inside the slab's world-axis bounds and outside the
    // slab: at 45 degrees the surface has moved 15 along +X by the time it gets
    // there, and the 4-unit thickness does not reach back.
    const hitscan_result_t corner =
        resolve_hitscan({0.f, torso_y, 15.f}, {1.f, 0.f, 0.f}, 208.f, slab_target);
    check(corner.hit_uid == 0, "a turned box is not tested as its world-axis bounds");
  }

  // --- impact data ---------------------------------------------------------
  {
    const hitscan_result_t hit =
        resolve_hitscan({0.f, torso_y, 0.f}, {1.f, 0.f, 0.f}, 1000.f, targets);
    check(hit.impact_normal.x < -0.9f, "impact normal faces back along the ray");
    check(std::abs(hit.impact_point.y - torso_y) < 0.01f,
          "impact point stays on the ray");
  }

  // --- the broad-phase bound -----------------------------------------------
  // The reject only ever skips work, so the ONE property worth asserting is
  // that it changes no answer. `targets` above is built without a bound
  // (infinite radius, never rejects); this is the same volumes WITH one, swept
  // over a fan of rays that includes the grazing cases where a bound computed
  // slightly too tight would start eating real hits.
  {
    const std::vector<hitscan_target_t> bounded{make_hitscan_target(1, volumes)};

    const shared::bounding_sphere_t sphere = shared::compute_bounding_sphere(volumes);
    check(std::isfinite(sphere.radius) && sphere.radius > 0.f,
          "a bound over real volumes is finite and non-empty");

    // Every volume must sit inside it, or the reject is not conservative.
    bool every_volume_contained = true;
    for (const posed_hitbox_t& hitbox : volumes)
    {
      const float thickness =
          std::max(hitbox.radius, linalg::length(hitbox.half_extents));
      every_volume_contained &=
          linalg::length(hitbox.start - sphere.center) + thickness <= sphere.radius + 0.01f;
      every_volume_contained &=
          linalg::length(hitbox.end - sphere.center) + thickness <= sphere.radius + 0.01f;
    }
    check(every_volume_contained, "every volume lies inside the bound");

    bool identical = true;
    for (int step = -80; step <= 80; ++step)
    {
      // Heights across and well past the silhouette, and a lateral sweep that
      // walks the ray from a clean hit out to a clean miss.
      const float height  = static_cast<float>(step);
      const float lateral = static_cast<float>(step) * 0.5f;
      const linalg::vec3f origin{0.f, height, lateral};
      const linalg::vec3f direction{1.f, 0.f, 0.f};

      const hitscan_result_t without = resolve_hitscan(origin, direction, 1000.f, targets);
      const hitscan_result_t with    = resolve_hitscan(origin, direction, 1000.f, bounded);

      identical &= without.hit_uid == with.hit_uid && without.region == with.region &&
                   std::abs(without.distance - with.distance) < 1e-4f;
    }
    check(identical, "the bound changes no result across a sweep of rays");

    // A shooter standing INSIDE the bound must not be rejected: the projection
    // clamps to 0 there. Compared against the unbounded run rather than asserted
    // outright -- at point-blank the origin is inside the torso box, and a ray
    // starting inside a volume misses it by design, so the interesting question
    // is only whether the bound agrees.
    const linalg::vec3f inside_origin{195.f, torso_y, 0.f};
    check(linalg::length(inside_origin - sphere.center) < sphere.radius,
          "the point-blank origin really is inside the bound");
    check(resolve_hitscan(inside_origin, {1.f, 0.f, 0.f}, 1000.f, targets).hit_uid ==
              resolve_hitscan(inside_origin, {1.f, 0.f, 0.f}, 1000.f, bounded).hit_uid,
          "a shooter inside the bound is not rejected");

    // Proof the sweep above is not vacuous: at least one of its rays passes far
    // enough from the bound to be rejected, so "changes no result" was measured
    // with the reject actually firing.
    const linalg::vec3f far_origin{0.f, 80.f, 40.f};
    const linalg::vec3f to_center = sphere.center - far_origin;
    const float along = std::clamp(linalg::dot(to_center, {1.f, 0.f, 0.f}), 0.f, 1000.f);
    const linalg::vec3f offset = to_center - linalg::vec3f{1.f, 0.f, 0.f} * along;
    check(linalg::length(offset) > sphere.radius,
          "the widest swept ray is outside the bound, so the reject fired");
  }

  printf(failures == 0 ? "hitscan_test PASSED\n" : "hitscan_test FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}
