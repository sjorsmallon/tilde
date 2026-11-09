// The pin for an asset mesh's collision shape (geometry_def.md ss11.6): its triangles weld into a closed mesh
// and decompose into convex pieces whose union is the letter, hole included, so a hull dropped at the hole's
// shadow falls through where the directional bound would have held it, and a plank roots in the ring's shadow.
#include "assets/asset_state.hpp"
#include "assets/asset_types.hpp"
#include "collision/collision_detection.hpp"
#include "collision/movers.hpp"
#include "collision/predicted_world.hpp"
#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "geometry/convex_decomposition.hpp"
#include "geometry/convex_hull.hpp"
#include "geometry/directional_bounds.hpp"
#include "lighting/projector_mask.hpp"
#include "lighting/shadow_volume.hpp"
#include "lighting/solid_shadows.hpp"
#include "lighting/support_planks.hpp"
#include "map/map_geometry.hpp"
#include "map/map_geometry_asset.hpp"
#include "map/mesh/mesh.hpp"
#include "movement/hull_sweep.hpp"
#include "movement/player_constants.hpp"

#include <cmath>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

using linalg::vec3;

static int failure_count = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
}

// Four corners appended as four fresh vertices, wound so the face looks along `outward`: an exporter's soup.
static void add_quad(assets::mesh_asset_t& asset, vec3 a, vec3 b, vec3 c, vec3 d, const vec3& outward)
{
  if (linalg::dot(linalg::cross(b - a, c - a), outward) < 0.f)
  {
    std::swap(b, d);
  }
  const uint32_t base = (uint32_t)asset.vertices.size();
  for (const vec3& corner : {a, b, c, d})
    asset.vertices.push_back({corner, linalg::normalize(outward), {0.f, 0.f}});
  for (const uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u})
    asset.indices.push_back(base + index);
}

// A square ring: outer half width `outer`, inner `inner`, from y = 0 to y = `height`.
static assets::mesh_asset_t make_square_ring(float outer, float inner, float height)
{
  assets::mesh_asset_t asset;
  const vec3           square[4] = {{1.f, 0.f, 1.f}, {-1.f, 0.f, 1.f}, {-1.f, 0.f, -1.f}, {1.f, 0.f, -1.f}};
  for (int side = 0; side < 4; ++side)
  {
    const vec3 o0 = square[side] * outer, o1 = square[(side + 1) % 4] * outer;
    const vec3 i0 = square[side] * inner, i1 = square[(side + 1) % 4] * inner;
    const vec3 up = {0.f, height, 0.f};
    const vec3 side_out = linalg::normalize((o0 + o1) * 0.5f);
    add_quad(asset, o0 + up, o1 + up, i1 + up, i0 + up, {0.f, 1.f, 0.f});
    add_quad(asset, o0, o1, i1, i0, {0.f, -1.f, 0.f});
    add_quad(asset, o0, o1, o1 + up, o0 + up, side_out);
    add_quad(asset, i0, i1, i1 + up, i0 + up, side_out * -1.f);
  }
  return asset;
}

static bool inside_any_piece(Span<const shared::collision_piece_t> pieces, const vec3& point)
{
  for (const shared::collision_piece_t& piece : pieces)
  {
    bool inside = true;
    for (const Plane& plane : piece.planes)
      if (linalg::dot(point - plane.point, plane.normal) > 1e-3f)
      {
        inside = false;
        break;
      }
    if (inside)
      return true;
  }
  return false;
}

static std::vector<vec3> box_corners(const vec3& minimum, const vec3& maximum)
{
  std::vector<vec3> corners;
  for (int corner = 0; corner < 8; ++corner)
    corners.push_back({(corner & 1) ? maximum.x : minimum.x, (corner & 2) ? maximum.y : minimum.y,
                       (corner & 4) ? maximum.z : minimum.z});
  return corners;
}

