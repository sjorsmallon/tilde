#pragma once

#include "../../shared/assets/generated/assets_generated.hpp"
#include "../../shared/cvars/generated/cvars_generated.hpp"
#include "../../shared/linalg.hpp"

namespace client
{

struct audio_system_t
{
  audio_system_t() = default;
  ~audio_system_t();

  audio_system_t(const audio_system_t &) = delete;
  audio_system_t &operator=(const audio_system_t &) = delete;

  // init is present because some attenuation settings need to be provided globally.
  bool init(const cvars::cvar_state_t &cvars);
  void shutdown();

  bool ready() const { return impl != nullptr; }

  void update(const linalg::vec3f &listener_position,
              const linalg::vec3f &listener_forward,
              const linalg::vec3f &listener_up);

  // play a spatialized one-shot at a world-space position. `volume` is linear
  // (1.0 = unattenuated source)
  void play_3d(assets::sound_asset sound, const linalg::vec3f& position, const float volume = 1.0f);
   // play a non-spatialized one-shot (UI, announcer, 2D feedback).
  void play_2d(assets::sound_asset sound, const float volume = 1.0f);

private:

  struct audio_impl_t* impl = nullptr;

  // Borrowed from the launcher via init(). Null until then; play_3d is the
  // only reader and only runs after a successful init.
  const cvars::cvar_state_t* cvars = nullptr;
};

} // namespace client
