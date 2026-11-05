// The pin for solid shadows (projector_plan.md ss1): a shadows_are_solid light's volume is a mover, so a
// hull dropped onto a crate's shadow stops on the shadow's top face and one dropped beside it reaches the
// floor. Without the flag the light's volumes are movers of nothing.
#include "assets/asset_state.hpp"
#include "collision/collision_detection.hpp"
#include "collision/movers.hpp"
#include "collision/predicted_world.hpp"
#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "lighting/player_shadow_casters.hpp"
#include "lighting/projector_mask.hpp"
#include "lighting/shadow_volume.hpp"
#include "lighting/solid_shadows.hpp"
#include "map/map_geometry.hpp"
#include "map/mesh/mesh.hpp"
#include "movement/hull_sweep.hpp"
#include "movement/player_constants.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using linalg::vec3;

static int failure_count = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
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

static std::optional<shared::hull_sweep_hit_t> drop_hull(const Bounding_Volume_Hierarchy& bvh,
                                                         Span<const shared::mover_t> movers, float x, float z)
{
  shared::predicted_world_t world{};
  world.movers = movers;
  return shared::sweep_hull(bvh, world, {x, 200.f, z}, {x, 0.f, z}, shared::player_half_width,
                            shared::player_half_height);
}

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  // A floor whose top is y = 0 and a crate on it from y = 0 to 64, both plain map geometry: casters with no owner.
  const shared::mesh_geometry_t floor = shared::make_box_mesh({0.f, -64.f, 0.f}, {2048.f, 64.f, 2048.f});
  const shared::mesh_geometry_t crate = shared::make_box_mesh({0.f, 32.f, 0.f}, {32.f, 32.f, 32.f});
  const Bounding_Volume_Hierarchy bvh = build_world({floor, crate});
  const std::vector<shared::entity_uid_t> owner_of = {shared::null_entity_uid, shared::null_entity_uid};

  // A lamp a little above crate height, off to -x: the crate's shadow runs toward +x with a nearly flat top.
  shared::Entity_System      system;
  const shared::entity_uid_t lamp_uid = system.spawn(entities::entity_type::Point_Light_Entity);
  entities::Point_Light_Entity* lamp  = system.get<entities::Point_Light_Entity>(lamp_uid);
  lamp->position                      = {-300.f, 80.f, 0.f};
  lamp->range                         = 0.f;
  lamp->switch_state.value            = true;
  lamp->light.cuts_geometry           = false;
  lamp->light.shadows_are_solid       = true;

  std::vector<shared::shadow_volume_t> volumes;
  std::vector<shared::mover_t>         movers;
  shared::projector_mask_cache_t       masks;
  shared::solid_shadow_cache_t         cache;
  shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks, volumes);
  shared::compute_solid_shadows(system, volumes, cache, movers);

  printf("[pin] a shadows_are_solid light's volumes are one mover\n");
  {
    check(volumes.size() == 2, "the floor and the crate each throw a volume, kept with no receiver in the map");
    check(movers.size() == 1, "one mover for the light");
    check(!movers.empty() && movers[0].uid == lamp_uid, "the mover is the light's");
    check(!movers.empty() && movers[0].pieces.size() == 2, "one piece per volume");
    check(!movers.empty() && !movers[0].crushes, "a shadow crushes nobody");
  }

  printf("[pin] a piece is built once: the same volume next cut is a copy, a moved lamp's is rebuilt\n");
  {
    check(cache.builds == 2, "the first cut built both pieces");
    std::vector<shared::mover_t> again;
    shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks, volumes);
    shared::compute_solid_shadows(system, volumes, cache, again);
    check(cache.builds == 2, "the same lamp over the same crate builds nothing");
    check(again.size() == 1 && again[0].pieces.size() == 2 && !movers.empty() &&
              again[0].pieces[1].planes.size() == movers[0].pieces[1].planes.size(),
          "and the copied pieces are the pieces");

    lamp->position.y += 1.f;
    std::vector<shared::mover_t> moved;
    shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks, volumes);
    shared::compute_solid_shadows(system, volumes, cache, moved);
    check(cache.builds == 4, "a lamp that moved throws new planes: both pieces rebuilt");
    check(cache.entries.size() == 2, "and overwrite their entries rather than adding");

    lamp->position.y -= 1.f;
    std::vector<shared::mover_t> back;
    shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks, volumes);
    shared::compute_solid_shadows(system, volumes, cache, back);
    check(cache.builds == 6, "moved back is new planes again: the cache holds one past build per volume");
  }

  printf("[pin] a hull on the shadow's top face is held above the floor, on ground\n");
  {
    const std::optional<shared::hull_sweep_hit_t> hit = drop_hull(bvh, movers, 100.f, 0.f);
    check(hit.has_value(), "a hull dropped beside the crate hits something");
    check(hit && hit->mover_uid == lamp_uid, "what it hits is the light's mover");
    // The top side plane runs from the lamp through the crate's far top edge (32, 64); the hull's near bottom
    // corner at x = 84 meets it first.
    const float plane_y_at_near_corner = 80.f + (84.f + 300.f) * (64.f - 80.f) / (32.f + 300.f);
    const float expected_centre_y      = plane_y_at_near_corner + shared::player_half_height + hull_sweep_skin;
    const float stop_y                 = 200.f - 200.f * (hit ? hit->t : 0.f);
    printf("       stopped at y = %.2f (expected %.2f), normal.y = %.3f\n", stop_y, expected_centre_y,
           hit ? hit->normal.y : 0.f);
    check(hit && std::fabs(stop_y - expected_centre_y) < 0.5f,
          "the centre stops half_height plus the skin above the shadow's top face: the feet are on it");
    check(hit && hit->normal.y >= shared::GROUND_NORMAL_MINIMUM_Y, "the top face is ground: the lens is low");
  }

  printf("[pin] a hull beside the shadow falls to the floor\n");
  {
    const std::optional<shared::hull_sweep_hit_t> hit = drop_hull(bvh, movers, 100.f, 200.f);
    check(hit.has_value(), "it hits the floor");
    check(hit && hit->mover_uid == shared::null_entity_uid, "the floor is no mover");
    check(hit && hit->id.type == Collision_Id::Type::Static_Geometry && hit->id.index == 0, "the hit names the floor");
  }

  printf("[pin] a player casts into the solid light: a mover named by the player, which its own view leaves out\n");
  {
    // A player beside the crate at z = 200: its shadow runs toward +x, spreading in z as the rays do.
    const shared::entity_uid_t player_uid = system.spawn(entities::entity_type::Player_Entity);
    entities::Player_Entity*   player     = system.get<entities::Player_Entity>(player_uid);
    player->position                      = {0.f, 0.f, 200.f};
    player->health.current_health         = 100;

    std::vector<shared::player_shadow_caster_t> casters;
    shared::compute_player_shadow_casters(system, casters);
    check(casters.size() == 1 && casters[0].uid == player_uid, "one caster, the player's");
    if (casters.size() == 1)
    {
      const shared::aabb_bounds_t hull  = shared::player_hull_bounds(player->position);
      const shared::aabb_bounds_t piece = casters[0].piece.bounds;
      check(linalg::length(piece.min - hull.min) < 0.01f && linalg::length(piece.max - hull.max) < 0.01f,
            "the piece is the hull box");
      check(casters[0].piece.planes.size() == 6, "six planes: a box's shadow costs what a crate's does");
    }

    std::vector<shared::shadow_volume_t> player_volumes;
    std::vector<shared::mover_t>         player_movers;
    shared::compute_shadow_volumes(system, bvh, owner_of, {}, casters, {}, masks, player_volumes);
    shared::compute_solid_shadows(system, player_volumes, cache, player_movers);
    check(player_volumes.size() == 3, "the floor, the crate and the player each throw a volume");
    for (const shared::shadow_volume_t& volume : player_volumes)
    {
      const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
      const std::optional<shared::collision_piece_t> piece = shared::try_build_solid_shadow_piece(volume);
      const double microseconds = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
      printf("       volume of caster %u: %u side planes + %u light-facing planes + 6 reach planes -> piece of %zu faces in %.0f us\n",
             volume.caster, volume.side_plane_count, volume.light_facing_plane_count,
             piece ? piece->planes.size() : 0, microseconds);
    }
    check(player_movers.size() == 2, "the lamp's mover and the player's");
    bool player_has_a_mover = false;
    for (const shared::mover_t& mover : player_movers)
      if (mover.uid == player_uid)
      {
        player_has_a_mover = true;
        check(mover.pieces.size() == 1, "one piece: the one solid light");
        check(!mover.crushes, "a player's shadow crushes nobody");
      }
    check(player_has_a_mover, "a mover is named by the player's uid");

    const std::optional<shared::hull_sweep_hit_t> behind = drop_hull(bvh, player_movers, 100.f, 266.f);
    check(behind && behind->mover_uid == player_uid, "a hull dropped behind the player stands on the player's shadow");

    shared::predicted_world_t others{};
    others.movers = player_movers;
    const std::optional<shared::hull_sweep_hit_t> onto_other =
        shared::sweep_hull(bvh, others, {0.f, 200.f, 200.f}, {0.f, 0.f, 200.f}, shared::player_half_width,
                           shared::player_half_height);
    check(onto_other && onto_other->mover_uid == player_uid,
          "another's hull dropped onto the player is held by the shadow, which begins at the lit body");

    shared::predicted_world_t own{};
    own.movers               = player_movers;
    own.mover_uids_to_ignore = Span<const shared::entity_uid_t>(&player_uid, 1);
    const std::optional<shared::hull_sweep_hit_t> onto_self =
        shared::sweep_hull(bvh, own, {0.f, 200.f, 200.f}, {0.f, 0.f, 200.f}, shared::player_half_width,
                           shared::player_half_height);
    check(onto_self && onto_self->mover_uid == shared::null_entity_uid && onto_self->id.index == 0,
          "the player's own view falls through its own shadow to the floor");

    player->health.current_health = 0;
    shared::compute_player_shadow_casters(system, casters);
    check(casters.empty(), "a dead player casts nothing");
  }

  printf("[pin] without the flag the light throws no mover\n");
  {
    lamp->light.shadows_are_solid = false;
    lamp->light.cuts_geometry     = true;
    std::vector<shared::shadow_volume_t> plain_volumes;
    std::vector<shared::mover_t>         plain_movers;
    shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks,plain_volumes);
    shared::compute_solid_shadows(system, plain_volumes, cache, plain_movers);
    check(plain_volumes.empty(), "a cutting light's volumes reaching no receiver are culled");
    check(plain_movers.empty(), "and nothing is solid");

    const std::optional<shared::hull_sweep_hit_t> hit = drop_hull(bvh, plain_movers, 100.f, 0.f);
    check(hit && hit->mover_uid == shared::null_entity_uid && hit->id.index == 0,
          "the hull beside the crate falls to the floor");
  }

  printf("[pin] a directional light's open prism is closed at the reach\n");
  {
    const shared::entity_uid_t sun_uid = system.spawn(entities::entity_type::Directional_Light_Entity);
    entities::Directional_Light_Entity* sun = system.get<entities::Directional_Light_Entity>(sun_uid);
    sun->light.shadows_are_solid = true;
    sun->orientation             = linalg::from_axis_angle({1.f, 0.f, 0.f}, -90.f);
    std::vector<shared::shadow_volume_t> sun_volumes;
    std::vector<shared::mover_t>         sun_movers;
    shared::compute_shadow_volumes(system, bvh, owner_of, {}, {}, {}, masks,sun_volumes);
    shared::compute_solid_shadows(system, sun_volumes, cache, sun_movers);
    bool sun_has_a_mover = false;
    for (const shared::mover_t& mover : sun_movers)
      if (mover.uid == sun_uid)
      {
        sun_has_a_mover = true;
        for (const shared::collision_piece_t& piece : mover.pieces)
        {
          const vec3 size = piece.bounds.max - piece.bounds.min;
          check(size.x <= 2.f * (2048.f + shared::SOLID_SHADOW_REACH) + 1.f &&
                    size.y <= 2.f * (64.f + shared::SOLID_SHADOW_REACH) + 1.f,
                "a prism with no far cap is bounded by the reach box");
        }
      }
    check(sun_has_a_mover, "a directional light with the flag throws a mover");
  }

  printf(failure_count == 0 ? "\nsolid_shadow_test: all passed\n" : "\nsolid_shadow_test: %d FAILED\n", failure_count);
  return failure_count == 0 ? 0 : 1;
}
