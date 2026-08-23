#include "audio_system.hpp"

#include "../../shared/asset.hpp"
#include "../../shared/log.hpp"
#include "../../shared/array.hpp"

#include "miniaudio.h"

#include <bit>
#include <optional>

namespace client
{

// Distance attenuation tuning. The world is in Quake/Source-style units
// (gravity 800, jumpspeed 270 ≈ 1 unit per inch), so a "room" spans hundreds
// of units. miniaudio's defaults (minDistance=1, rolloff=1) assume ~meter-scale
// and make everything past a few units silent - we need an adjustment.

// Inverse model (miniaudio default): gain = ref / (ref + rolloff*(d - ref)),
// with d clamped to [ref, max]. So volume is full within sound_reference_distance and
// falls off from there; beyond sound_max_distance_cutoff it holds at a small floor.

// note that miniaudio has different attunement models that can be switched.

constexpr uint32_t MAX_VOICE_COUNT = 32;

struct voices_t
{
  static_assert(MAX_VOICE_COUNT <= 32, "free_slots is a uint32_t bitmask");

  Array<ma_sound, MAX_VOICE_COUNT> sounds;

  // bit N set = slot N is free. All free at construction. The shift is 64-bit
  // so that the count==32 case is not UB.
  uint32_t free_slots = (uint32_t)((1ull << MAX_VOICE_COUNT) - 1ull);
};

[[nodiscard]] static std::optional<uint32_t> try_acquire_voice(voices_t& voices)
{
  if (voices.free_slots == 0)
    return std::nullopt;

  const uint32_t index = (uint32_t)std::countr_zero(voices.free_slots);
  voices.free_slots &= ~(1u << index);
  return index;
}

// Hand a slot back. The ma_sound must already be uninit'd (or never inited).
static void release_voice(voices_t& voices, uint32_t index)
{
  voices.free_slots |= 1u << index;
}

static bool is_voice_live(const voices_t& voices, uint32_t index)
{
  return (voices.free_slots & (1u << index)) == 0;
}

struct audio_impl_t
{
  ma_engine engine{};
  voices_t voices;

