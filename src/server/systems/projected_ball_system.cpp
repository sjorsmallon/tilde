#include "projected_ball_system.hpp"

#include "../../shared/collision/mover_path.hpp"
#include "../../shared/collision/movers.hpp"
#include "../../shared/lighting/lighting.hpp"
#include "../../shared/lighting/projected_ball.hpp"
#include "../../shared/lighting/projector_mask.hpp"
#include "../../shared/lighting/shadow_volume.hpp"
#include "entities/generated/entities/projected_ball_entity_generated.hpp"
#include "entities/generated/entities/projector_entity_generated.hpp"

namespace server
{

namespace
{

// The mask's rectangles, the footprint of the things in this projector's light, then every posed target's hitboxes
// as the silhouettes they throw on its picture (pose_all_targets ran this tick, so the volumes are the ones a shot
// would hit).
void gather_projected_ball_solids(server_context_t& context, const shared::predicted_world_storage_t& world,
                                  const entities::Projector_Entity& projector, const shared::projector_mask_t& mask)
{
  std::vector<shared::projector_mask_rectangle_t>& solids = context.projected_ball_solids;
  solids.assign(mask.rectangles.begin(), mask.rectangles.end());

  const std::optional<shared::scene_light_t> light = try_compute_projector_scene_light(context, projector);
  if (!light)
    return;

  const linalg::mat4f picture_matrix = shared::projector_picture_matrix(*light);
  if (shared::compute_projector_shadow_footprint(projector.entity_id, *light, picture_matrix,
                                                 Span<const shared::shadow_volume_t>(world.shadow_volumes),
                                                 context.projected_ball_cells, solids))
    log_warning("[projected ball] projector {} has more shadow on its picture than {} rectangles cover; the rest "
                "is shadow but not solid to the ball",
                projector.entity_id, shared::MAX_PROJECTOR_MASK_RECTANGLES);

  for (const shared::hitscan_target_t& target : context.posed_players.targets)
    for (const assets::posed_hitbox_t& hitbox : target.hitboxes)
      if (const std::optional<shared::projector_mask_rectangle_t> silhouette =
              shared::try_project_hitbox_to_picture(picture_matrix, hitbox))
        solids.push_back(*silhouette);
}

} // namespace

std::optional<shared::scene_light_t> try_compute_projector_scene_light(server_context_t&                 context,
                                                                      const entities::Projector_Entity& projector)
{
  shared::game_session_t&   session = context.world.session;
  const shared::path_pose_t placed  = shared::get_placed_pose_for_entity(projector);
  const entities::Rides*    rides   = entities::get_rides(&projector);
  const shared::path_pose_t pose =
      rides != nullptr ? shared::ridden_pose_at(session.entity_system, session.path_links, session.mover_rests,
                                                placed, *rides, context.tick_number, context.cvars->sv_tickrate)
                       : placed;
  return shared::try_convert_light_entity_to_scene_light(projector, pose);
}

void update_projected_balls(server_context_t& context, const shared::predicted_world_storage_t& world, float dt)
{
  shared::game_session_t& session = context.world.session;

  for (entities::Projected_Ball_Entity& ball :
       session.entity_system.entities_of_type<entities::Projected_Ball_Entity>())
  {
    const entities::Projector_Entity* projector =
        entities::entity_as<entities::Projector_Entity>(session.entity_system.try_find(ball.projector));

    shared::projected_ball_t state{.uv       = {ball.uv.x, ball.uv.y},
                                   .velocity = {ball.velocity.x, ball.velocity.y}};

    if (projector == nullptr || shared::projected_ball_is_outside_frame(state))
    {
      state = {.uv = {ball.spawn_uv.x, ball.spawn_uv.y}, .velocity = {ball.spawn_velocity.x, ball.spawn_velocity.y}};
      ++ball.respawn_count;
    }
    else
    {
      const shared::projector_mask_t& mask =
          shared::find_or_compute_projector_mask(session.projector_mask_cache, projector->texture);
      gather_projected_ball_solids(context, world, *projector, mask);
      const shared::projected_ball_settings_t settings{.radius      = ball.radius,
                                                       .gravity     = context.cvars->mp_projected_gravity,
                                                       .restitution = context.cvars->mp_projected_restitution,
                                                       .friction    = context.cvars->mp_projected_friction};
      state = shared::projected_ball_step(Span<const shared::projector_mask_rectangle_t>(context.projected_ball_solids),
                                          state, settings, dt);
    }

    ball.uv       = {state.uv.x, state.uv.y, 0.f};
    ball.velocity = {state.velocity.x, state.velocity.y, 0.f};
  }
}

} // namespace server
