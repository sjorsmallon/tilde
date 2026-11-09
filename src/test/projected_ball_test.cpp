// The pin for projector_plan.md ss3b's step: a projected ball falls onto a rectangle and sits still, bounces
// off a wall by its restitution, rolls to a stop by its friction, falls through a gap and leaves the frame, is
// pushed out of a rectangle it starts inside, and is carried aside by a rectangle that advances into it. A
// hitbox's silhouette lands on the picture where the frame's matrix maps it: centred on the axis, above it
// where world up is, nothing from behind the lens or outside the frame.
#include "collision/collision_detection.hpp"
#include "lighting/lighting.hpp"
#include "lighting/projected_ball.hpp"
#include "map/map_geometry.hpp"
#include "map/mesh/mesh.hpp"

#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

using linalg::vec2f;
using linalg::vec3f;
using shared::projected_ball_settings_t;
using shared::projected_ball_t;
using shared::projector_mask_rectangle_t;

static int failure_count = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
}

static bool near(float a, float b, float tolerance)
{
  return std::fabs(a - b) <= tolerance;
}

static constexpr float TICK = 1.f / 64.f;

static constexpr projected_ball_settings_t SETTINGS{.radius = 0.03f, .gravity = 2.f, .restitution = 0.5f, .friction = 4.f};
static constexpr projected_ball_settings_t WEIGHTLESS{.radius = 0.03f, .gravity = 0.f, .restitution = 0.5f, .friction = 4.f};

static const projector_mask_rectangle_t FLOOR{0.f, 0.8f, 1.f, 1.f};
static constexpr float                  RESTING_V = 0.8f - 0.03f;

static projected_ball_t run_ticks(const std::vector<projector_mask_rectangle_t>& solids, projected_ball_t ball,
                                  const projected_ball_settings_t& settings, int ticks)
{
  for (int tick = 0; tick < ticks; ++tick)
    ball = shared::projected_ball_step(Span<const projector_mask_rectangle_t>(solids), ball, settings, TICK);
  return ball;
}

static void test_falls_onto_a_rectangle_and_sits()
{
  printf("falls onto a rectangle and sits\n");
  projected_ball_t ball = run_ticks({FLOOR}, {{0.5f, 0.5f}, {0.f, 0.f}}, SETTINGS, 192);
  check(near(ball.uv.v, RESTING_V, 1e-3f), "rests one radius above the rectangle");
  check(std::fabs(ball.velocity.v) < 1e-5f, "normal velocity is stopped, not reflected");
  const float resting_v = ball.uv.v;
  ball                  = run_ticks({FLOOR}, ball, SETTINGS, 16);
  check(near(ball.uv.v, resting_v, 1e-5f), "does not hop once settled");
  check(!shared::projected_ball_is_outside_frame(ball), "a resting ball is inside the frame");
}

static void test_bounces_off_a_wall()
{
  printf("bounces off a wall\n");
  const projector_mask_rectangle_t wall{0.9f, 0.f, 1.f, 1.f};
  const projected_ball_t           ball = run_ticks({wall}, {{0.5f, 0.5f}, {2.f, 0.f}}, WEIGHTLESS, 32);
  check(near(ball.velocity.u, -1.f, 1e-3f), "comes back at restitution times its speed");
  check(ball.uv.u <= 0.9f - 0.03f + 1e-4f, "is outside the wall");
  check(near(ball.uv.v, 0.5f, 1e-5f), "a wall hit leaves v alone");
}

static void test_rolls_to_a_stop()
{
  printf("rolls to a stop along a rectangle\n");
  const projected_ball_t ball = run_ticks({FLOOR}, {{0.3f, RESTING_V}, {0.5f, 0.f}}, SETTINGS, 128);
  check(std::fabs(ball.velocity.u) < 0.01f, "friction takes the roll away");
  check(ball.uv.u > 0.3f && ball.uv.u < 0.5f, "it rolled a little and stopped on the ledge");
  check(near(ball.uv.v, RESTING_V, 1e-3f), "still resting on it");
}

