// The overlap loop, out of Tick() and turned into signals. entity_io_def.md
// ss11 step 4.
//
// What this replaced dispatched a Trigger_Action enum straight at the toucher,
// which is why a trigger could only ever act on whoever walked into it. It now
// emits Touched and Left and knows nothing else: the map's connections decide
// what happens, and the receiver need not be the toucher or even be near it.
//
// A jump pad is the second volume in the loop, and it emits Touched / Left like
// any other -- a pad wired to a counter still counts. It no longer LAUNCHES:
// that is player_move's, so the client predicts it (prediction_def.md ss1.6).
// Both overlap tests read the same Box_Volume through the same
// shared::get_bounds, so they cannot disagree about who is inside.
//
// Linear scan O(volumes x touchers), unchanged. The canonical replacement is
// Jolt sensor bodies in the broadphase; see "Spatial query strategy" in
// src/client/editor/readme.md for the migration trigger.
#include "trigger_system.hpp"

#include "../../shared/entities/generated/entities/jump_pad_entity_generated.hpp"
#include "../../shared/entities/generated/entities/trigger_volume_entity_generated.hpp"
#include "../../shared/entity_system.hpp"
#include "../../shared/game_session.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/player_constants.hpp"
#include "../../shared/shapes.hpp"
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


// The one walk that decides WHO can trip a trigger.
//
// Unlike the volumes above, this cannot be a component or a trait view: a
// toucher's bounds are what the thing physically IS -- the player's movement
// hull, a body's own half-extents -- and that is per TYPE, not per component.
// What IS derivable is the type LIST, which `by` declares. So the arms stay
// hand-written and the static_assert is what keeps them in step with the
// declaration: a type that can touch and is not walked here passes a check
// nothing then honours, which is one of the three defects entity_io_def.md ss2
// names.
void collect_touchers(shared::game_session_t& session, std::vector<toucher_t>& out)
{
  static_assert(
      entities::SIGNAL_ACTIVATOR_MASKS[(uint16_t)entities::entity_signal::Touched] ==
          (entities::entity_type_bit(entities::entity_type::Player_Entity) |
           entities::entity_type_bit(entities::entity_type::Physics_Body_Entity)),
      "Touched's `by` list moved: add or remove the matching pool walk below");

  out.clear();

  for (entities::Player_Entity& player :
       session.entity_system.entities_of<entities::Player_Entity>())
    out.push_back({&player, shared::player_hull_bounds(player.position)});

  for (entities::Physics_Body_Entity& body :
       session.entity_system.entities_of<entities::Physics_Body_Entity>())
    out.push_back({&body, {body.position - body.size, body.position + body.size}});
}

// One pass over every Touchable there is, whatever pool it lives in. Keyed on
// the TRAIT rather than on the two pools that carry it today: `is Touchable` is
// what entities.def declares, and `requires Box_Volume, Enabled` is what makes
// the two components non-null here by declaration rather than by coincidence.
void collect_overlaps(server_context_t& context, Span<const toucher_t> touchers,
                      std::set<trigger_overlap_t>& current)
{
  for (auto [volume, box, switch_state] :
       context.world.session.entity_system.entities_with_trait<entities::Touchable>())
  {
    // A disabled volume has no overlaps at all rather than merely no new ones,
    // so switching one off while somebody stands in it emits their Left. The
    // alternative -- freezing the pair set -- leaves a Touched with no Left,
    // and a door that never closes is worse than one that closes early.
    if (!switch_state.value)
      continue;

    // The ONE bounds function, shared with collect_movement_volumes: a pad's
    // Touched and a pad's launch must agree about who is inside it.
    const shared::aabb_bounds_t volume_bounds = shared::get_bounds(box, volume.position);

    for (const toucher_t& toucher : touchers)
    {
      if (!shared::aabbs_intersect(toucher.bounds, volume_bounds))
        continue;

      const trigger_overlap_t pair{volume.entity_id, toucher.entity->entity_id};
      current.insert(pair);

      if (context.world.previous_tick_trigger_overlaps.count(pair) > 0)
        continue;

      input_context_t emit_context{context, toucher.entity->entity_id, context.tick_number};
      entities::emit_touched(volume, entities::Touched_Data{}, emit_context);
    }
  }
}

} // namespace

void update_triggers(server_context_t& context)
{
  shared::game_session_t& session = context.world.session;

  std::vector<toucher_t> touchers;
  collect_touchers(session, touchers);

  std::set<trigger_overlap_t> current;

  collect_overlaps(context, Span<const toucher_t>(touchers), current);

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
