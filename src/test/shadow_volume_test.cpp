// A shadow volume is the pyramid a convex piece throws from a light: the space behind the caster is
// inside, the space beside it and the space between it and the light are not.

#include "aabb.hpp"
#include "map_geometry.hpp"
#include "shadow_volume.hpp"

#include <cmath>
#include <cstdio>
#include <optional>

static int failure_count = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
}

static shared::aabb_bounds_t box_about(const linalg::vec3f& center, float half)
{
  return {center - linalg::vec3f{half, half, half}, center + linalg::vec3f{half, half, half}};
}

static std::optional<shared::shadow_volume_t> try_cast(const shared::shadow_light_t& light,
                                                       const shared::collision_piece_t& piece)
{
  const shared::shadow_cast_t cast = shared::cast_shadow_volume(light, piece.planes, piece.face_polygons, 7);
  if (cast.refusal != shared::shadow_cast_refusal_t::None)
    return std::nullopt;
  return cast.volume;
}

int main()
{
  const shared::collision_piece_t caster =
      shared::piece_from_aabb({.center = {0.f, 0.f, 0.f}, .half_extents = {32.f, 32.f, 32.f}});

  printf("[pin] a point light above a box throws a pyramid below it\n");
  {
    const shared::shadow_light_t light = {.apex = {0.f, 200.f, 0.f}, .range = 0.f};
    const std::optional<shared::shadow_volume_t> volume =
        try_cast(light, caster);
    check(volume.has_value(), "a box below a light casts");
    check(volume->side_plane_count == 4 && volume->back_plane_count == 5,
          "a box seen square-on has four sides, and five faces turned away");
    check(shared::shadow_volume_contains_point(*volume, {0.f, -100.f, 0.f}), "straight below is inside");
    check(shared::shadow_volume_contains_point(*volume, {40.f, -200.f, 0.f}), "the pyramid widens with depth");
    check(!shared::shadow_volume_contains_point(*volume, {45.f, -20.f, 0.f}), "just outside the widening edge is outside");
    check(!shared::shadow_volume_contains_point(*volume, {200.f, -100.f, 0.f}), "far beside is outside");
    check(!shared::shadow_volume_contains_point(*volume, {0.f, 100.f, 0.f}), "between the light and the box is outside");
    check(shared::shadow_volume_touches_box(*volume, box_about({40.f, -100.f, 0.f}, 16.f)), "a hull straddling the edge touches");
    check(!shared::shadow_volume_contains_box(*volume, box_about({40.f, -100.f, 0.f}, 16.f)), "and is not contained");
    check(shared::shadow_volume_contains_box(*volume, box_about({0.f, -100.f, 0.f}, 16.f)), "a hull wholly beneath is contained");
  }

  printf("[pin] a point light's range caps the volume\n");
  {
    const shared::shadow_light_t light = {.apex = {0.f, 200.f, 0.f}, .range = 250.f};
    const std::optional<shared::shadow_volume_t> volume =
        try_cast(light, caster);
    check(volume.has_value() && volume->side_plane_count == 4 + 1, "a ranged light adds a far cap");
    check(shared::shadow_volume_contains_point(*volume, {0.f, -40.f, 0.f}), "inside the range is inside");
    check(!shared::shadow_volume_contains_point(*volume, {0.f, -100.f, 0.f}), "past the range is outside");
  }

  printf("[pin] a light beside the box throws sideways, and one inside it throws nothing\n");
  {
    const shared::shadow_light_t light = {.apex = {200.f, 0.f, 0.f}};
    const std::optional<shared::shadow_volume_t> volume =
        try_cast(light, caster);
    check(volume.has_value() && shared::shadow_volume_contains_point(*volume, {-100.f, 0.f, 0.f}),
          "the far side of the box is inside");
    check(!shared::shadow_volume_contains_point(*volume, {0.f, -100.f, 0.f}), "below the box is outside");

    const shared::shadow_light_t inside = {.apex = {0.f, 0.f, 0.f}};
    check(!try_cast(inside, caster).has_value(),
          "a light inside its caster casts no volume");
  }

  printf("[pin] a directional light throws a prism\n");
  {
    const shared::shadow_light_t light = {.directional = true, .direction = {0.f, -1.f, 0.f}};
    const std::optional<shared::shadow_volume_t> volume =
        try_cast(light, caster);
    check(volume.has_value() && volume->side_plane_count == 4, "four sides and no far cap");
    check(shared::shadow_volume_contains_point(*volume, {0.f, -1000.f, 0.f}), "straight below, however far, is inside");
    check(shared::shadow_volume_contains_point(*volume, {31.f, -1000.f, 31.f}), "the prism does not widen");
    check(!shared::shadow_volume_contains_point(*volume, {33.f, -1000.f, 0.f}), "and does not narrow");
    check(!shared::shadow_volume_contains_point(*volume, {0.f, 100.f, 0.f}), "above the box is outside");
  }

  printf("[pin] a diagonal view hulls the box's six silhouette corners\n");
  {
    const shared::shadow_light_t light = {.apex = {200.f, 200.f, 200.f}};
    const std::optional<shared::shadow_volume_t> volume =
        try_cast(light, caster);
    check(volume.has_value() && volume->side_plane_count == 6 && volume->back_plane_count == 3,
          "six sides, and the three faces turned away");
    check(shared::shadow_volume_contains_point(*volume, {-100.f, -100.f, -100.f}), "the far corner's direction is inside");
  }

  printf("[pin] a flat slab under a grazing light shadows nothing above itself\n");
  {
    const shared::collision_piece_t slab =
        shared::piece_from_aabb({.center = {0.f, 0.f, 0.f}, .half_extents = {512.f, 16.f, 512.f}});
    const shared::shadow_light_t light = {.apex = {-1000.f, 300.f, 0.f}};
    const std::optional<shared::shadow_volume_t> volume = try_cast(light, slab);
    check(volume.has_value(), "a slab seen at a grazing angle casts");
    check(!shared::shadow_volume_contains_point(*volume, {0.f, 40.f, 0.f}), "the air just above the slab is lit");
    check(!shared::shadow_volume_contains_point(*volume, {400.f, 100.f, 0.f}), "the air above its far end is lit");
    check(shared::shadow_volume_contains_point(*volume, {0.f, -40.f, 0.f}), "just below the slab is in shadow");
    check(shared::shadow_volume_contains_point(*volume, {600.f, -10.f, 0.f}), "past its far edge, below the light's rays, is in shadow");
    check(!shared::shadow_volume_touches_box(*volume, box_about({0.f, 60.f, 0.f}, 16.f)), "a hull standing on the slab touches no shadow");
    check(shared::shadow_volume_touches_box(*volume, box_about({600.f, -10.f, 0.f}, 16.f)), "a hull past the far edge does");
  }

  printf("[pin] a spot light casts only from pieces its beam reaches\n");
  {
    const shared::shadow_light_t light = {.apex                  = {0.f, 200.f, 0.f},
                                          .direction             = {0.f, -1.f, 0.f},
                                          .cosine_of_outer_angle = std::cos(linalg::to_radians(30.f))};
    check(try_cast(light, caster).has_value(), "a box straight below a downward spot casts");
    const shared::collision_piece_t beside =
        shared::piece_from_aabb({.center = {400.f, 200.f, 0.f}, .half_extents = {32.f, 32.f, 32.f}});
    const shared::shadow_cast_t cast = shared::cast_shadow_volume(light, beside.planes, beside.face_polygons, 7);
    check(cast.refusal == shared::shadow_cast_refusal_t::Outside_Spot_Beam, "a box level with the spot is outside its beam");
    const shared::collision_piece_t wide_slab =
        shared::piece_from_aabb({.center = {0.f, 0.f, 0.f}, .half_extents = {400.f, 8.f, 400.f}});
    const std::optional<shared::shadow_volume_t> slab_volume = try_cast(light, wide_slab);
    check(slab_volume.has_value(),
          "a slab wider than the beam casts: the beam touches it although no corner is inside the cone");
    check(slab_volume && slab_volume->side_plane_count > 4,
          "and the beam's planes clip its pyramid, since the slab's corners lie outside the cone");
    check(slab_volume && shared::shadow_volume_contains_point(*slab_volume, {0.f, -100.f, 0.f}),
          "under the slab inside the beam is in shadow");
    check(slab_volume && !shared::shadow_volume_contains_point(*slab_volume, {300.f, -100.f, 0.f}),
          "under the slab but outside the beam is not: nothing lit there to be in the shadow of");
    check(slab_volume && slab_volume->side_count == 4,
          "the drawn ring is the slab's four-edge silhouette");
  }

  printf("[pin] a light level with one end of a long slab still casts: the light is outside the piece\n");
  {
    const shared::collision_piece_t long_slab =
        shared::piece_from_aabb({.center = {0.f, 0.f, 0.f}, .half_extents = {16.f, 200.f, 400.f}});
    const shared::shadow_light_t light = {.apex                  = {0.f, 210.f, -380.f},
                                          .direction             = {0.f, -1.f, 0.f},
                                          .range                 = 1024.f,
                                          .cosine_of_outer_angle = std::cos(linalg::to_radians(30.f))};
    const shared::shadow_cast_t cast = shared::cast_shadow_volume(light, long_slab.planes, long_slab.face_polygons, 7);
    printf("       refusal %d sides %u back %u\n", (int)cast.refusal, cast.volume.side_plane_count,
           cast.volume.back_plane_count);
    check(cast.refusal == shared::shadow_cast_refusal_t::None, "a spot just above the near end of a tall slab casts");
    check(cast.refusal == shared::shadow_cast_refusal_t::None &&
              shared::shadow_volume_contains_point(cast.volume, {0.f, -250.f, -380.f}),
          "the floor under the lit end is in its shadow");
    check(cast.refusal == shared::shadow_cast_refusal_t::None &&
              !shared::shadow_volume_contains_point(cast.volume, {0.f, -250.f, 300.f}),
          "the floor under the far end, outside the beam, is not");
  }

  printf("%s\n", failure_count == 0 ? "ALL PASSED" : "FAILURES");
  return failure_count == 0 ? 0 : 1;
}
