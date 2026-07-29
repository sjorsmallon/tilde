#pragma once

#include "../../shared/cvars/generated/cvars_generated.hpp"
#include "../../shared/linalg.hpp"

namespace client
{

// Client-side audio playback built on miniaudio. Owns one ma_engine and a pool
// of one-shot voices. Fire-and-forget: callers ask to play a sound at a point
// in the world and forget about it — finished voices are reaped in update().
//
// The backend (miniaudio's resource manager) caches decoded file data by path,
// so repeated plays of the same sound do not re-read or re-decode the file.
// This mirrors the path-keyed caching in assets::load_mesh, just owned by the
// audio backend rather than the assets module.
//
// Steam Audio (HRTF) is a later milestone (see memory project_roadmap): it
// slots in as a DSP node in miniaudio's node graph without changing this
// interface — only what is behind play_3d's spatializer changes.
struct audio_system_t
{
  audio_system_t() = default;
  ~audio_system_t();

  audio_system_t(const audio_system_t &) = delete;
  audio_system_t &operator=(const audio_system_t &) = delete;

  // Bring up the audio device + engine. Returns false (and logs) on failure;
  // a failed audio system is inert — play_* become no-ops — so a machine with
  // no audio device still runs the game.
  //
  // `cvars` is the launcher's one cvar_state_t, borrowed for the lifetime of
  // this object: the sound_* attenuation tunables are read once per voice at
  // spawn time (so a console change applies to the next sound, not to sounds
  // already in flight), which is why they are read through here rather than
  // copied at init.
  bool init(const cvars::cvar_state_t &cvars);
  void shutdown();

  bool ready() const { return impl_ != nullptr; }

  // Position the listener for spatialized playback. `forward` and `up` are the
  // camera basis vectors in world space (need not be normalized). Call once per
  // frame; also reaps voices that have finished playing.
  void update(const linalg::vec3f &listener_position,
              const linalg::vec3f &listener_forward,
              const linalg::vec3f &listener_up);

  // Play a spatialized one-shot at a world-space position. `volume` is linear
  // (1.0 = unattenuated source gain). Missing/unreadable files log once and are
  // otherwise ignored (never silently — see feedback_no_silent_failures).
  void play_3d(const char *path, const linalg::vec3f &position, float volume = 1.0f);

  // Play a non-spatialized one-shot (UI, announcer, 2D feedback).
  void play_2d(const char *path, float volume = 1.0f);

private:
  // Pimpl: miniaudio.h is enormous, so it stays out of this header. Defined in
  // audio_system.cpp.
  struct audio_impl_t *impl_ = nullptr;

  // Borrowed from the launcher via init(). Null until then; play_3d is the
  // only reader and only runs after a successful init.
  const cvars::cvar_state_t *cvars_ = nullptr;
};

} // namespace client