static void test_falls_through_a_gap()
{
  printf("falls through a gap and leaves the frame\n");
  const std::vector<projector_mask_rectangle_t> ledges{{0.f, 0.8f, 0.4f, 1.f}, {0.6f, 0.8f, 1.f, 1.f}};
  projected_ball_t                              ball{{0.5f, 0.5f}, {0.f, 0.f}};
  int                                           ticks_until_outside = 0;
  while (!shared::projected_ball_is_outside_frame(ball) && ticks_until_outside < 192)
  {
    ball = run_ticks(ledges, ball, SETTINGS, 1);
    ++ticks_until_outside;
  }
  check(ticks_until_outside < 192, "leaves the frame through the gap");
  check(ball.velocity.u == 0.f, "touched neither ledge on the way");
}

static void test_pushed_out_of_a_rectangle_it_starts_inside()
{
  printf("pushed out of a rectangle it starts inside\n");
  const projected_ball_t ball = run_ticks({FLOOR}, {{0.5f, 0.85f}, {0.f, 0.f}}, WEIGHTLESS, 1);
  check(near(ball.uv.v, RESTING_V, 1e-5f), "leaves by the nearest edge");
  check(near(ball.uv.u, 0.5f, 1e-5f), "and only that edge");
}

static void test_carried_by_an_advancing_rectangle()
{
  printf("carried aside by an advancing rectangle\n");
  projected_ball_t ball{{0.5f, RESTING_V}, {0.f, 0.f}};
  for (int tick = 0; tick < 30; ++tick)
  {
    const projector_mask_rectangle_t wall{0.f, 0.f, 0.3f + 0.01f * static_cast<float>(tick + 1), 1.f};
    ball = run_ticks({FLOOR, wall}, ball, SETTINGS, 1);
  }
  check(ball.uv.u >= 0.6f + 0.03f - 1e-4f, "is ahead of the wall's edge");
  check(near(ball.uv.v, RESTING_V, 1e-3f), "and still on the floor");
}

static assets::posed_hitbox_t sphere_hitbox(const vec3f& centre, float radius)
{
  assets::posed_hitbox_t hitbox;
  hitbox.shape  = assets::hitbox_shape_t::Sphere;
  hitbox.start  = centre;
  hitbox.end    = centre;
  hitbox.radius = radius;
  return hitbox;
}

static void test_hitbox_silhouette_on_the_picture()
{
  printf("a hitbox's silhouette lands where the frame's matrix maps it\n");
  shared::scene_light_t light;
  light.kind      = shared::light_kind_t::Projector;
  light.position  = {0.f, 0.f, 0.f};
  light.forward   = {0.f, 0.f, -1.f};
  light.up        = {0.f, 1.f, 0.f};
  light.cos_outer = std::cos(linalg::to_radians(30.f));
  light.aspect    = 2.f;
  light.range     = 100.f;
  const linalg::mat4f matrix      = shared::projector_picture_matrix(light);
  const float         half_height = 10.f * std::tan(linalg::to_radians(30.f));

  const std::optional<projector_mask_rectangle_t> centred =
      shared::try_project_hitbox_to_picture(matrix, sphere_hitbox({0.f, 0.f, -10.f}, 1.f));
  check(centred.has_value(), "a sphere on the axis casts");
  check(centred && near(0.5f * (centred->u0 + centred->u1), 0.5f, 1e-4f), "centred in u");
  check(centred && near(0.5f * (centred->v0 + centred->v1), 0.5f, 1e-4f), "centred in v");
  const float near_face_half_height = 9.f * std::tan(linalg::to_radians(30.f));
  check(centred && near(centred->v1 - centred->v0, 1.f / near_face_half_height, 1e-4f),
        "its height is its box's near face over the frame's there");
  check(centred && near(centred->u1 - centred->u0, 0.5f / near_face_half_height, 1e-4f), "its width is that over the aspect");

  const std::optional<projector_mask_rectangle_t> raised =
      shared::try_project_hitbox_to_picture(matrix, sphere_hitbox({0.f, 0.5f * half_height, -10.f}, 0.f));
  check(raised.has_value() && near(raised->v0, 0.25f, 1e-4f), "world up is up the picture: half way to the top is v a quarter");

  const assets::posed_hitbox_t box = assets::make_box_hit_volume({0.f, 0.f, -10.f}, {1.f, 2.f, 3.f}, entities::Hit_Region::Torso);
  const std::optional<projector_mask_rectangle_t> boxed = shared::try_project_hitbox_to_picture(matrix, box);
  check(boxed && near(boxed->v1 - boxed->v0, 4.f / (2.f * (10.f - 3.f) * std::tan(linalg::to_radians(30.f))), 1e-4f),
        "a box's near face bounds its silhouette");

  check(!shared::try_project_hitbox_to_picture(matrix, sphere_hitbox({0.f, 0.f, 10.f}, 1.f)).has_value(),
        "behind the lens casts nothing");
  check(!shared::try_project_hitbox_to_picture(matrix, sphere_hitbox({100.f, 0.f, -10.f}, 1.f)).has_value(),
        "outside the frame casts nothing");
}

