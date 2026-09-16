#pragma once

#include "../../shared/assets/generated/assets_generated.hpp"
#include "../../shared/linalg.hpp"

namespace client
{

// Rewritten every frame by update() from the sound_* cvars; see play_3d.
struct sound_attenuation_t
{
  float reference_distance  = 150.0f;
  float max_distance_cutoff = 4000.0f;
  float rolloff_factor      = 1.0f;
};

struct Audio_System
{
  Audio_System() = default;
  ~Audio_System();

  Audio_System(const Audio_System &) = delete;
  Audio_System &operator=(const Audio_System &) = delete;

  bool init();
  void shutdown();

  bool ready() const { return impl != nullptr; }

  void update(const linalg::vec3f& listener_position,
              const linalg::vec3f& listener_forward,
              const linalg::vec3f& listener_up,
              const sound_attenuation_t &rhs_attenuation);

  // play a spatialized one-shot at a world-space position. `volume` is linear
  // (1.0 = unattenuated source)
  void play_3d(assets::sound_asset sound, const linalg::vec3f& position, const float volume = 1.0f);
  // the same, but inaudible past `max_distance` instead of past the sound_* cvars' cutoff
  void play_3d_within(assets::sound_asset sound, const linalg::vec3f& position,
                      float max_distance, float volume = 1.0f);
   // play a non-spatialized one-shot (UI, announcer, 2D feedback).
  void play_2d(assets::sound_asset sound, const float volume = 1.0f);

  // Silences every voice, playing or yet to start.
  void set_muted(bool muted);

private:

  struct audio_impl_t* impl = nullptr;
  sound_attenuation_t attenuation;
};

} // namespace client
