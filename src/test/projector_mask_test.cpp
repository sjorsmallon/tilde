// The pin for the solid mask (projector_plan.md ss2c): the dark of a projector's picture is rectangles of its
// grid, each cast from the glass as a piece, so a hull walking into the dark half of the frame is stopped by the
// projector's mover and one in the lit half walks on. A texel's uv lands where the frame's matrix maps it, so
// the solid dark is where the shader draws dark.
#include "assets/asset_state.hpp"
#include "collision/collision_detection.hpp"
#include "collision/movers.hpp"
#include "collision/predicted_world.hpp"
#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "lighting/lighting.hpp"
#include "lighting/projector_mask.hpp"
#include "lighting/shadow_volume.hpp"
#include "lighting/solid_shadows.hpp"
#include "map/map_geometry.hpp"
#include "map/mesh/mesh.hpp"
#include "movement/hull_sweep.hpp"
#include "movement/player_constants.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using linalg::vec3;
using linalg::vec4;

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

// A picture `width` by `height`, white, with `paint` deciding which texels are black.
template <typename Paint_T>
static assets::texture_asset_t make_picture(int32_t width, int32_t height, Paint_T&& paint)
{
  assets::texture_asset_t picture;
  picture.width    = width;
  picture.height   = height;
  picture.channels = 4;
  picture.pixels.assign(static_cast<size_t>(width) * height * 4, 255);
  for (int32_t y = 0; y < height; ++y)
    for (int32_t x = 0; x < width; ++x)
      if (paint(x, y))
      {
        uint8_t* texel = &picture.pixels[(static_cast<size_t>(y) * width + x) * 4];
        texel[0] = texel[1] = texel[2] = 0;
      }
  return picture;
}

static Bounding_Volume_Hierarchy build_world(const std::vector<shared::mesh_geometry_t>& solids)
{
  std::vector<BVH_Input> inputs;
  for (uint32_t index = 0; index < solids.size(); ++index)
    for (const shared::collision_piece_t& piece : shared::get_collision_pieces(solids[index], index + 1))
    {
      BVH_Input input;
      input.aabb             = piece.bounds;
      input.id               = {Collision_Id::Type::Static_Geometry, index};
      input.collision_planes = piece.planes;
      input.face_polygons    = piece.face_polygons;
      inputs.push_back(std::move(input));
    }
  return build_bvh(inputs);
}