static Bounding_Volume_Hierarchy build_world_from_boxes(const std::vector<shared::mesh_geometry_t>& boxes)
{
  std::vector<BVH_Input> inputs;
  for (uint32_t index = 0; index < boxes.size(); ++index)
  {
    for (const shared::collision_piece_t& piece : shared::get_collision_pieces(boxes[index], index + 1))
    {
      BVH_Input input;
      input.aabb             = piece.bounds;
      input.id               = {Collision_Id::Type::Static_Geometry, index};
      input.collision_planes = piece.planes;
      input.face_polygons    = piece.face_polygons;
      inputs.push_back(std::move(input));
    }
  }
  return build_bvh(inputs);
}

static void test_room_point_is_where_the_picture_lands()
{
  printf("the ball is in the room where its picture lands\n");
  shared::scene_light_t light;
  light.kind      = shared::light_kind_t::Projector;
  light.position  = {0.f, 0.f, 0.f};
  light.forward   = {0.f, 0.f, -1.f};
  light.up        = {0.f, 1.f, 0.f};
  light.cos_outer = std::cos(linalg::to_radians(30.f));
  light.aspect    = 2.f;
  light.range     = 1000.f;

  const Bounding_Volume_Hierarchy wall =
      build_world_from_boxes({shared::make_box_mesh({0.f, 0.f, -110.f}, {400.f, 400.f, 10.f})});
  const float half_height = 100.f * std::tan(linalg::to_radians(30.f));

  const std::optional<shared::projected_ball_room_point_t> centre =
      shared::try_find_projected_ball_room_point(wall, light, {0.5f, 0.5f}, 0.03f);
  check(centre.has_value(), "the centre of the picture lands on the wall");
  check(centre && near(centre->point.x, 0.f, 1e-3f) && near(centre->point.y, 0.f, 1e-3f) &&
            near(centre->point.z, -100.f, 1e-3f),
        "on the axis, at the wall's near face");
  check(centre && near(centre->radius, 0.03f * 2.f * half_height, 1e-3f),
        "its radius is the disc's share of the frame's height there");

  const std::optional<shared::projected_ball_room_point_t> raised =
      shared::try_find_projected_ball_room_point(wall, light, {0.5f, 0.25f}, 0.03f);
  check(raised && near(raised->point.y, 0.5f * half_height, 1e-3f), "v a quarter is half way up the frame");

  const std::optional<shared::projected_ball_room_point_t> right =
      shared::try_find_projected_ball_room_point(wall, light, {0.75f, 0.5f}, 0.03f);
  check(right && near(right->point.x, 0.5f * half_height * 2.f, 1e-3f), "u three quarters is half way right, by the aspect");

  light.forward = {0.f, 0.f, 1.f};
  check(!shared::try_find_projected_ball_room_point(wall, light, {0.5f, 0.5f}, 0.03f).has_value(),
        "a picture that lands on nothing has no room point");
}

int main()
{
  test_falls_onto_a_rectangle_and_sits();
  test_bounces_off_a_wall();
  test_rolls_to_a_stop();
  test_falls_through_a_gap();
  test_pushed_out_of_a_rectangle_it_starts_inside();
  test_carried_by_an_advancing_rectangle();
  test_hitbox_silhouette_on_the_picture();
  test_room_point_is_where_the_picture_lands();

  if (failure_count == 0)
    printf("projected_ball_test: all passed\n");
  else
    printf("projected_ball_test: %d FAILED\n", failure_count);
  return failure_count == 0 ? 0 : 1;
}
