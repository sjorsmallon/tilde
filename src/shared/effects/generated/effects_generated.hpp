// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/effects/effects.def by def_gen. Do not edit.
#pragma once

#include "array.hpp"
#include "event_stream.hpp"
#include "linalg.hpp"
#include "network/bitstream.hpp"
#include "network/network_types.hpp"
#include "reflection.hpp"
#include "span.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace shared
{

template <typename T> std::optional<T> try_from_string(std::string_view text);

// ===================================================================
// Channel: Effect
// ===================================================================

// Declaration order is the wire id. It is mixed into SCHEMA_HASH, so a
// reorder is a refused handshake rather than a silent remap -- a better
// outcome, not a licence. Append.
enum class effect_type : uint16_t
{
  Rocket_Explosion = 0, // splash particles + surface decal
  Bullet_Impact = 1, // world-surface hit
  Footstep = 2, // one foot planting
  Jump = 3, // leaving the ground
  Land = 4, // arriving back on it
  Flesh_Impact = 5, // a shot that landed on a player; surface_material carries the hit_region_t, attached_entity the victim
};

// Not a member of the enum above, so `switch` over a effect_type
// still warns on an unhandled case.
constexpr uint32_t EFFECT_TYPE_COUNT = 6;

const char* to_string(effect_type value);

// The shared payload. Authored in the .def rather than built into the
// generator, so adding a field every member can use is one line there.
struct Effect
{
  linalg::vec3f origin = {};
  linalg::vec3f normal = {};
  linalg::vec3f color = {};
  float scale = {};
  uint32_t attached_entity = {};
  uint16_t surface_material = {};
};
static_assert(std::is_trivially_copyable_v<Effect>,
              "Effect must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

// One struct per member, ALWAYS -- a member with no fields of its own gets
// an empty one rather than a special case. These are STACK LOCALS: the
// encoder builds one inside the fire helper and the reader builds one on
// the way to a consumer. Neither is ever stored, which is why this family
// has no tagged union and no queue.
struct Rocket_Explosion : Effect
{
};
static_assert(std::is_trivially_copyable_v<Rocket_Explosion>,
              "Rocket_Explosion must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Bullet_Impact : Effect
{
};
static_assert(std::is_trivially_copyable_v<Bullet_Impact>,
              "Bullet_Impact must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Footstep : Effect
{
};
static_assert(std::is_trivially_copyable_v<Footstep>,
              "Footstep must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Jump : Effect
{
};
static_assert(std::is_trivially_copyable_v<Jump>,
              "Jump must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Land : Effect
{
};
static_assert(std::is_trivially_copyable_v<Land>,
              "Land must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Flesh_Impact : Effect
{
};
static_assert(std::is_trivially_copyable_v<Flesh_Impact>,
              "Flesh_Impact must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

// Fire helpers. Each writes the kind, then the channel's fields, then its
// own -- straight into the stream. Nothing is queued, so no value survives
// the call and a kind can never disagree with its payload.
void fire_rocket_explosion(event_stream_t& stream, const Rocket_Explosion& payload);
void fire_bullet_impact(event_stream_t& stream, const Bullet_Impact& payload);
void fire_footstep(event_stream_t& stream, const Footstep& payload);
void fire_jump(event_stream_t& stream, const Jump& payload);
void fire_land(event_stream_t& stream, const Land& payload);
void fire_flesh_impact(event_stream_t& stream, const Flesh_Impact& payload);

// The read half, one per member. Empty when a field's value is outside
// this build's tables -- an enum id no declared value holds. That leaves
// the reader mid-record with the rest of the batch bit-packed behind it,
// so there is nothing to resynchronize to and the caller stops.
//
// The receiving side's dispatch switch is generated beside its handlers
// (client_*_bindings.cpp), because it is what references them.
[[nodiscard]] std::optional<Rocket_Explosion> try_read_rocket_explosion(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Bullet_Impact> try_read_bullet_impact(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Footstep> try_read_footstep(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Jump> try_read_jump(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Land> try_read_land(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Flesh_Impact> try_read_flesh_impact(network::Bit_Reader& reader);

// The ONE place a payload becomes characters. One overload per member, so
// a caller holding a payload has a formatter for it.
std::string to_text(const Rocket_Explosion& value);
std::string to_text(const Bullet_Impact& value);
std::string to_text(const Footstep& value);
std::string to_text(const Jump& value);
std::string to_text(const Land& value);
std::string to_text(const Flesh_Impact& value);

// Every event PENDING in the stream, decoded back out of the bytes that
// will actually be sent. A debugger view of a queue shows what someone
// INTENDED to send; a codec bug is invisible there and visible here.
std::string effect_stream_to_text(const event_stream_t& stream);

} // namespace shared

// --- Enum_Array support ---------------------------------------------
//
// Global scope on purpose: enum_traits is declared in shared/array.hpp,
// which knows nothing about this namespace. `count` is what sizes an
// Enum_Array over a channel tag -- adding a member to the .def resizes
// every table over it, which is what deleted the handler registry's
// hand-picked table size.

template <> struct enum_traits<shared::effect_type>
{
  static constexpr uint32_t count = shared::EFFECT_TYPE_COUNT;
};