static vec3 project_to_uv(const linalg::mat4f& matrix, const vec3& point)
{
  const vec4 clip = matrix * vec4{point.x, point.y, point.z, 1.f};
  return {(clip.x / clip.w) * 0.5f + 0.5f, (clip.y / clip.w) * 0.5f + 0.5f, clip.z / clip.w};
}

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  constexpr assets::texture_asset LEFT_HALF_DARK = static_cast<assets::texture_asset>(1001);

  printf("[pin] the dark of a picture is rectangles of its grid\n");
  {
    const shared::projector_mask_t left_half = shared::compute_projector_mask_from_picture(
        make_picture(8, 4, [](int32_t x, int32_t) { return x < 4; }), LEFT_HALF_DARK,
        shared::PROJECTOR_MASK_DARK_THRESHOLD);
    check(left_half.columns == 8 && left_half.rows == 4, "a small picture is read on its own texels");
    check(left_half.rectangles.size() == 1, "a dark left half is one rectangle");
    if (left_half.rectangles.size() == 1)
    {
      const shared::projector_mask_rectangle_t& rectangle = left_half.rectangles[0];
      check(near(rectangle.u0, 0.f) && near(rectangle.u1, 0.5f) && near(rectangle.v0, 0.f) && near(rectangle.v1, 1.f),
            "covering u [0, 0.5) and the whole height");
    }
    check(!left_half.capped, "and is not capped");

    const shared::projector_mask_t two_squares = shared::compute_projector_mask_from_picture(
        make_picture(8, 8, [](int32_t x, int32_t y) { return (x < 2 && y < 2) || (x >= 6 && y >= 6); }),
        LEFT_HALF_DARK, shared::PROJECTOR_MASK_DARK_THRESHOLD);
    check(two_squares.rectangles.size() == 2, "two separate squares are two rectangles");

    const shared::projector_mask_t ell = shared::compute_projector_mask_from_picture(
        make_picture(8, 8, [](int32_t x, int32_t y) { return x < 2 || y >= 6; }), LEFT_HALF_DARK,
        shared::PROJECTOR_MASK_DARK_THRESHOLD);
    check(ell.rectangles.size() == 2, "an L is two rectangles");

    const shared::projector_mask_t white = shared::compute_projector_mask_from_picture(
        make_picture(8, 8, [](int32_t, int32_t) { return false; }), LEFT_HALF_DARK,
        shared::PROJECTOR_MASK_DARK_THRESHOLD);
    check(white.rectangles.empty(), "a white picture has no dark");

    const shared::projector_mask_t grey = shared::compute_projector_mask_from_picture(
        make_picture(8, 8, [](int32_t, int32_t) { return false; }), LEFT_HALF_DARK, 1.1f);
    check(grey.rectangles.size() == 1, "every texel is dark under a threshold above white, as one rectangle");

    const shared::projector_mask_t large = shared::compute_projector_mask_from_picture(
        make_picture(256, 128, [](int32_t x, int32_t) { return x < 128; }), LEFT_HALF_DARK,
        shared::PROJECTOR_MASK_DARK_THRESHOLD);
    check(large.columns == shared::PROJECTOR_MASK_GRID_LIMIT && large.rows == shared::PROJECTOR_MASK_GRID_LIMIT,
          "a large picture is read on the grid limit");
    check(large.rectangles.size() == 1, "and its dark half is still one rectangle");

    const shared::projector_mask_t checkerboard = shared::compute_projector_mask_from_picture(
        make_picture(16, 16, [](int32_t x, int32_t y) { return (x + y) % 2 == 0; }), LEFT_HALF_DARK,
        shared::PROJECTOR_MASK_DARK_THRESHOLD);
    check(checkerboard.capped && checkerboard.rectangles.size() == shared::MAX_PROJECTOR_MASK_RECTANGLES,
          "a checkerboard is capped at the rectangle limit");
  }

  // A floor whose top is y = 0, and a projector 300 above it aimed straight down with its frame's up along -z,
  // showing the left-half-dark picture.
  const shared::mesh_geometry_t   floor = shared::make_box_mesh({0.f, -64.f, 0.f}, {2048.f, 64.f, 2048.f});
  const Bounding_Volume_Hierarchy bvh   = build_world({floor});
  const std::vector<shared::entity_uid_t> owner_of = {shared::null_entity_uid};

  shared::Entity_System      system;
  const shared::entity_uid_t projector_uid = system.spawn(entities::entity_type::Projector_Entity);
  entities::Projector_Entity* projector    = system.get<entities::Projector_Entity>(projector_uid);
  projector->position                      = {0.f, 300.f, 0.f};
  projector->orientation                   = linalg::from_view_angles(0.f, -90.f);
  projector->range                         = 400.f;
  projector->half_angle_degrees            = 20.f;
  projector->aspect                        = 1.f;
  projector->texture                       = LEFT_HALF_DARK;
  projector->switch_state.value            = true;
  projector->light.cuts_geometry           = false;
  projector->light.shadows_are_solid       = true;

  shared::projector_mask_cache_t masks;
  masks.masks.push_back(shared::compute_projector_mask_from_picture(
      make_picture(8, 4, [](int32_t x, int32_t) { return x < 4; }), LEFT_HALF_DARK,
      shared::PROJECTOR_MASK_DARK_THRESHOLD));

  const shared::path_pose_t pose{.position = projector->position, .orientation = projector->orientation};
  const std::optional<shared::shadow_light_t> shadow_light = shared::try_shadow_light_from_entity(*projector, pose);
  const std::optional<shared::scene_light_t>  scene_light  = shared::try_convert_light_entity_to_scene_light(*projector, pose);
  check(shadow_light.has_value() && scene_light.has_value(), "the projector is a shadow light and a scene light");
  if (!shadow_light || !scene_light)
    return 1;
  check(shadow_light->picture == LEFT_HALF_DARK, "the shadow light carries the picture");

  printf("[pin] a picture uv lands where the frame's matrix maps it\n");
  {
    const linalg::mat4f matrix = shared::projector_shadow_projection(*scene_light, 1024).view_projection;
    const float         glass  = shared::projector_mask_glass_distance(*shadow_light);
    check(glass > shared::SHADOW_NEAR_PLANE && glass <= projector->range * 0.5f, "the glass is past the near plane and within half the range");
    const vec3 top_left = project_to_uv(matrix, shared::projector_frame_point(*shadow_light, 0.f, 0.f, glass));
    check(near(top_left.x, 0.f, 2e-3f) && near(top_left.y, 0.f, 2e-3f), "uv (0, 0) is the picture's top-left texel");
    const vec3 bottom_right = project_to_uv(matrix, shared::projector_frame_point(*shadow_light, 1.f, 1.f, glass));
    check(near(bottom_right.x, 1.f, 2e-3f) && near(bottom_right.y, 1.f, 2e-3f), "uv (1, 1) is the bottom-right");
    const vec3 middle = project_to_uv(matrix, shared::projector_frame_point(*shadow_light, 0.5f, 0.25f, 200.f));
    check(near(middle.x, 0.5f, 2e-3f) && near(middle.y, 0.25f, 2e-3f), "and a point inside at any distance keeps its uv");
    const Array<vec3, 4> corners = shared::compute_projector_frame_corners(*scene_light, glass);
    const vec3           from_frame = shared::projector_frame_point(*shadow_light, 0.f, 1.f, glass);
    check(linalg::length(from_frame - corners[0]) < 1e-2f, "uv (0, 1) is the frame's bottom-left corner");
  }

  std::vector<shared::shadow_volume_t> volumes;
  std::vector<shared::mover_t>         movers;
  const shared::shadow_volume_report_t report =
      shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks,volumes);
  shared::solid_shadow_cache_t solid_shadow_cache;
  shared::compute_solid_shadows(system, volumes, solid_shadow_cache, movers);

  printf("[pin] the picture's dark casts from the glass as the projector's own pieces\n");
  {
    check(report.mask_pieces == 1, "one mask piece for the one rectangle");
    // The floor casts into the projector too (it is a caster with no owner), its volume behind the floor.
    uint32_t mask_volumes = 0;
    for (const shared::shadow_volume_t& volume : volumes)
      if (volume.caster == projector_uid)
        ++mask_volumes;
    check(mask_volumes == 1, "one volume whose caster is the projector");
    check(movers.size() == 1 && movers[0].uid == projector_uid, "the projector's mover holds them");
  }

  printf("[pin] a hull walks into the dark half and is stopped, through the lit half and is not\n");
  {
    shared::predicted_world_t world{};
    world.movers = movers;
    // The frame at the floor: 300 down at a 20 degree half angle is 109 units half a side. u runs along the
    // frame's right, which for a view aimed down with yaw 0 is +x; the dark half is x < 0.
    const Array<vec3, 4> at_floor = shared::compute_projector_frame_corners(*scene_light, 300.f);
    const vec3           left_middle  = (at_floor[0] + at_floor[3]) * 0.5f;
    const vec3           right_middle = (at_floor[1] + at_floor[2]) * 0.5f;
    const vec3           across       = linalg::normalize(right_middle - left_middle);
    const vec3           centre       = (left_middle + right_middle) * 0.5f;
    const float          stand_y      = shared::player_half_height + 1.f;
    const vec3           lit_start    = centre + across * 60.f + vec3{0.f, stand_y, 0.f};
    const vec3           dark_end     = centre - across * 60.f + vec3{0.f, stand_y, 0.f};

    const std::optional<shared::hull_sweep_hit_t> into_dark =
        shared::sweep_hull(bvh, world, lit_start, dark_end, shared::player_half_width, shared::player_half_height);
    check(into_dark.has_value(), "walking from the lit half into the dark half hits something");
    check(into_dark && into_dark->mover_uid == projector_uid, "what it hits is the projector's mover");
    check(into_dark && into_dark->t > 0.1f && into_dark->t < 0.9f, "part way across: at the dark's edge");
    check(into_dark && std::fabs(linalg::dot(into_dark->normal, across)) > 0.9f, "the stopping face is the dark's edge, square to the walk");

    const vec3 lit_end = centre + across * 100.f + vec3{0.f, stand_y, 0.f};
    const std::optional<shared::hull_sweep_hit_t> along_lit =
        shared::sweep_hull(bvh, world, lit_start, lit_end, shared::player_half_width, shared::player_half_height);
    check(!along_lit.has_value(), "walking within the lit half hits nothing");

    const vec3 beside_frame_start = centre + across * 200.f + vec3{0.f, stand_y, 0.f};
    const vec3 beside_frame_end   = centre + across * 260.f + vec3{0.f, stand_y, 0.f};
    const std::optional<shared::hull_sweep_hit_t> beside =
        shared::sweep_hull(bvh, world, beside_frame_start, beside_frame_end, shared::player_half_width,
                           shared::player_half_height);
    check(!beside.has_value(), "outside the frame nothing is solid");
  }

  printf("[pin] a projector with no picture casts no mask, and the cache computes a mask once\n");
  {
    projector->texture = assets::texture_asset::Missing;
    std::vector<shared::shadow_volume_t> plain_volumes;
    const shared::shadow_volume_report_t plain =
        shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks,plain_volumes);
    check(plain.mask_pieces == 0, "no mask pieces without a picture");
    check(masks.masks.size() == 1, "the cache holds the one seeded mask");
    const shared::projector_mask_t& missing = shared::find_or_compute_projector_mask(masks, assets::texture_asset::Missing);
    check(missing.rectangles.empty() && masks.masks.size() == 2, "Missing is an empty mask, kept");
    check(&shared::find_or_compute_projector_mask(masks, LEFT_HALF_DARK) == &masks.masks[0],
          "a seeded texture is found, not recomputed");
  }

  printf(failure_count == 0 ? "\nprojector_mask_test: all passed\n" : "\nprojector_mask_test: %d FAILED\n",
         failure_count);
  return failure_count == 0 ? 0 : 1;
}
