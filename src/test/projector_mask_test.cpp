// The pin for the solid mask (projector_plan.md ss2c): the dark of a projector's picture is rectangles of its
// grid, each cast from the glass as a piece, so a hull walking into the dark half of the frame is stopped by the
// projector's mover and one in the lit half walks on. A texel's uv lands where the frame's matrix maps it, so
// the solid dark is where the shader draws dark.
#include "assets/asset.hpp"
#include "assets/asset_state.hpp"
#include "collision/collision_detection.hpp"
#include "collision/movers.hpp"
#include "collision/predicted_world.hpp"
#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "lighting/glyph_solids.hpp"
#include "lighting/lighting.hpp"
#include "lighting/projected_image.hpp"
#include "lighting/projector_mask.hpp"
#include "lighting/shadow_volume.hpp"
#include "lighting/solid_shadows.hpp"
#include "map/map_geometry.hpp"
#include "map/mesh/mesh.hpp"
#include "movement/hull_sweep.hpp"
#include "movement/player_constants.hpp"
#include "text_raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

// A picture read on its own texels up to the grid limit, as an image reads its texture on the full grid.
static shared::projector_mask_t mask_of_picture(const assets::texture_asset_t& picture, float dark_threshold)
{
  const uint32_t columns = std::min<uint32_t>(static_cast<uint32_t>(picture.width), shared::PROJECTOR_MASK_GRID_LIMIT);
  const uint32_t rows    = std::min<uint32_t>(static_cast<uint32_t>(picture.height), shared::PROJECTOR_MASK_GRID_LIMIT);
  return shared::compute_projector_mask_from_cells(
      shared::compute_dark_cells_of_picture(picture, columns, rows, dark_threshold), columns, rows);
}

// The cache told that the image's picture, as it is now, is a dark left half: the cast is pinned on one
// rectangle without a font's say in where the dark is.
static void seed_left_half_dark(shared::projector_mask_cache_t& masks, const entities::Projected_Image_Entity& image,
                                float aspect)
{
  const shared::projector_mask_grid_t grid = shared::compute_projector_mask_grid_for_aspect(aspect);
  masks.masks.push_back(shared::projector_mask_t{
      .image        = image.entity_id,
      .picture_hash = shared::compute_projected_image_picture_hash(image, aspect),
      .columns      = grid.columns,
      .rows         = grid.rows,
      .quads        = {shared::make_projector_mask_quad(0.f, 0.f, 0.5f, 1.f)}});
}

// An axis-aligned quad's edges, as make_projector_mask_quad lays its corners.
static float quad_u0(const shared::projector_mask_quad_t& quad) { return quad.corners[0].u; }
static float quad_v0(const shared::projector_mask_quad_t& quad) { return quad.corners[0].v; }
static float quad_u1(const shared::projector_mask_quad_t& quad) { return quad.corners[2].u; }
static float quad_v1(const shared::projector_mask_quad_t& quad) { return quad.corners[2].v; }

// Every corner in the unit cell and every turn the same way round: what a glyph's solid must be.
static bool quad_is_convex_in_unit_cell(const shared::projector_mask_quad_t& quad)
{
  float turn_sign = 0.f;
  for (uint32_t index = 0; index < 4; ++index)
  {
    const linalg::vec2f corner = quad.corners[index];
    if (corner.u < -1e-6f || corner.u > 1.f + 1e-6f || corner.v < -1e-6f || corner.v > 1.f + 1e-6f)
      return false;
    const linalg::vec2f a    = quad.corners[(index + 1) % 4] - corner;
    const linalg::vec2f b    = quad.corners[(index + 2) % 4] - quad.corners[(index + 1) % 4];
    const float         turn = a.u * b.v - a.v * b.u;
    if (std::fabs(turn) < 1e-9f)
      continue;
    if (turn_sign == 0.f)
      turn_sign = turn > 0.f ? 1.f : -1.f;
    else if ((turn > 0.f ? 1.f : -1.f) != turn_sign)
      return false;
  }
  return turn_sign != 0.f;
}