// A dop block as decompose_mesh.py writes one: the extents of the box's corners, in the directions' order.
static std::string box_dop_block(const vec3& minimum, const vec3& maximum)
{
  const std::vector<vec3>             corners = box_corners(minimum, maximum);
  const shared::directional_extents_t extents = shared::compute_directional_extents(corners);
  std::string                         block   = "dop\n";
  for (uint32_t direction = 0; direction < shared::DIRECTIONAL_BOUND_DIRECTIONS; ++direction)
    block += std::format("  {} {}\n", extents[direction].nearest, extents[direction].furthest);
  return block;
}

static std::string box_hull_block(const vec3& minimum, const vec3& maximum)
{
  std::string block = "hull\n";
  for (int corner = 0; corner < 8; ++corner)
    block += std::format("  {} {} {}\n", (corner & 1) ? maximum.x : minimum.x, (corner & 2) ? maximum.y : minimum.y,
                         (corner & 4) ? maximum.z : minimum.z);
  return block;
}

static Bounding_Volume_Hierarchy build_world(const shared::mesh_geometry_t&            floor,
                                             Span<const shared::collision_piece_t> ring_pieces)
{
  std::vector<BVH_Input> inputs;
  for (const shared::collision_piece_t& piece : shared::get_collision_pieces(floor, 1))
  {
    BVH_Input input;
    input.aabb             = piece.bounds;
    input.id               = {Collision_Id::Type::Static_Geometry, 0};
    input.collision_planes = piece.planes;
    input.face_polygons    = piece.face_polygons;
    inputs.push_back(std::move(input));
  }
  for (const shared::collision_piece_t& piece : ring_pieces)
  {
    BVH_Input input;
    input.aabb             = piece.bounds;
    input.id               = {Collision_Id::Type::Static_Geometry, 1};
    input.collision_planes = piece.planes;
    input.face_polygons    = piece.face_polygons;
    inputs.push_back(std::move(input));
  }
  return build_bvh(inputs);
}

