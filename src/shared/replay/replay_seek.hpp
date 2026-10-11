#pragma once

// replay_def.md §5, the seek: the frame a replay holds at a tick, rebuilt from
// the keyframe at or before it, and every cvar record up to it. Handed to the
// client as ONE full update, so the edge watchers see no previous frame and a
// jump fires no gunshot and no banner.

#include "network/entity_snapshot.hpp"
#include "replay_file.hpp"
#include "span.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace shared
{

struct replay_position_t
{
  // The last snapshot recorded at or before the requested tick.
  network::snapshot_frame_t frame;

  // Every Cvar_Values payload at or before the requested tick, in file order.
  // Spans into the replay's bytes.
  std::vector<Span<const uint8_t>> cvar_payloads;

  // The first record past the requested tick: where playback resumes.
  uint64_t next_record_offset = 0;
};

// Nothing when the tick precedes every keyframe or a record in the chain does
// not decode, the second logged naming the tick.
[[nodiscard]] std::optional<replay_position_t> try_reconstruct_replay_at(const replay_t& replay, uint32_t tick);

} // namespace shared
