#include "../../shared/effects/generated/effects_generated.hpp"
#include "../../shared/hitscan.hpp"
#include "../../shared/linalg.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

namespace
{

// PLACEHOLDER CONTENT: these are the knife hit sounds, standing in until there
// are bullet-flesh ones. They are the only wet impacts in resources/sounds.
constexpr const char *FLESH_IMPACT_SOUNDS[] = {
    "resources/sounds/knife_hit1.wav",
    "resources/sounds/knife_hit2.wav",
    "resources/sounds/knife_hit3.wav",
    "resources/sounds/knife_hit4.wav",
};

} // namespace

// FLESH_IMPACT: a shot that landed on a player. World-space and heard by
// everyone INCLUDING the shooter -- unlike a jump or a landing there is no
// "already played locally" case to suppress, because nothing predicts a hit.
//
// The shooter's hitmarker is a separate thing entirely and does not come
// through here: it rides Player_Entity::last_hit_tick, because it is per-viewer
// and this queue is a broadcast.
void on_flesh_impact(client_context_t &context,
                     const shared::Flesh_Impact &data)
{
  if (!context.audio)
    return;

  // Cycled rather than randomised: four identical thuds in a row is what makes
  // a sound read as canned, and a counter costs no RNG and no state to seed.
  static uint32_t next_variant = 0;
  const char     *sound = FLESH_IMPACT_SOUNDS[next_variant % std::size(FLESH_IMPACT_SOUNDS)];
  ++next_variant;

  // A headshot is louder, not different -- the distinct headshot sound belongs
  // to the shooter's hitmarker, and playing it out loud here would tell the
  // whole server where someone just got clipped in the head.
  const bool  headshot = static_cast<shared::hit_region_t>(data.surface_material) ==
                        shared::hit_region_t::Head;
  const float volume   = headshot ? 1.0f : 0.8f;

  context.audio->play_3d(sound, data.origin, volume);
}

} // namespace client::effects
