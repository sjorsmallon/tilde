#pragma once

// The volumes player_move tests the hull against, cut out of the session the
// way the BVH is cut out of the geometry. prediction_def.md §1.
//
// A flat list of plain VALUES, not entities: player_move is a pure function of
// its arguments and stays one, and an entity pointer would hand it the whole
// session. What a pad IS -- an orientation and a speed -- is flattened here,
// once per tick, into the velocity it delivers.
//
// This is what makes a jump pad PREDICTED. The launch used to be a server
// system's write after the move loop, so the client felt it a round trip late
// and was then snapped by reconciliation. Quake 3 is the model: BG_TouchJumpPad
// compiled into both sides, run inside the client's prediction step.

#include "aabb.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "span.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

struct Entity_System;

enum class movement_volume_kind_t : uint8_t
{
  Jump_Pad,
};

struct movement_volume_t
{
  entity_uid_t           uid     = null_entity_uid;
  movement_volume_kind_t kind    = movement_volume_kind_t::Jump_Pad;
  aabb_bounds_t          bounds  = {};

  // Replicated state, not map data, which is why it is carried rather than
  // filtered out at collection: a disabled volume is still a volume, and the
  // step wants to say so.
  bool                   enabled = true;

  linalg::vec3f          launch_velocity = {};
};

// ONE exhaustive switch over entity_type: a @predicted type has an arm that
// produces a volume, every other type produces nothing, and -Werror=switch
// polices it. The generator cannot write this -- flattening a pad into a launch
// velocity is per-type logic -- which is why @predicted is a flag with one rule
// rather than an emitter.
void collect_movement_volumes(Entity_System&                  system,
                              std::vector<movement_volume_t>& out);

// Where a cosmetic produced by a launch should play: the volume's own centre,
// read out of the list the step used rather than re-resolved through the entity
// it came from, so the sound cannot land somewhere the launch did not. Answers
// `fallback` for a uid the list no longer holds.
[[nodiscard]] inline linalg::vec3f movement_volume_origin(
    Span<const movement_volume_t> volumes, entity_uid_t uid,
    const linalg::vec3f& fallback)
{
  for (const movement_volume_t& volume : volumes)
    if (volume.uid == uid)
      return get_aabb_center(volume.bounds);
  return fallback;
}

} // namespace shared
