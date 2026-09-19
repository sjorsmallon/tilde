#include "../../shared/entities/generated/traits/playable_generated.hpp"
#include "../entity_io_context.hpp"

namespace entities
{

void play(Entity&, Playback& state, const Play_Data&, server::input_context_t&)
{
  ++state.play_count;
}

void stop_playing(Entity&, Playback& state, const Stop_Playing_Data&, server::input_context_t&)
{
  ++state.stop_count;
}

} // namespace entities
