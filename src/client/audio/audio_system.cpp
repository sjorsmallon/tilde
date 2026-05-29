#include "audio_system.hpp"

#include "../../shared/cvar.hpp"
#include "../../shared/log.hpp"

#include "miniaudio.h"

#include <unordered_set>
#include <vector>

namespace client
{

// Distance attenuation tuning. The world is in Quake/Source-style units
// (gravity 800, jumpspeed 270 ≈ 1 unit per inch), so a "room" spans hundreds
// of units. miniaudio's defaults (minDistance=1, rolloff=1) assume ~meter-scale
// and make everything past a few units silent — hence these much larger
// reference distances. Tunable live from the console to dial in by ear.
//
// Inverse model (miniaudio default): gain = ref / (ref + rolloff*(d - ref)),
// with d clamped to [ref, max]. So volume is full within sound_reference_distance and
// falls off from there; beyond sound_max_distance_cutoff it holds at a small floor.
//
// NOTE: if the falloff *curve shape* ever feels wrong after tuning the
// reference distance, the other lever is the attenuation model. Switching to
// ma_attenuation_model_linear (via ma_sound_set_attenuation_model) gives a
// straight ramp that reaches true zero at sound_max_distance_cutoff instead of
// the inverse model's long tail. Could be exposed as a cvar later.
static cvar::CVar<float> sound_reference_distance(
    "sound_reference_distance", 150.f,
    "Audio: distance (world units) within which a 3D sound is at full volume");
static cvar::CVar<float> sound_max_distance_cutoff(
    "sound_max_distance_cutoff", 4000.f,
    "Audio: distance (world units) past which 3D attenuation stops increasing");
static cvar::CVar<float> sound_rolloff_factor(
    "sound_rolloff_factor", 1.f, "Audio: 3D distance attenuation rolloff factor");

// One-shot voice. We allocate a ma_sound per active sound rather than reusing a
// fixed pool: ma_sound_init_from_file is cheap once the resource manager has the
// decoded data cached, and tracking explicit lifetimes keeps reaping simple.
struct audio_impl_t
{
  ma_engine engine{};
  std::vector<ma_sound *> active_voices;
  // Paths we have already failed to load, so a missing footstep/explosion file
  // is logged once instead of on every shot.
  std::unordered_set<std::string> warned_missing;
};

audio_system_t::~audio_system_t() { shutdown(); }

bool audio_system_t::init()
{
  if (impl_)
    return true; // already initialized

  audio_impl_t *impl = new audio_impl_t();

  ma_engine_config config = ma_engine_config_init();
  ma_result result = ma_engine_init(&config, &impl->engine);
  if (result != MA_SUCCESS)
  {
    log_error("audio_system: ma_engine_init failed (ma_result {}) — audio disabled",
              static_cast<int>(result));
    delete impl;
    return false;
  }

  impl_ = impl;

  // Report the actual output endpoint miniaudio opened. With a default config
  // this is the OS default playback device (WASAPI on Windows); the device is
  // bound at init and does not follow later changes to the system default.
  ma_device *device = ma_engine_get_device(&impl_->engine);
  const char *device_name =
      (device && device->playback.name[0] != '\0') ? device->playback.name
                                                    : "<unknown>";
  log_terminal("audio_system: miniaudio engine initialized — device '{}' ({} Hz)",
               device_name, ma_engine_get_sample_rate(&impl_->engine));
  return true;
}

void audio_system_t::shutdown()
{
  if (!impl_)
    return;

  for (ma_sound *voice : impl_->active_voices)
  {
    ma_sound_uninit(voice);
    delete voice;
  }
  impl_->active_voices.clear();

  ma_engine_uninit(&impl_->engine);
  delete impl_;
  impl_ = nullptr;
}

void audio_system_t::update(const linalg::vec3f &listener_position,
                            const linalg::vec3f &listener_forward,
                            const linalg::vec3f &listener_up)
{
  if (!impl_)
    return;

  ma_engine_listener_set_position(&impl_->engine, 0, listener_position.x,
                                  listener_position.y, listener_position.z);
  ma_engine_listener_set_direction(&impl_->engine, 0, listener_forward.x,
                                   listener_forward.y, listener_forward.z);
  ma_engine_listener_set_world_up(&impl_->engine, 0, listener_up.x,
                                  listener_up.y, listener_up.z);

  // Reap finished voices.
  std::vector<ma_sound *> &voices = impl_->active_voices;
  for (size_t i = 0; i < voices.size();)
  {
    if (ma_sound_at_end(voices[i]))
    {
      ma_sound_uninit(voices[i]);
      delete voices[i];
      voices[i] = voices.back();
      voices.pop_back();
    }
    else
    {
      ++i;
    }
  }
}

// Shared helper: init a voice from `path`, applying `flags`. Returns nullptr on
// failure (logged once per path). Caller owns the returned ma_sound.
static ma_sound *init_voice(audio_impl_t *impl, const char *path, ma_uint32 flags)
{
  ma_sound *voice = new ma_sound;
  ma_result result =
      ma_sound_init_from_file(&impl->engine, path, flags, nullptr, nullptr, voice);
  if (result != MA_SUCCESS)
  {
    if (impl->warned_missing.insert(path).second)
    {
      log_error("audio_system: failed to load sound '{}' (ma_result {})", path,
                static_cast<int>(result));
    }
    delete voice;
    return nullptr;
  }
  return voice;
}

void audio_system_t::play_3d(const char *path, const linalg::vec3f &position,
                             float volume)
{
  if (!impl_)
    return;

  // MA_SOUND_FLAG_DECODE fully decodes into the resource-manager cache so the
  // first play pays the decode cost and subsequent plays are allocation-cheap.
  ma_sound *voice = init_voice(impl_, path, MA_SOUND_FLAG_DECODE);
  if (!voice)
    return;

  ma_sound_set_spatialization_enabled(voice, MA_TRUE);
  // Match attenuation to the game's world scale (see cvar comments above).
  // Without this, miniaudio's default minDistance=1 makes sounds inaudible
  // within a few units.
  ma_sound_set_min_distance(voice, sound_reference_distance.Get());
  ma_sound_set_max_distance(voice, sound_max_distance_cutoff.Get());
  ma_sound_set_rolloff(voice, sound_rolloff_factor.Get());
  ma_sound_set_position(voice, position.x, position.y, position.z);
  ma_sound_set_volume(voice, volume);
  ma_sound_start(voice);
  impl_->active_voices.push_back(voice);
}

void audio_system_t::play_2d(const char *path, float volume)
{
  if (!impl_)
    return;

  ma_sound *voice = init_voice(impl_, path, MA_SOUND_FLAG_DECODE);
  if (!voice)
    return;

  ma_sound_set_spatialization_enabled(voice, MA_FALSE);
  ma_sound_set_volume(voice, volume);
  ma_sound_start(voice);
  impl_->active_voices.push_back(voice);
}

} // namespace client
