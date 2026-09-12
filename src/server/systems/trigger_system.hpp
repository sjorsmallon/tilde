#pragma once

namespace server
{

struct server_context_t;

// Overlap between every enabled Trigger_Volume_Entity or Jump_Pad_Entity and everything Touchable
// declares it can be touched BY, turned into Touched / Left signals. The
// volume says WHEN; what a touch does is a connection, and this function does
// not know and cannot know what that is. The one exception is a jump pad,
// which launches the player itself: that is what the type IS.
//
// Runs after the move loop and after physics, so the positions it tests are the
// ones the tick ended at. Nothing it emits runs this tick -- an emit queues,
// and the queue drains at the top of the next one.
void update_triggers(server_context_t& context);

} // namespace server
