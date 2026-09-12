// The overlap loop, out of Tick() and turned into signals. entity_io_def.md
// ss11 step 4.
//
// What this replaced dispatched a Trigger_Action enum straight at the toucher,
// which is why a trigger could only ever act on whoever walked into it. It now
// emits Touched and Left and knows nothing else: the map's connections decide
// what happens, and the receiver need not be the toucher or even be near it.
//
// A jump pad is the second volume in the loop, and the one exception to "knows
// nothing else": a pad launching is per-TYPE behaviour, the way a rocket
// flying is, so it happens here rather than in a connection.
//
// Linear scan O(volumes x touchers), unchanged. The canonical replacement is
// Jolt sensor bodies in the broadphase; see "Spatial query strategy" in
// src/client/editor/readme.md for the migration trigger.
#include "trigger_system.hpp"

#include "../../shared/effects/generated/effects_generated.hpp"
#include "../../shared/entities/generated/entities/jump_pad_entity_generated.hpp"
#include "../../shared/entities/generated/entities/trigger_volume_entity_generated.hpp"
#include "../../shared/entity_system.hpp"
#include "../../shared/game_session.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/player_constants.hpp"
#include "../entity_io_context.hpp"
#include "../server_context.hpp"

namespace server
{

namespace
{

struct toucher_t
{
  entities::Entity*  entity = nullptr;
  shared::aabb_bounds_t bounds;
};


void collect_touchers(shared::game_session_t& session, std::vector<toucher_t>& out)
{
  out.clear();

  for (entities::Player_Entity& player :
       session.entity_system.entities_of<entities::Player_Entity>())
    out.push_back({&player, shared::player_hull_bounds(player.position)});

  for (entities::Physics_Body_Entity& body :
       session.entity_system.entities_of<entities::Physics_Body_Entity>())
    out.push_back({&body, {body.position - body.size, body.position + body.size}});
}

// A player's velocity is ours to write; a crate's belongs to Jolt and is
// overwritten by the next physics step, so a pad only ever announces one.
void launch_from_jump_pad(server_context_t& context, const entities::Jump_Pad_Entity& pad,
                          entities::Entity& toucher)
{
  entities::Player_Entity* player = entities::entity_as<entities::Player_Entity>(&toucher);
  if (player == nullptr)
    return;

  const vec3f direction = linalg::forward(pad.orientation);
  player->velocity      = direction * pad.launch_speed;

  shared::Jump_Pad_Launch fx{};
  fx.origin          = pad.position;
  fx.normal          = direction;
  fx.attached_entity = player->entity_id;
  shared::fire_jump_pad_launch(context.outgoing.effects, fx);
}

// One pass over one pool of volumes. `on_touched` runs on the rising edge,
// after the emit, with the volume and whoever entered it.
template <typename Volume_T, typename On_Touched_T>
void collect_overlaps(server_context_t& context, Span<Volume_T> volumes,
                      Span<const toucher_t> touchers, std::set<trigger_overlap_t>& current,
                      On_Touched_T&& on_touched)
{
  for (Volume_T& volume : volumes)
  {
    // A disabled volume has no overlaps at all rather than merely no new ones,
    // so switching one off while somebody stands in it emits their Left. The
    // alternative -- freezing the pair set -- leaves a Touched with no Left,
    // and a door that never closes is worse than one that closes early.
    if (!volume.switch_state.value)
      continue;

    const vec3f center  = volume.position + volume.volume.position;
    const vec3f minimum = center - volume.volume.half_extents;
    const vec3f maximum = center + volume.volume.half_extents;

    for (const toucher_t& toucher : touchers)
    {
      if (!linalg::intersect_aabb_aabb(toucher.bounds.min, toucher.bounds.max, minimum, maximum))
        continue;

      const trigger_overlap_t pair{volume.entity_id, toucher.entity->entity_id};
      current.insert(pair);

      if (context.world.previous_tick_trigger_overlaps.count(pair) > 0)
        continue;

      input_context_t emit_context{context, toucher.entity->entity_id, context.tick_number};
      entities::emit_touched(volume, entities::Touched_Data{}, emit_context);
      on_touched(volume, *toucher.entity);
    }
  }
}

} // namespace

void update_triggers(server_context_t& context)
{
  shared::game_session_t& session = context.world.session;

  std::vector<toucher_t> touchers;
  collect_touchers(session, touchers);

  // Both pools are fetched HERE rather than reused from earlier in the tick:
  // this is a walk over everything, not a lookup of one, so it wants the pool
  // -- but a pool pointer grabbed hundreds of lines ago would have survived
  // every spawn and destroy in between.
  std::set<trigger_overlap_t> current;

  collect_overlaps(context, session.entity_system.entities_of<entities::Trigger_Volume_Entity>(),
                   Span<const toucher_t>(touchers), current,
                   [](entities::Trigger_Volume_Entity&, entities::Entity&) {});

  collect_overlaps(context, session.entity_system.entities_of<entities::Jump_Pad_Entity>(),
                   Span<const toucher_t>(touchers), current,
                   [&context](entities::Jump_Pad_Entity& pad, entities::Entity& toucher) {
                     launch_from_jump_pad(context, pad, toucher);
                   });

  // The falling edge, from the pairs that were there and are not. A trigger or
  // a toucher that stopped existing is one of them, and it still emits: the
  // sender's connections are the session's, not the entity's, so a destroyed
  // volume's Left still reaches whatever it was wired to.
  for (const trigger_overlap_t& pair : context.world.previous_tick_trigger_overlaps)
  {
    if (current.count(pair) > 0)
      continue;

    const entities::Entity* sender = session.entity_system.try_find(pair.trigger);
    if (sender == nullptr)
      continue;

    input_context_t emit_context{context, pair.toucher, context.tick_number};
    entities::emit_left(*sender, entities::Left_Data{}, emit_context);
  }

  context.world.previous_tick_trigger_overlaps = std::move(current);
}

} // namespace server
