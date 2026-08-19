// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/events/events.def by def_gen. Do not edit.
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

enum class Round_Phase : uint8_t
{
  Warmup = 0,
  Countdown = 1,
  Live = 2,
  Round_End = 3,
  Game_Over = 4,
};

constexpr uint32_t Round_Phase_COUNT = 5;

const char* to_string(Round_Phase value);
template <> std::optional<Round_Phase> try_from_string<Round_Phase>(std::string_view text);

// ===================================================================
// Channel: Game_Event
// ===================================================================

// Declaration order is the wire id. It is mixed into SCHEMA_HASH, so a
// reorder is a refused handshake rather than a silent remap -- a better
// outcome, not a licence. Append.
enum class game_event_type : uint16_t
{
  Rocket_Detonated = 0, // a rocket detonated, by impact or lifetime expiry
  Player_Died = 1, // a player's health crossed from >0 to <=0
  Player_Spawned = 2, // a player entered the world at a spawn point
  Round_Phase_Changed = 3, // the match entered a new round phase
};

// Not a member of the enum above, so `switch` over a game_event_type
// still warns on an unhandled case.
constexpr uint32_t GAME_EVENT_TYPE_COUNT = 4;

const char* to_string(game_event_type value);

// The shared payload. Authored in the .def rather than built into the
// generator, so adding a field every member can use is one line there.
struct Game_Event
{
};
static_assert(std::is_trivially_copyable_v<Game_Event>,
              "Game_Event must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

// One struct per member, ALWAYS -- a member with no fields of its own gets
// an empty one rather than a special case. These are STACK LOCALS: the
// encoder builds one inside the fire helper and the reader builds one on
// the way to a consumer. Neither is ever stored, which is why this family
// has no tagged union and no queue.
struct Rocket_Detonated : Game_Event
{
  uint32_t attacker_id = {};
  uint32_t victim_id = {};
  uint16_t weapon_id = {};
};
static_assert(std::is_trivially_copyable_v<Rocket_Detonated>,
              "Rocket_Detonated must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Player_Died : Game_Event
{
  uint32_t victim_id = {};
  uint32_t attacker_id = {};
  uint16_t weapon_id = {};
  bool was_headshot = {};
};
static_assert(std::is_trivially_copyable_v<Player_Died>,
              "Player_Died must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Player_Spawned : Game_Event
{
  uint32_t player_id = {};
  linalg::vec3f spawn_position = {};
  linalg::vec3f spawn_orientation = {};
};
static_assert(std::is_trivially_copyable_v<Player_Spawned>,
              "Player_Spawned must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Round_Phase_Changed : Game_Event
{
  Round_Phase phase = {};
  uint32_t round_number = {};
  uint32_t phase_end_tick = {};
};
static_assert(std::is_trivially_copyable_v<Round_Phase_Changed>,
              "Round_Phase_Changed must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

// Fire helpers. Each writes the kind, then the channel's fields, then its
// own -- straight into the stream. Nothing is queued, so no value survives
// the call and a kind can never disagree with its payload.
void fire_rocket_detonated(event_stream_t& stream, const Rocket_Detonated& payload);
void fire_player_died(event_stream_t& stream, const Player_Died& payload);
void fire_player_spawned(event_stream_t& stream, const Player_Spawned& payload);
void fire_round_phase_changed(event_stream_t& stream, const Round_Phase_Changed& payload);

// The read half, one per member. Empty when a field's value is outside
// this build's tables -- an enum id no declared value holds. That leaves
// the reader mid-record with the rest of the batch bit-packed behind it,
// so there is nothing to resynchronize to and the caller stops.
//
// The receiving side's dispatch switch is generated beside its handlers
// (client_*_bindings.cpp), because it is what references them.
[[nodiscard]] std::optional<Rocket_Detonated> try_read_rocket_detonated(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Player_Died> try_read_player_died(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Player_Spawned> try_read_player_spawned(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Round_Phase_Changed> try_read_round_phase_changed(network::Bit_Reader& reader);

// The ONE place a payload becomes characters. One overload per member, so
// a caller holding a payload has a formatter for it.
std::string to_text(const Rocket_Detonated& value);
std::string to_text(const Player_Died& value);
std::string to_text(const Player_Spawned& value);
std::string to_text(const Round_Phase_Changed& value);

// Every event PENDING in the stream, decoded back out of the bytes that
// will actually be sent. A debugger view of a queue shows what someone
// INTENDED to send; a codec bug is invisible there and visible here.
std::string game_event_stream_to_text(const event_stream_t& stream);

} // namespace shared

// --- Enum_Array support ---------------------------------------------
//
// Global scope on purpose: enum_traits is declared in shared/array.hpp,
// which knows nothing about this namespace. `count` is what sizes an
// Enum_Array over a channel tag -- adding a member to the .def resizes
// every table over it, which is what deleted the handler registry's
// hand-picked table size.

template <> struct enum_traits<shared::game_event_type>
{
  static constexpr uint32_t count = shared::GAME_EVENT_TYPE_COUNT;
};

template <> struct enum_traits<shared::Round_Phase>
{
  static constexpr uint32_t count = shared::Round_Phase_COUNT;
};