int main()
{
  // The glyphs come from a font asset, so the asset system is mounted the way a launcher mounts it.
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::mount_asset_source();
  assets::init();

  printf("[pin] the dark of a picture is rectangles of its grid\n");
  {
    const shared::projector_mask_t left_half = mask_of_picture(
        make_picture(8, 4, [](int32_t x, int32_t) { return x < 4; }), shared::PROJECTOR_MASK_DARK_THRESHOLD);
    check(left_half.columns == 8 && left_half.rows == 4, "a small picture is read on its own texels");
    check(left_half.quads.size() == 1, "a dark left half is one rectangle");
    if (left_half.quads.size() == 1)
    {
      const shared::projector_mask_quad_t& rectangle = left_half.quads[0];
      check(near(quad_u0(rectangle), 0.f) && near(quad_u1(rectangle), 0.5f) && near(quad_v0(rectangle), 0.f) && near(quad_v1(rectangle), 1.f),
            "covering u [0, 0.5) and the whole height");
    }
    check(!left_half.capped, "and is not capped");

    const shared::projector_mask_t two_squares = mask_of_picture(
        make_picture(8, 8, [](int32_t x, int32_t y) { return (x < 2 && y < 2) || (x >= 6 && y >= 6); }),
        shared::PROJECTOR_MASK_DARK_THRESHOLD);
    check(two_squares.quads.size() == 2, "two separate squares are two rectangles");

    const shared::projector_mask_t ell = mask_of_picture(
        make_picture(8, 8, [](int32_t x, int32_t y) { return x < 2 || y >= 6; }), shared::PROJECTOR_MASK_DARK_THRESHOLD);
    check(ell.quads.size() == 2, "an L is two rectangles");

    const shared::projector_mask_t white = mask_of_picture(make_picture(8, 8, [](int32_t, int32_t) { return false; }),
                                                           shared::PROJECTOR_MASK_DARK_THRESHOLD);
    check(white.quads.empty(), "a white picture has no dark");

    const shared::projector_mask_t grey = mask_of_picture(make_picture(8, 8, [](int32_t, int32_t) { return false; }), 1.1f);
    check(grey.quads.size() == 1, "every texel is dark under a threshold above white, as one rectangle");

    const shared::projector_mask_t large = mask_of_picture(
        make_picture(256, 128, [](int32_t x, int32_t) { return x < 128; }), shared::PROJECTOR_MASK_DARK_THRESHOLD);
    check(large.columns == shared::PROJECTOR_MASK_GRID_LIMIT && large.rows == shared::PROJECTOR_MASK_GRID_LIMIT,
          "a large picture is read on the grid limit");
    check(large.quads.size() == 1, "and its dark half is still one rectangle");

    const shared::projector_mask_t checkerboard = mask_of_picture(
        make_picture(16, 16, [](int32_t x, int32_t y) { return (x + y) % 2 == 0; }), shared::PROJECTOR_MASK_DARK_THRESHOLD);
    check(checkerboard.capped && checkerboard.quads.size() == shared::MAX_PROJECTOR_MASK_RECTANGLES,
          "a checkerboard is capped at the rectangle limit");
  }

  printf("[pin] the frame's grid follows its aspect\n");
  {
    const shared::projector_mask_grid_t wide   = shared::compute_projector_mask_grid_for_aspect(16.f / 9.f);
    const shared::projector_mask_grid_t square = shared::compute_projector_mask_grid_for_aspect(1.f);
    const shared::projector_mask_grid_t tall   = shared::compute_projector_mask_grid_for_aspect(0.5f);
    check(wide.columns == 64 && wide.rows == 36, "a widescreen frame is 64 by 36 cells");
    check(square.columns == 64 && square.rows == 64, "a square one 64 by 64");
    check(tall.columns == 32 && tall.rows == 64, "a tall one 32 by 64");
  }

  printf("[pin] an image's text is its picture: glyphs at any resolution, cells on the projector's grid\n");
  {
    shared::Entity_System              system;
    const shared::entity_uid_t         image_uid = system.spawn(entities::entity_type::Projected_Image_Entity);
    entities::Projected_Image_Entity* image     = system.get<entities::Projected_Image_Entity>(image_uid);
    check(shared::projected_image_character_at(*image, 3, 5) == ' ' && !shared::projected_image_has_text(*image),
          "a new image is blank");
    shared::set_projected_image_character(*image, 3, 5, 'M');
    check(shared::projected_image_character_at(*image, 3, 5) == 'M', "a set character is read back");
    check(shared::projected_image_character_at(*image, 5, 3) == ' ', "and only there: column is u, row is v");
    check(image->text.values[5].length == 4 && std::strcmp(image->text.values[5].c_str(), "   M") == 0,
          "line 5 is padded with spaces up to the M");
    check(shared::projected_image_has_text(*image), "an M is text");

    const assets::texture_asset_t picture = shared::rasterise_projected_image_text(*image, 320, 180, 16.f / 9.f);
    check(picture.width == 320 && picture.height == 180 && picture.channels == 4, "rasterised at the asked size");
    bool ink_inside_cell = false, ink_outside_cell = false;
    for (int32_t y = 0; y < picture.height; ++y)
      for (int32_t x = 0; x < picture.width; ++x)
      {
        const bool dark    = picture.pixels[(static_cast<size_t>(y) * picture.width + x) * 4] < 128;
        const bool in_cell = x >= 30 && x < 40 && y >= 100 && y < 120;
        ink_inside_cell |= dark && in_cell;
        ink_outside_cell |= dark && !in_cell;
      }
    check(ink_inside_cell && !ink_outside_cell, "the glyph's ink is inside its 10 by 20 pixel cell and nowhere else");

    const Span<const uint8_t> font_bytes = assets::get(assets::get_font(assets::font_id(shared::PROJECTED_IMAGE_FONT)))->bytes;
    const std::optional<shared::character_ink_box_t> wide_m =
        shared::try_compute_character_ink_box(font_bytes, 'M', shared::compute_projected_image_cell_aspect(16.f / 9.f));
    const std::optional<shared::character_ink_box_t> narrow_m =
        shared::try_compute_character_ink_box(font_bytes, 'M', shared::compute_projected_image_cell_aspect(1.f));
    check(wide_m && wide_m->u0 < 0.1f && wide_m->u1 > 0.9f && wide_m->v0 > 0.02f && wide_m->v1 < 0.9f,
          "the cell is the font's advance: an M's ink nearly fills its width and sits inside its height");
    check(narrow_m && std::fabs((narrow_m->u1 - narrow_m->u0) - (wide_m->u1 - wide_m->u0)) < 1e-3f &&
              (narrow_m->v1 - narrow_m->v0) < 0.6f * (wide_m->v1 - wide_m->v0),
          "on a square frame the cell is wider on the wall, so the glyph still fills its width and sits shorter in it");

    uint32_t tiled_width = 0, tiled_height = 0;
    for (uint32_t column = 0; column < 32; ++column)
      tiled_width += shared::compute_projected_image_cell_pixels(column, 0, 2048, 2048).width;
    for (uint32_t row = 0; row < 9; ++row)
      tiled_height += shared::compute_projected_image_cell_pixels(0, row, 2048, 2048).height;
    const shared::projected_image_cell_pixels_t last = shared::compute_projected_image_cell_pixels(31, 8, 2048, 2048);
    check(tiled_width == 2048 && tiled_height == 2048 && last.x + last.width == 2048 && last.y + last.height == 2048,
          "the cells tile a 2048 by 2048 layer exactly, 9 not dividing it");
    const assets::texture_asset_t typed_cell = shared::compose_projected_image_cell(*image, 3, 5, 320, 180, 16.f / 9.f);
    const assets::texture_asset_t blank_cell = shared::compose_projected_image_cell(*image, 4, 5, 320, 180, 16.f / 9.f);
    bool typed_has_ink = false, blank_is_white = true;
    for (size_t pixel = 0; pixel < typed_cell.pixels.size(); pixel += 4)
      typed_has_ink |= typed_cell.pixels[pixel] < 128;
    for (size_t pixel = 0; pixel < blank_cell.pixels.size(); pixel += 4)
      blank_is_white &= blank_cell.pixels[pixel] == 255 && blank_cell.pixels[pixel + 3] == 255;
    check(typed_cell.width == 10 && typed_cell.height == 20 && typed_has_ink, "a cell composed alone is 10 by 20 with the M's ink");
    check(blank_is_white, "and a blank cell over no texture is white");

    shared::projector_mask_cache_t  masks;
    const shared::projector_mask_t& glyph = shared::find_or_compute_projector_mask_for_image(masks, *image, 16.f / 9.f);
    check(glyph.columns == 64 && glyph.rows == 36, "the mask is on the 64 by 36 grid");
    check(glyph.quads.size() == 1, "an M has no row of its own, so it is solid as one box round its ink");
    bool every_quad_in_cell = true;
    for (const shared::projector_mask_quad_t& quad : glyph.quads)
      for (const linalg::vec2f& corner : quad.corners)
        every_quad_in_cell &= corner.u >= 3.f / 32.f - 1e-5f && corner.u <= 4.f / 32.f + 1e-5f &&
                              corner.v >= 5.f / 9.f - 1e-5f && corner.v <= 6.f / 9.f + 1e-5f;
    check(every_quad_in_cell, "inside the character's cell of the 32 by 9 text");
    if (glyph.quads.size() == 1)
      check(quad_u1(glyph.quads[0]) - quad_u0(glyph.quads[0]) > 0.6f / 32.f &&
                quad_v1(glyph.quads[0]) - quad_v0(glyph.quads[0]) > 0.5f / 9.f,
            "over most of the cell's width and half its height: an M is a wide, tall glyph");

    const uint64_t hash_before = shared::compute_projected_image_picture_hash(*image, 16.f / 9.f);
    shared::set_projected_image_character(*image, 3, 5, ' ');
    check(shared::compute_projected_image_picture_hash(*image, 16.f / 9.f) != hash_before,
          "blanking the character moves the picture's hash");
    const shared::projector_mask_t& cleared = shared::find_or_compute_projector_mask_for_image(masks, *image, 16.f / 9.f);
    check(cleared.quads.empty() && masks.masks.size() == 1, "and the mask is recomputed in place, not kept beside");

    shared::set_projected_image_character(*image, 0, 8, '/');
    const shared::projector_mask_t& slash = shared::find_or_compute_projector_mask_for_image(masks, *image, 16.f / 9.f);
    check(slash.quads.size() == 1, "a slash has a row: one parallelogram");
    if (slash.quads.size() == 1)
    {
      const shared::projector_mask_quad_t& quad = slash.quads[0];
      check(quad.corners[0].v < quad.corners[2].v && quad.corners[0].u > quad.corners[2].u,
            "leaning from top-right to bottom-left");
      bool in_corner_cell = true;
      for (const linalg::vec2f& corner : quad.corners)
        in_corner_cell &= corner.u >= 0.f && corner.u <= 1.f / 32.f + 1e-5f && corner.v >= 8.f / 9.f - 1e-5f && corner.v <= 1.f + 1e-5f;
      check(in_corner_cell, "placed in the bottom-left cell");
    }
  }

  printf("[pin] a glyph's solid is a few convex quads in its cell\n");
  {
    bool every_row_sound = true;
    for (const shared::glyph_solid_t& solid : shared::glyph_solid_rows())
    {
      every_row_sound &= solid.quad_count >= 1 && solid.quad_count <= shared::MAX_GLYPH_SOLID_QUADS &&
                         solid.character > ' ' && solid.character <= '~';
      for (uint32_t index = 0; index < solid.quad_count; ++index)
        every_row_sound &= quad_is_convex_in_unit_cell(solid.quads[index]);
    }
    check(every_row_sound, "every hand-authored row is one to three convex quads inside the unit cell");
    check(shared::find_glyph_solid('/') != nullptr && shared::find_glyph_solid('T') != nullptr &&
              shared::find_glyph_solid('T')->quad_count == 2,
          "a slash and a T have rows, the T two quads");
    check(shared::find_glyph_solid('O') == nullptr, "an O has none: its ink box stands in");
    check(quad_is_convex_in_unit_cell(shared::make_projector_mask_quad(0.1f, 0.2f, 0.3f, 0.4f)) &&
              !quad_is_convex_in_unit_cell(shared::projector_mask_quad_t{.corners = {{{0.f, 0.f}, {1.f, 1.f}, {1.f, 0.f}, {0.f, 1.f}}}}),
          "the pin tells a box from a bow tie");
  }

  // A floor whose top is y = 0, and a projector 300 above it aimed straight down with its frame's up along -z,
  // showing an image whose left half is dark.
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
  projector->switch_state.value            = true;
  projector->light.cuts_geometry           = false;
  projector->light.solid_shadows       = entities::Solid_Shadows::Shadow_Volumes_Only;

  const shared::entity_uid_t         image_uid = system.spawn(entities::entity_type::Projected_Image_Entity);
  entities::Projected_Image_Entity* image     = system.get<entities::Projected_Image_Entity>(image_uid);
  image->projector                             = projector_uid;
  check(shared::find_projected_image_for_projector(system, projector_uid) == image, "the image naming the projector is found");

  shared::projector_mask_cache_t masks;
  seed_left_half_dark(masks, *image, projector->aspect);

  const shared::path_pose_t pose{.position = projector->position, .orientation = projector->orientation};
  const std::optional<shared::shadow_light_t> shadow_light = shared::try_shadow_light_from_entity(*projector, pose);
  const std::optional<shared::scene_light_t>  scene_light  = shared::try_convert_light_entity_to_scene_light(*projector, pose);
  check(shadow_light.has_value() && scene_light.has_value(), "the projector is a shadow light and a scene light");
  if (!shadow_light || !scene_light)
    return 1;

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
  shared::shadow_memo_t                memo;
  std::vector<shared::mover_t>         movers;
  const shared::shadow_volume_report_t report =
      shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks, memo, volumes);
  shared::compute_solid_shadows(system, volumes, memo, movers);

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

  printf("[pin] a thing in the light lands on the picture as rectangles; the room throws nothing\n");
  {
    // A crate on the floor, right of centre, a THING (casts_solid_from_lens); the floor is the room, unowned.
    const shared::mesh_geometry_t   crate      = shared::make_box_mesh({50.f, 10.f, 0.f}, {10.f, 10.f, 10.f});
    const Bounding_Volume_Hierarchy with_crate = build_world({floor, crate});
    const shared::entity_uid_t      crate_owner_uid = system.spawn(entities::entity_type::Geometry_Owner_Entity);
    entities::Geometry_Owner_Entity* crate_owner = system.get<entities::Geometry_Owner_Entity>(crate_owner_uid);
    crate_owner->switch_state.value              = true;
    crate_owner->casts_solid_from_lens           = true;
    const std::vector<shared::entity_uid_t> owners = {shared::null_entity_uid, crate_owner_uid};

    std::vector<shared::shadow_volume_t> crate_volumes;
    (void)shared::compute_shadow_volumes(system, with_crate, owners, {}, {}, {}, masks, memo, crate_volumes);
    const linalg::mat4f matrix = shared::projector_picture_matrix(*scene_light);

    std::vector<bool>                               cells;
    std::vector<shared::projector_mask_quad_t> footprint;
    const bool capped = shared::compute_projector_shadow_footprint(projector_uid, *scene_light, matrix, crate_volumes,
                                                                   cells, footprint);
    check(!footprint.empty() && !capped, "the crate lands on the picture, within the cap");
    const auto covered = [&](const vec3& world)
    {
      const vec3 uv = project_to_uv(matrix, world);
      for (const shared::projector_mask_quad_t& rectangle : footprint)
        if (uv.x >= quad_u0(rectangle) && uv.x < quad_u1(rectangle) && uv.y >= quad_v0(rectangle) && uv.y < quad_v1(rectangle))
          return true;
      return false;
    };
    check(covered({50.f, 20.f, 0.f}), "the crate's top is dark");
    check(!covered({-50.f, 0.f, 0.f}), "the floor beside it is lit: it is what the picture lands on");
    check(!covered({0.f, 0.f, 0.f}), "and so is the frame's centre");
    float area = 0.f;
    for (const shared::projector_mask_quad_t& rectangle : footprint)
      area += (quad_u1(rectangle) - quad_u0(rectangle)) * (quad_v1(rectangle) - quad_v0(rectangle));
    check(area > 0.005f && area < 0.05f, "the dark is about the crate's size");

    std::vector<shared::projector_mask_quad_t> room_only;
    (void)shared::compute_projector_shadow_footprint(projector_uid, *scene_light, matrix, volumes, cells, room_only);
    check(room_only.empty(), "the floor alone, unowned, throws nothing on itself");
  }

  printf("[pin] a white image casts no mask, no image casts none, and the cache holds one mask per image\n");
  {
    check(masks.masks.size() == 1 && masks.masks[0].image == image_uid, "the cache holds the image's mask");
    shared::set_projected_image_character(*image, 0, 0, 'M');
    std::vector<shared::shadow_volume_t> plain_volumes;
    const shared::shadow_volume_report_t typed =
        shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks, memo, plain_volumes);
    check(typed.mask_pieces > 0 && masks.masks.size() == 1, "a typed glyph is cast, the mask recomputed in place");
    shared::set_projected_image_character(*image, 0, 0, ' ');
    const shared::shadow_volume_report_t plain =
        shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks, memo, plain_volumes);
    check(plain.mask_pieces == 0, "no mask pieces from a white image");
    check(masks.masks.size() == 1 && masks.masks[0].quads.empty(), "recomputed in place");

    masks.masks.clear();
    seed_left_half_dark(masks, *image, projector->aspect);
    image->projector = shared::null_entity_uid;
    const shared::shadow_volume_report_t unnamed =
        shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks, memo, plain_volumes);
    check(unnamed.mask_pieces == 0, "a projector no image names is a white frame");
    image->projector = projector_uid;
    const shared::shadow_volume_report_t named =
        shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks, memo, plain_volumes);
    check(named.mask_pieces == 1 && masks.masks.size() == 1, "named again, the one rectangle is back and the cache has not grown");
  }

  printf(failure_count == 0 ? "\nprojector_mask_test: all passed\n" : "\nprojector_mask_test: %d FAILED\n",
         failure_count);
  return failure_count == 0 ? 0 : 1;
}