static std::optional<shared::hull_sweep_hit_t> drop_hull(const Bounding_Volume_Hierarchy& bvh,
                                                         Span<const shared::mover_t> movers, float x, float z)
{
  shared::predicted_world_t world{};
  world.movers = movers;
  return shared::sweep_hull(bvh, world, {x, 400.f, z}, {x, 0.f, z}, shared::player_half_width,
                            shared::player_half_height);
}

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::mount_asset_source();
  assets::number_asset_ids_from_tree(asset_state, "resources");

  printf("[pin] the sidecar's word\n");
  {
    const auto parse_shape = [](std::string_view text) -> std::optional<shared::asset_collision_shape_t>
    {
      const std::optional<shared::asset_collision_file_t> file = shared::try_parse_collision_file(text, "test.collision");
      return file ? std::optional(file->shape) : std::nullopt;
    };
    check(parse_shape(" decompose_mesh_into_convex_pieces\r\n") ==
              shared::asset_collision_shape_t::Decompose_Mesh_Into_Convex_Pieces,
          "decompose_mesh_into_convex_pieces, with whitespace around it");
    check(parse_shape("dop_26") == shared::asset_collision_shape_t::Dop_26, "dop_26");
    check(!parse_shape("convex hull"), "another word is refused");
    check(!parse_shape("dop_26 dop_26"), "a word twice is refused");
    check(shared::collision_sidecar_path_for_mesh_asset("resources/glb/letter_o.glb") ==
              "resources/glb/letter_o.collision",
          "the sidecar sits beside the mesh under its stem");
    check(shared::collision_sidecar_path_for_mesh_asset("resources/a.b/letter") == "resources/a.b/letter.collision",
          "a dot in a directory name is not an extension");
  }

  printf("[pin] the sidecar's hull blocks: one piece per block, scaled and posed per placement\n");
  {
    // The square ring as four bars, as an offline decomposer would write it: the hole is in no hull.
    const std::string text = box_hull_block({32.f, 0.f, -64.f}, {64.f, 16.f, 64.f}) +
                             box_hull_block({-64.f, 0.f, -64.f}, {-32.f, 16.f, 64.f}) +
                             box_hull_block({-32.f, 0.f, 32.f}, {32.f, 16.f, 64.f}) +
                             box_hull_block({-32.f, 0.f, -64.f}, {32.f, 16.f, -32.f});
    const std::optional<shared::asset_collision_file_t> file = shared::try_parse_collision_file(text, "ring.collision");
    check(file && file->shape == shared::asset_collision_shape_t::Hulls, "hull blocks parse as Hulls");
    check(file && file->hulls.size() == 4 && file->hulls[0].size() == 8, "four blocks of eight points");

    if (file)
    {
      const linalg::quatf quarter_turn = linalg::from_axis_angle({0.f, 1.f, 0.f}, 90.f);
      const std::optional<std::vector<shared::collision_piece_t>> placed = shared::try_build_collision_pieces_from_hulls(
          file->hulls, {2.f, 1.f, 1.f}, {0.f, 200.f, 0.f}, quarter_turn, "ring.collision");
      check(placed && placed->size() == 4, "each block is one piece");
      size_t most_planes = 0;
      if (placed)
        for (const shared::collision_piece_t& piece : *placed)
          most_planes = std::max(most_planes, piece.planes.size());
      check(most_planes == 6, "a box block is six planes, its coplanar points one face");
      if (placed)
      {
        const vec3 bar   = linalg::rotate(quarter_turn, vec3{96.f, 8.f, 0.f}) + vec3{0.f, 200.f, 0.f};
        const vec3 hole  = linalg::rotate(quarter_turn, vec3{48.f, 8.f, 0.f}) + vec3{0.f, 200.f, 0.f};
        const vec3 above = linalg::rotate(quarter_turn, vec3{96.f, 24.f, 0.f}) + vec3{0.f, 200.f, 0.f};
        check(inside_any_piece(*placed, bar), "a point in a bar, stretched in x and turned, is inside");
        check(!inside_any_piece(*placed, hole), "the hole, stretched with it, is in no piece");
        check(!inside_any_piece(*placed, above), "nor the air above");
      }
    }

    check(!shared::try_parse_collision_file("hull 0 0 0  1 0 0  0 1 0", "short.collision"), "three points are refused");
    check(!shared::try_parse_collision_file("hull 0 0 0  1 0 0  0 1 0  0 0", "torn.collision"),
          "a torn point is refused");
    check(!shared::try_parse_collision_file("hull 0 0 0  1 0 0  0 1 0  0 0 x", "word.collision"),
          "a word among the numbers is refused");
    check(!shared::try_parse_collision_file("hull", "empty.collision"), "an empty block is refused");
    check(!shared::try_parse_collision_file("dop_26 hull 0 0 0  1 0 0  0 1 0  0 0 1", "mixed.collision"),
          "a word before the blocks is refused");
    std::string crowded = "hull";
    for (size_t point = 0; point <= shared::MAX_SIDECAR_HULL_POINTS; ++point)
      crowded += std::format(" {} {} {}", (float)point, (float)(point * point), (float)(point % 7));
    check(!shared::try_parse_collision_file(crowded, "crowded.collision"), "a block past the point cap is refused");

    const std::optional<shared::asset_collision_file_t> flat =
        shared::try_parse_collision_file("hull 0 0 0  64 0 0  64 0 64  0 0 64", "flat.collision");
    check(flat.has_value(), "a flat block parses: flatness is the hull's to find");
    check(flat && !shared::try_build_collision_pieces_from_hulls(flat->hulls, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f},
                                                                  linalg::quatf::identity(), "flat.collision"),
          "and is refused when hulled");
  }

  printf("[pin] the sidecar's dop blocks: one piece per block, its 26 planes built, never hulled\n");
  {
    const std::string text = box_dop_block({32.f, 0.f, -64.f}, {64.f, 16.f, 64.f}) +
                             box_dop_block({-64.f, 0.f, -64.f}, {-32.f, 16.f, 64.f}) +
                             box_dop_block({-32.f, 0.f, 32.f}, {32.f, 16.f, 64.f}) +
                             box_dop_block({-32.f, 0.f, -64.f}, {32.f, 16.f, -32.f});
    const std::optional<shared::asset_collision_file_t> file = shared::try_parse_collision_file(text, "ring.collision");
    check(file && file->shape == shared::asset_collision_shape_t::Dops, "dop blocks parse as Dops");
    check(file && file->dops.size() == 4 && file->hulls.empty(), "four blocks, no hulls");
    check(file && file->dops[0][0].nearest == 32.f && file->dops[0][0].furthest == 64.f,
          "a block's first pair is its reach along x");

    if (file)
    {
      const linalg::quatf quarter_turn = linalg::from_axis_angle({0.f, 1.f, 0.f}, 90.f);
      const std::optional<std::vector<shared::collision_piece_t>> placed = shared::try_build_collision_pieces_from_dops(
          file->dops, {2.f, 1.f, 1.f}, {0.f, 200.f, 0.f}, quarter_turn, "ring.collision");
      check(placed && placed->size() == 4, "each block is one piece");
      size_t most_planes = 0;
      if (placed)
        for (const shared::collision_piece_t& piece : *placed)
          most_planes = std::max(most_planes, piece.planes.size());
      check(most_planes == 6, "a box's bound is six planes: the twenty redundant ones contribute no face");
      if (placed)
      {
        const vec3 bar  = linalg::rotate(quarter_turn, vec3{96.f, 8.f, 0.f}) + vec3{0.f, 200.f, 0.f};
        const vec3 hole = linalg::rotate(quarter_turn, vec3{48.f, 8.f, 0.f}) + vec3{0.f, 200.f, 0.f};
        check(inside_any_piece(*placed, bar), "a point in a bar, stretched in x and turned, is inside");
        check(!inside_any_piece(*placed, hole), "the hole, stretched with it, is in no piece");
      }
      check(!shared::try_build_collision_pieces_from_dops(file->dops, {0.f, 1.f, 1.f}, {0.f, 0.f, 0.f},
                                                          linalg::quatf::identity(), "ring.collision"),
            "a zero scale is refused: every block is flat");
    }

    // The octahedron's bound as a block, stretched: eight faces whatever the scale, and the diagonal planes tilt with it.
    const std::vector<vec3> octahedron = {{1.f, 0.f, 0.f},  {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
                                          {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f},  {0.f, 0.f, -1.f}};
    const shared::directional_extents_t extents = shared::compute_directional_extents(octahedron);
    std::string                         octahedron_text = "dop";
    for (uint32_t direction = 0; direction < shared::DIRECTIONAL_BOUND_DIRECTIONS; ++direction)
      octahedron_text += std::format(" {} {}", extents[direction].nearest, extents[direction].furthest);
    const std::optional<shared::asset_collision_file_t> octahedron_file =
        shared::try_parse_collision_file(octahedron_text, "octahedron.collision");
    const std::optional<std::vector<shared::collision_piece_t>> stretched =
        octahedron_file ? shared::try_build_collision_pieces_from_dops(octahedron_file->dops, {3.f, 1.f, 1.f},
                                                                       {0.f, 0.f, 0.f}, linalg::quatf::identity(),
                                                                       "octahedron.collision")
                        : std::nullopt;
    check(stretched && stretched->size() == 1 && (*stretched)[0].planes.size() == 8,
          "the stretched octahedron's bound has its eight faces");
    check(stretched && inside_any_piece(*stretched, {2.9f, 0.f, 0.f}) && !inside_any_piece(*stretched, {1.5f, 0.6f, 0.f}),
          "its corner moved out to x = 3 and its face stayed flat");

    check(!shared::try_parse_collision_file("dop 0 1", "short.collision"), "two numbers are refused");
    std::string reversed = "dop 1 0";
    for (uint32_t direction = 1; direction < shared::DIRECTIONAL_BOUND_DIRECTIONS; ++direction)
      reversed += " 0 1";
    check(!shared::try_parse_collision_file(reversed, "reversed.collision"), "a nearest past its furthest is refused");
    check(!shared::try_parse_collision_file(octahedron_text + " 7", "extra.collision"), "a 27th number is refused");
    check(!shared::try_parse_collision_file("hull 0 0 0  1 0 0  0 1 0  0 0 1 " + octahedron_text, "mixed.collision"),
          "hull and dop blocks in one file are refused");

    for (const char* mesh_path : {"resources/glb/mvmt.glb", "resources/obj/Error.obj"})
    {
      const shared::asset_collision_file_t committed = shared::read_asset_collision_file({.path = mesh_path});
      const std::optional<std::vector<shared::collision_piece_t>> committed_pieces =
          shared::try_build_collision_pieces_from_dops(committed.dops, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f},
                                                       linalg::quatf::identity(), mesh_path);
      size_t most_planes = 0;
      if (committed_pieces)
        for (const shared::collision_piece_t& piece : *committed_pieces)
          most_planes = std::max(most_planes, piece.planes.size());
      printf("       %s: %zu dop blocks, at most %zu planes\n", mesh_path, committed.dops.size(), most_planes);
      check(committed.shape == shared::asset_collision_shape_t::Dops && committed_pieces &&
                committed_pieces->size() == committed.dops.size() && most_planes <= shared::DIRECTIONAL_BOUND_PLANES,
            "the committed sidecar parses as dop blocks and every block closes within 26 planes");
    }
  }

  const assets::mesh_asset_t ring = make_square_ring(64.f, 32.f, 16.f);

  printf("[pin] a soup of triangles welds into one closed mesh\n");
  {
    const shared::mesh_geometry_t closed = shared::make_mesh_from_asset_triangles(ring, {1.f, 1.f, 1.f});
    check(closed.vertices.size() == 16, "sixty-four corners weld to sixteen vertices");
    check(closed.faces.size() == 32, "thirty-two triangle faces");
    check(!shared::find_mesh_defect(closed), "and it is a solid");

    assets::mesh_asset_t open = ring;
    open.indices.resize(open.indices.size() - 3);
    const std::optional<shared::mesh_defect_t> defect =
        shared::find_mesh_defect(shared::make_mesh_from_asset_triangles(open, {1.f, 1.f, 1.f}));
    check(defect && defect->kind == shared::mesh_defect_kind_t::Edge_Is_Unpaired,
          "a ring with a triangle missing is an open skin: an unpaired edge");
    check(!shared::try_decompose_asset_mesh_into_collision_pieces(open, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f},
                                                                   linalg::quatf::identity()),
          "and is refused");
  }

  printf("[pin] the pieces are the ring: a point in the hole is in none of them\n");
  std::vector<shared::collision_piece_t> pieces;
  {
    const std::optional<std::vector<shared::collision_piece_t>> decomposed =
        shared::try_decompose_asset_mesh_into_collision_pieces(ring, {1.f, 1.f, 1.f}, {0.f, 200.f, 0.f},
                                                               linalg::quatf::identity());
    check(decomposed.has_value(), "the ring decomposes");
    if (decomposed)
      pieces = *decomposed;
    printf("       %zu pieces\n", pieces.size());
    check(pieces.size() >= 2 && pieces.size() <= 8, "a square ring is a few convex pieces");
    size_t most_planes = 0;
    for (const shared::collision_piece_t& piece : pieces)
      most_planes = std::max(most_planes, piece.planes.size());
    printf("       most planes on a piece: %zu\n", most_planes);
    check(most_planes <= 8, "coplanar triangles merged: a piece has one plane per face");
    check(inside_any_piece(pieces, {48.f, 208.f, 0.f}), "a point in the ring is inside a piece");
    check(inside_any_piece(pieces, {0.f, 208.f, 48.f}), "on every side");
    check(inside_any_piece(pieces, {-40.f, 208.f, -40.f}), "and in a corner");
    check(!inside_any_piece(pieces, {0.f, 208.f, 0.f}), "the hole is in no piece");
    check(!inside_any_piece(pieces, {80.f, 208.f, 0.f}), "nor the air beside the ring");
    check(!inside_any_piece(pieces, {48.f, 230.f, 0.f}), "nor the air above it");

    const std::optional<std::vector<shared::collision_piece_t>> stretched =
        shared::try_decompose_asset_mesh_into_collision_pieces(ring, {2.f, 1.f, 1.f}, {0.f, 0.f, 0.f},
                                                               linalg::quatf::identity());
    check(stretched && inside_any_piece(*stretched, {96.f, 8.f, 0.f}) && !inside_any_piece(*stretched, {48.f, 8.f, 0.f}),
          "the scale is applied before the cut");
    const std::optional<std::vector<shared::collision_piece_t>> mirrored =
        shared::try_decompose_asset_mesh_into_collision_pieces(ring, {-1.f, 1.f, 1.f}, {0.f, 0.f, 0.f},
                                                               linalg::quatf::identity());
    check(mirrored && inside_any_piece(*mirrored, {48.f, 8.f, 0.f}) && !inside_any_piece(*mirrored, {0.f, 8.f, 0.f}),
          "a mirroring scale still closes outward");
  }

  printf("[pin] under a sun the ring's shadow is solid and the hole's shadow is open; the bound's is not\n");
  {
    const shared::mesh_geometry_t floor = shared::make_box_mesh({0.f, -64.f, 0.f}, {2048.f, 64.f, 2048.f});
    const Bounding_Volume_Hierarchy bvh = build_world(floor, pieces);

    // Five degrees off straight down toward +x: exactly along the ring's walls the silhouette is undecided
    // and the cast refuses. A shadow is its caster's column, drifting +x by 0.0875 per unit of fall.
    shared::Entity_System      system;
    const shared::entity_uid_t sun_uid = system.spawn(entities::entity_type::Directional_Light_Entity);
    entities::Directional_Light_Entity* sun = system.get<entities::Directional_Light_Entity>(sun_uid);
    sun->light.solid_shadows                = entities::Solid_Shadows::Shadow_Volumes_Only;
    sun->orientation                        = linalg::from_axis_angle({0.f, 0.f, 1.f}, -85.f);
    const std::vector<shared::entity_uid_t> owner_of = {shared::null_entity_uid, shared::null_entity_uid};

    std::vector<shared::shadow_volume_t> volumes;
    std::vector<shared::mover_t>         movers;
    shared::projector_mask_cache_t       masks;
    shared::shadow_memo_t                memo;
    shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks, memo, volumes);
    shared::compute_solid_shadows(system, volumes, memo, movers);
    size_t ring_volumes = 0;
    for (const shared::shadow_volume_t& volume : volumes)
      if (volume.caster_bounds.min.y > 100.f)
        ++ring_volumes;
    printf("       %zu volumes, %zu from the ring\n", volumes.size(), ring_volumes);
    check(ring_volumes == pieces.size(), "every piece of the ring throws a volume");
    check(!volumes.empty() && volumes[0].light_directional && volumes[0].light_direction.y < -0.99f,
          "the sun points nearly straight down");

    const shared::mover_t* dark = nullptr;
    for (const shared::mover_t& mover : movers)
      if (mover.uid == sun_uid)
        dark = &mover;
    check(dark != nullptr, "the sun's volumes are one mover");
    const auto in_the_dark = [&](const vec3& point)
    { return dark != nullptr && inside_any_piece(dark->pieces, point); };
    check(in_the_dark({48.f, 100.f, 0.f}), "under the ring is dark");
    check(in_the_dark({0.f, 100.f, -48.f}), "on every side");
    check(!in_the_dark({0.f, 100.f, 0.f}), "under the hole is lit: the hole is open");
    check(!in_the_dark({80.f, 100.f, 0.f}), "beside the ring is lit");

    const std::optional<shared::hull_sweep_hit_t> through_the_hole = drop_hull(bvh, movers, 8.f, 0.f);
    check(through_the_hole && through_the_hole->mover_uid == shared::null_entity_uid && through_the_hole->id.index == 0,
          "a hull dropped down the hole's shadow reaches the floor");
    shared::predicted_world_t world{};
    world.movers = movers;
    const std::optional<shared::hull_sweep_hit_t> into_the_ring =
        shared::sweep_hull(bvh, world, {160.f, 100.f, 0.f}, {0.f, 100.f, 0.f}, shared::player_half_width,
                           shared::player_half_height);
    check(into_the_ring && into_the_ring->mover_uid == sun_uid, "a hull walked into the ring's shadow is stopped by the sun's mover");

    shared::supported_player_t held = {.player = 900, .feet = {48.f, 100.f, 0.f}, .ground_mover = sun_uid};
    std::vector<shared::support_plank_t> planks;
    shared::compute_support_planks(Span<const shared::supported_player_t>(&held, 1), volumes, bvh, planks);
    check(planks.size() == 1, "a player held in the ring's shadow gets a plank");
    if (!planks.empty())
    {
      const float landing_x = (planks[0].corners[2].x + planks[0].corners[3].x) * 0.5f;
      printf("       plank lands at x = %.1f\n", landing_x);
      check(landing_x > 49.5f && landing_x < 82.9f && std::fabs(planks[0].corners[2].y) < 0.1f,
            "and it roots in the ring's shadow on the floor");
    }

    // The same ring as its directional bound: one lump, no hole, so the hole's shadow is dark.
    std::vector<vec3> points;
    for (const vertex_xnu& vertex : ring.vertices)
      points.push_back(vertex.position + vec3{0.f, 200.f, 0.f});
    const Array<Plane, shared::DIRECTIONAL_BOUND_PLANES> bound_planes = shared::compute_directional_bound_planes(points);
    std::vector<Plane>                                   bound_plane_list;
    for (uint32_t index = 0; index < bound_planes.size(); ++index)
      bound_plane_list.push_back(bound_planes[index]);
    const std::optional<shared::convex_polyhedron_t> body =
        shared::try_build_convex_from_planes(bound_plane_list, {0.f, 208.f, 0.f}, 200.f);
    check(body.has_value(), "the bound closes");
    if (body)
    {
      const shared::collision_piece_t bound = {.bounds        = shared::compute_point_bounds(body->vertices),
                                               .planes        = shared::compute_polyhedron_planes(*body),
                                               .face_polygons = shared::compute_polyhedron_face_polygons(*body)};
      const Bounding_Volume_Hierarchy bound_bvh = build_world(floor, Span<const shared::collision_piece_t>(&bound, 1));
      std::vector<shared::shadow_volume_t> bound_volumes;
      std::vector<shared::mover_t>         bound_movers;
      shared::compute_shadow_volumes(system, bound_bvh, owner_of, {}, {}, {}, masks, memo, bound_volumes);
      shared::compute_solid_shadows(system, bound_volumes, memo, bound_movers);
      bool hole_filled = false;
      for (const shared::mover_t& mover : bound_movers)
        if (mover.uid == sun_uid && inside_any_piece(mover.pieces, {0.f, 100.f, 0.f}))
          hole_filled = true;
      check(hole_filled, "under the bound the hole's shadow is dark: a blob with no hole");
    }
  }

  printf(failure_count == 0 ? "\nasset_mesh_collision_test: all passed\n" : "\nasset_mesh_collision_test: %d FAILED\n",
         failure_count);
  return failure_count == 0 ? 0 : 1;
}
