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

struct audio_system_t
{
  audio_system_t() = default;
  ~audio_system_t();

  audio_system_t(const audio_system_t &) = delete;
  audio_system_t &operator=(const audio_system_t &) = delete;

  bool init();
  void shutdown();

  bool ready() const { return impl != nullptr; }

  void update(const linalg::vec3f &listener_position,
              const linalg::vec3f &listener_forward,
              const linalg::vec3f &listener_up,
              const sound_attenuation_t &rhs_attenuation);

  // play a spatialized one-shot at a world-space position. `volume` is linear
  // (1.0 = unattenuated source)
  void play_3d(assets::sound_asset sound, const linalg::vec3f& position, const float volume = 1.0f);
   // play a non-spatialized one-shot (UI, announcer, 2D feedback).
  void play_2d(assets::sound_asset sound, const float volume = 1.0f);

private:

  struct audio_impl_t* impl = nullptr;
  sound_attenuation_t attenuation;
};

} // namespace client
