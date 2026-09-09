// The overlap loop, out of Tick() and turned into signals. entity_io_def.md
// ss11 step 4.
//
// What this replaced dispatched a Trigger_Action enum straight at the toucher,
// which is why a trigger could only ever act on whoever walked into it. It now
// emits Touched and Left and knows nothing else: the map's connections decide
// what happens, and the receiver need not be the toucher or even be near it.
//
// Linear scan O(triggers x touchers), unchanged. The canonical replacement is
// Jolt sensor bodies in the broadphase; see "Spatial query strategy" in
// src/client/editor/readme.md for the migration trigger.
#include "trigger_system.hpp"

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

// Both bounds are what the thing physically IS, never compute_entity_bounds,
// which answers with the drawn mesh: a trigger fires on where a body is, and a
// body is what the simulation collides with. A player's is its movement hull,
// with position at the feet; a physics body's is its own `size` half-extents,
// which is what Jolt was given. Reading the mesh would also drag the asset
// system into a function that has no other reason to need it.
shared::aabb_bounds_t player_bounds(const entities::Player_Entity& player)
{
  return {{player.position.x - shared::player_half_width, player.position.y,
           player.position.z - shared::player_half_width},
          {player.position.x + shared::player_half_width,
           player.position.y + shared::player_half_height * 2.f,
           player.position.z + shared::player_half_width}};
}

// Exactly the types Touchable's `by` list names, and keeping the two in step is
// what makes `by` mean anything: it is the set an !activator row is checked
// against at load, so a type that can touch and is not listed passes a check
// nothing then honours.
//
// A physics body is here because the loop this replaced could not do it -- a
// trigger tested players only, so a crate could never fire anything, which is
// one of the three defects entity_io_def.md ss2 names.
void collect_touchers(shared::game_session_t& session, std::vector<toucher_t>& out)
{
  out.clear();

  for (entities::Player_Entity& player :
       session.entity_system.entities_of<entities::Player_Entity>())
    out.push_back({&player, player_bounds(player)});

  for (entities::Physics_Body_Entity& body :
       session.entity_system.entities_of<entities::Physics_Body_Entity>())
    out.push_back({&body, {body.position - body.size, body.position + body.size}});
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
  Span<entities::Trigger_Volume_Entity> triggers =
      session.entity_system.entities_of<entities::Trigger_Volume_Entity>();

  std::set<trigger_overlap_t> current;

  for (entities::Trigger_Volume_Entity& trigger : triggers)
  {
    // A disabled volume has no overlaps at all rather than merely no new ones,
    // so switching one off while somebody stands in it emits their Left. The
    // alternative -- freezing the pair set -- leaves a Touched with no Left,
    // and a door that never closes is worse than one that closes early.
    if (!trigger.switch_state.value)
      continue;

    const vec3f center = trigger.position + trigger.volume.position;
    const vec3f minimum = center - trigger.volume.half_extents;
    const vec3f maximum = center + trigger.volume.half_extents;

    for (const toucher_t& toucher : touchers)
    {
      if (!linalg::intersect_aabb_aabb(toucher.bounds.min, toucher.bounds.max, minimum, maximum))
        continue;

      const trigger_overlap_t pair{trigger.entity_id, toucher.entity->entity_id};
      current.insert(pair);

      if (context.world.previous_tick_trigger_overlaps.count(pair) > 0)
        continue;

      input_context_t emit_context{context, toucher.entity->entity_id, context.tick_number};
      entities::emit_touched(trigger, entities::Touched_Data{}, emit_context);
    }
  }

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