  // One bit per id rather than a set of paths: the id space is closed, so
  // "complain once" is an array index.
  Enum_Array<assets::sound_asset, bool> play_failure_reported;
};

// The path a sound was registered with, which is the key every
// ma_sound_init_from_file below uses. Empty for sound_asset::Missing, the one
// id with no file behind it.
static const char* registered_path_for(assets::sound_asset sound)
{
  return assets::get(assets::get_sound(sound))->registered_path.c_str();
}

// Hand every enumerated sound's ENCODED bytes to miniaudio's resource manager
// under its manifest path, once, at init. After this,
// ma_sound_init_from_file(path) is served from memory and never touches the
// filesystem -- which is the whole point, and why neither the call sites nor
// the voice pool had to move.
//
// register_encoded_data DOES NOT COPY, so the bytes must outlive the engine.
// assets::read_asset_bytes guarantees exactly that (see asset_source_t).
//
// There is no asset_exists probe here any more, and that is step 5 of
// asset_pipeline_def.md landing: a sound is a manifest id, so there is no name
// to misspell and no file that can be absent -- read_asset_bytes is fatal on a
// broken install rather than a branch every caller writes.
static void register_every_sound(audio_impl_t* impl)
{
  ma_resource_manager* resource_manager = ma_engine_get_resource_manager(&impl->engine);

  // Id 0 is Missing, which has no file: it is the declared absence of a sound,
  // not a sound.
  for (uint32_t which = 1; which < assets::sound_asset_COUNT; ++which)
  {
    const char*               path  = registered_path_for((assets::sound_asset)which);
    const Span<const uint8_t> bytes = assets::read_asset_bytes(path);

    const ma_result result = ma_resource_manager_register_encoded_data(
        resource_manager, path, bytes.data, bytes.size());
    if (result != MA_SUCCESS)
    {
      log_error("audio_system_t: could not register '{}' with the resource manager "
                "(ma_result {})",
                path, static_cast<int>(result));
    }
  }
}

audio_system_t::~audio_system_t() { shutdown(); }

bool audio_system_t::init(const cvars::cvar_state_t& rhs_cvars)
{
  cvars = &rhs_cvars;

  if (impl)
  {
    log_warning("audio_system is inited twice, which shouldn't happen.");
    return true; // already initialized
  }

  audio_impl_t* implementation = new audio_impl_t();

  ma_engine_config config = ma_engine_config_init();
  ma_result result = ma_engine_init(&config, &implementation->engine);
  if (result != MA_SUCCESS)
  {
    log_error("audio_system_t: ma_engine_init failed (ma_result {}) — audio disabled",
              static_cast<int>(result));
    delete implementation;
    return false;
  }

  impl = implementation;

  register_every_sound(impl);

  // Report the actual output endpoint miniaudio opened. With a default config
  // this is the OS default playback device (WASAPI on Windows); the device is
  // bound at init and does not follow later changes to the system default.
  ma_device *device = ma_engine_get_device(&impl->engine);
  const char *device_name =
      (device && device->playback.name[0] != '\0') ? device->playback.name
                                                    : "<unknown>";
  log_terminal("audio_system_t: miniaudio engine initialized — device '{}' ({} Hz)",
               device_name, ma_engine_get_sample_rate(&impl->engine));
  return true;
}

void audio_system_t::shutdown()
{
  if (!impl)
    return;

  for (uint32_t index = 0; index < MAX_VOICE_COUNT; ++index)
  {
    if (!is_voice_live(impl->voices, index))
      continue;

    ma_sound_uninit(&impl->voices.sounds[index]);
    release_voice(impl->voices, index);
  }

  ma_engine_uninit(&impl->engine);
  delete impl;
  impl = nullptr;
}

void audio_system_t::update(const linalg::vec3f &listener_position,
                            const linalg::vec3f &listener_forward,
                            const linalg::vec3f &listener_up)
{
  if (!impl)
    return;

  ma_engine_listener_set_position(&impl->engine, 0, listener_position.x,
                                  listener_position.y, listener_position.z);
  ma_engine_listener_set_direction(&impl->engine, 0, listener_forward.x,
                                   listener_forward.y, listener_forward.z);
  ma_engine_listener_set_world_up(&impl->engine, 0, listener_up.x,
                                  listener_up.y, listener_up.z);

  // Reap finished voices, returning their slots to the pool.
  for (uint32_t index = 0; index < MAX_VOICE_COUNT; ++index)
  {
    if (!is_voice_live(impl->voices, index))
      continue;

    ma_sound* voice = &impl->voices.sounds[index];
    if (!ma_sound_at_end(voice))
      continue;

    ma_sound_uninit(voice);
    release_voice(impl->voices, index);
  }
}

// Shared helper: claim a voice slot and init its ma_sound from `sound`, applying
// `flags`. Returns the slot index, or nullopt when the id names no file, the
// pool is exhausted, or the sound failed to load (the first and last logged once
// per id). On success the caller owns the slot until update() reaps it.
[[nodiscard]] static std::optional<uint32_t> try_start_voice(audio_impl_t* impl,
                                                             assets::sound_asset sound,
                                                             ma_uint32 flags)
{
  const char* path = registered_path_for(sound);

  // Missing is the id a call site names when the content does not exist yet
  // (there is no footstep.wav and no rocket launch sound). Silence is the right
  // outcome; saying so once is what keeps it from being a silent failure.
  if (path[0] == '\0')
  {
    bool* reported = impl->play_failure_reported.try_get(sound);
    if (reported != nullptr && !*reported)
    {
      *reported = true;
      log_error("audio_system_t: '{}' has no file behind it — nothing to play",
                assets::to_string(sound));
    }
    return std::nullopt;
  }

  const std::optional<uint32_t> slot = try_acquire_voice(impl->voices);
  if (!slot)
  {
    log_warning("audio_system_t: all {} voices are busy — dropping '{}'",
                MAX_VOICE_COUNT, path);
    return std::nullopt;
  }

  ma_sound* voice = &impl->voices.sounds[*slot];
  ma_result result =
      ma_sound_init_from_file(&impl->engine, path, flags, nullptr, nullptr, voice);
  if (result != MA_SUCCESS)
  {
    bool* reported = impl->play_failure_reported.try_get(sound);
    if (reported != nullptr && !*reported)
    {
      *reported = true;
      log_error("audio_system_t: failed to load sound '{}' (ma_result {})", path,
                static_cast<int>(result));
    }
    release_voice(impl->voices, *slot);
    return std::nullopt;
  }
  return slot;
}

void audio_system_t::play_3d(assets::sound_asset sound, const linalg::vec3f &position,
                             float volume)
{
  if (!impl)
    return;

  // MA_SOUND_FLAG_DECODE fully decodes into the resource-manager cache so the
  // first play pays the decode cost and subsequent plays are allocation-cheap.
  const std::optional<uint32_t> slot = try_start_voice(impl, sound, MA_SOUND_FLAG_DECODE);
  if (!slot)
    return;

  ma_sound* voice = &impl->voices.sounds[*slot];
  ma_sound_set_spatialization_enabled(voice, MA_TRUE);
  // Match attenuation to the game's world scale (see cvar comments above).
  // Without this, miniaudio's default minDistance=1 makes sounds inaudible
  // within a few units.
  ma_sound_set_min_distance(voice, cvars->sound_reference_distance);
  ma_sound_set_max_distance(voice, cvars->sound_max_distance_cutoff);
  ma_sound_set_rolloff(voice, cvars->sound_rolloff_factor);
  ma_sound_set_position(voice, position.x, position.y, position.z);
  ma_sound_set_volume(voice, volume);
  ma_sound_start(voice);
}

void audio_system_t::play_2d(assets::sound_asset sound, float volume)
{
  if (!impl)
    return;

  const std::optional<uint32_t> slot = try_start_voice(impl, sound, MA_SOUND_FLAG_DECODE);
  if (!slot)
    return;

  ma_sound* voice = &impl->voices.sounds[*slot];
  ma_sound_set_spatialization_enabled(voice, MA_FALSE);
  ma_sound_set_volume(voice, volume);
  ma_sound_start(voice);
}

} // namespace client
