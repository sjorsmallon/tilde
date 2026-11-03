// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/network/messages.def by def_gen. Do not edit.
#pragma once

#include "array.hpp"
#include "entities/generated/entities_core_generated.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "message_stream.hpp"
#include "network/bitstream.hpp"
#include "network/network_types.hpp"
#include "pascal_array.hpp"
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

enum class Bracket_Status : uint8_t
{
  Ok = 0,
  Clamped = 1,
  Absent = 2,
  Malformed = 3,
  Unheld = 4,
};

constexpr uint32_t Bracket_Status_COUNT = 5;

const char* to_string(Bracket_Status value);
template <> std::optional<Bracket_Status> try_from_string<Bracket_Status>(std::string_view text);

enum class Bot_Goal : uint8_t
{
  Idle = 0,
  Chase = 1,
  Attack = 2,
  Retreat = 3,
};

constexpr uint32_t Bot_Goal_COUNT = 4;

const char* to_string(Bot_Goal value);
template <> std::optional<Bot_Goal> try_from_string<Bot_Goal>(std::string_view text);

enum class Bot_Behavior : uint8_t
{
  Idle = 0,
  Chase = 1,
  Regular = 2,
};

constexpr uint32_t Bot_Behavior_COUNT = 3;

const char* to_string(Bot_Behavior value);
template <> std::optional<Bot_Behavior> try_from_string<Bot_Behavior>(std::string_view text);

struct Effect_Site
{
  linalg::vec3f origin = {};
  linalg::vec3f normal = {};
  linalg::vec3f color = {};
  float scale = {};
  shared::entity_uid_t attached_entity = {};
  uint16_t surface_material = {};
};
static_assert(std::is_trivially_copyable_v<Effect_Site>,
              "Effect_Site must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Server_Map
{
  uint32_t generation = {};
  network::pascal_string_t<128> path = {};
  network::pascal_string_t<64> name = {};
  uint32_t content_hash = {};
};
static_assert(std::is_trivially_copyable_v<Server_Map>,
              "Server_Map must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Cvar_Value
{
  uint16_t id = {};
  network::pascal_string_t<64> text = {};
};
static_assert(std::is_trivially_copyable_v<Cvar_Value>,
              "Cvar_Value must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct View
{
  float yaw = {};
  float pitch = {};
};
static_assert(std::is_trivially_copyable_v<View>,
              "View must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Button_Change
{
  uint8_t slot = {};
  uint64_t buttons_after = {};
  View view_after = {};
};
static_assert(std::is_trivially_copyable_v<Button_Change>,
              "Button_Change must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Tick_Input
{
  uint64_t buttons_at_start = {};
  View view_at_start = {};
  View view_at_end = {};
  Pascal_Array<Button_Change, 8> button_changes = {};
};
static_assert(std::is_trivially_copyable_v<Tick_Input>,
              "Tick_Input must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Shot_Debug_Target
{
  shared::entity_uid_t player_uid = {};
  linalg::vec3f feet_position = {};
  float body_yaw = {};
  float view_yaw = {};
  float view_pitch = {};
};
static_assert(std::is_trivially_copyable_v<Shot_Debug_Target>,
              "Shot_Debug_Target must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Bot_Debug_Entry
{
  int32_t slot = {};
  Bot_Goal goal = {};
  Bot_Behavior behavior = {};
  Pascal_Array<linalg::vec3f, 32> path = {};
};
static_assert(std::is_trivially_copyable_v<Bot_Debug_Entry>,
              "Bot_Debug_Entry must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

// A component's fields as they sit inside a record, in declaration order:
// what every row carrying one encodes it through. try_read_ is empty when a
// field's value is outside this build's tables.
void write_effect_site(network::Bit_Writer& writer, const Effect_Site& value);
[[nodiscard]] std::optional<Effect_Site> try_read_effect_site(network::Bit_Reader& reader);
std::string to_text(const Effect_Site& value);

void write_server_map(network::Bit_Writer& writer, const Server_Map& value);
[[nodiscard]] std::optional<Server_Map> try_read_server_map(network::Bit_Reader& reader);
std::string to_text(const Server_Map& value);

void write_cvar_value(network::Bit_Writer& writer, const Cvar_Value& value);
[[nodiscard]] std::optional<Cvar_Value> try_read_cvar_value(network::Bit_Reader& reader);
std::string to_text(const Cvar_Value& value);

void write_view(network::Bit_Writer& writer, const View& value);
[[nodiscard]] std::optional<View> try_read_view(network::Bit_Reader& reader);
std::string to_text(const View& value);

void write_button_change(network::Bit_Writer& writer, const Button_Change& value);
[[nodiscard]] std::optional<Button_Change> try_read_button_change(network::Bit_Reader& reader);
std::string to_text(const Button_Change& value);

void write_tick_input(network::Bit_Writer& writer, const Tick_Input& value);
[[nodiscard]] std::optional<Tick_Input> try_read_tick_input(network::Bit_Reader& reader);
std::string to_text(const Tick_Input& value);

void write_shot_debug_target(network::Bit_Writer& writer, const Shot_Debug_Target& value);
[[nodiscard]] std::optional<Shot_Debug_Target> try_read_shot_debug_target(network::Bit_Reader& reader);
std::string to_text(const Shot_Debug_Target& value);

void write_bot_debug_entry(network::Bit_Writer& writer, const Bot_Debug_Entry& value);
[[nodiscard]] std::optional<Bot_Debug_Entry> try_read_bot_debug_entry(network::Bit_Reader& reader);
std::string to_text(const Bot_Debug_Entry& value);

// Declaration order is the wire id, mixed into SCHEMA_HASH: a reorder is a
// refused handshake rather than a silent remap. Append.
enum class Message_Type : uint16_t
{
  Rocket_Detonated = 0, // a rocket detonated, by impact or lifetime expiry
  Player_Died = 1, // a player's health crossed from >0 to <=0
  Player_Spawned = 2, // a player entered the world at a spawn point
  Objective_Reached = 3, // the level's objective was completed
  Rocket_Explosion = 4, // splash particles + surface decal
  Footstep = 5, // one foot planting
  Jump = 6, // leaving the ground
  Land = 7, // arriving back on it
  Shot_Impact = 8, // a shot landed; attached_entity is what it hit, 0 for the world
  Jump_Pad_Launch = 9, // a pad threw somebody; normal is the launch direction, attached_entity who
  Ping = 10, // a player pinged a spot; attached_entity is who pinged, normal the surface
  Pilot_Impact = 11, // a piloted rocket ended on a surface; origin the contact, normal the surface, attached_entity the pilot
  Console_Line = 12, // a console line typed at the client, run by the server as its caller
  Server_Text = 13, // a line for the client's console
  Connect = 14, // a peer asking for a slot
  Disconnect = 15, // a client leaving on purpose
  Accept = 16, // the answer to a Connect that got a slot
  Reject = 17, // the answer to a Connect that did not
  Change_Map = 18, // the server switched map; follow it
  Cvar_Values = 19, // @Mirrored values: the whole set at connect, the changed subset after
  Map_Data_Request = 20, // the client lacks the compiled package of the map at this generation; stream it
  Ghost_Available = 21, // the ghost for the party being run, by hash; 0 is none
  Ghost_Request = 22, // the client's cached ghost did not hash to the announced one
  Client_Input = 23, // one tick of input plus what the client HOLDS, restated every tick: the snapshot, the map, the screen
  Entity_Package = 24, // the snapshot at the datagram's tick: a delta against the tick the client said it holds, or a full update
  Shot_Debug = 25, // the server's view of one of the shooter's own shots, to the shooter alone (sv_shot_debug)
  Bot_Debug = 26, // every bot's goal and the path ahead of it, each tick while bots exist; more than the bound is several rows
};

constexpr uint32_t MESSAGE_TYPE_COUNT = 27;

const char* to_string(Message_Type value);
const char* description(Message_Type value);

// One struct per message. A stack local: the writer builds one and the
// reader builds one on the way to a handler; nothing stores one.
struct Rocket_Detonated
{
  static constexpr Message_Type type = Message_Type::Rocket_Detonated;
  uint32_t attacker_id = {};
  uint32_t victim_id = {};
  uint16_t weapon_id = {};
};
static_assert(std::is_trivially_copyable_v<Rocket_Detonated>,
              "Rocket_Detonated must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Player_Died
{
  static constexpr Message_Type type = Message_Type::Player_Died;
  uint32_t victim_id = {};
  uint32_t attacker_id = {};
  uint16_t weapon_id = {};
  bool was_headshot = {};
};
static_assert(std::is_trivially_copyable_v<Player_Died>,
              "Player_Died must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Player_Spawned
{
  static constexpr Message_Type type = Message_Type::Player_Spawned;
  uint32_t player_id = {};
  int32_t client_slot = {};
  linalg::vec3f spawn_position = {};
  linalg::quatf spawn_orientation = {};
};
static_assert(std::is_trivially_copyable_v<Player_Spawned>,
              "Player_Spawned must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Objective_Reached
{
  static constexpr Message_Type type = Message_Type::Objective_Reached;
  uint32_t completed_by = {};
  uint32_t attempt_ticks = {};
  uint32_t best_ticks = {};
};
static_assert(std::is_trivially_copyable_v<Objective_Reached>,
              "Objective_Reached must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Rocket_Explosion
{
  static constexpr Message_Type type = Message_Type::Rocket_Explosion;
  Effect_Site site = {};
};
static_assert(std::is_trivially_copyable_v<Rocket_Explosion>,
              "Rocket_Explosion must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Footstep
{
  static constexpr Message_Type type = Message_Type::Footstep;
  Effect_Site site = {};
};
static_assert(std::is_trivially_copyable_v<Footstep>,
              "Footstep must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Jump
{
  static constexpr Message_Type type = Message_Type::Jump;
  Effect_Site site = {};
};
static_assert(std::is_trivially_copyable_v<Jump>,
              "Jump must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Land
{
  static constexpr Message_Type type = Message_Type::Land;
  Effect_Site site = {};
};
static_assert(std::is_trivially_copyable_v<Land>,
              "Land must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Shot_Impact
{
  static constexpr Message_Type type = Message_Type::Shot_Impact;
  Effect_Site site = {};
  entities::Hit_Region region = {};
  entities::Weapon weapon = {};
  entities::Fire_Trigger trigger = {};
};
static_assert(std::is_trivially_copyable_v<Shot_Impact>,
              "Shot_Impact must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Jump_Pad_Launch
{
  static constexpr Message_Type type = Message_Type::Jump_Pad_Launch;
  Effect_Site site = {};
};
static_assert(std::is_trivially_copyable_v<Jump_Pad_Launch>,
              "Jump_Pad_Launch must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Ping
{
  static constexpr Message_Type type = Message_Type::Ping;
  Effect_Site site = {};
};
static_assert(std::is_trivially_copyable_v<Ping>,
              "Ping must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Pilot_Impact
{
  static constexpr Message_Type type = Message_Type::Pilot_Impact;
  Effect_Site site = {};
};
static_assert(std::is_trivially_copyable_v<Pilot_Impact>,
              "Pilot_Impact must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Console_Line
{
  static constexpr Message_Type type = Message_Type::Console_Line;
  network::pascal_string_t<255> line = {};
};
static_assert(std::is_trivially_copyable_v<Console_Line>,
              "Console_Line must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Server_Text
{
  static constexpr Message_Type type = Message_Type::Server_Text;
  network::pascal_string_t<255> text = {};
};
static_assert(std::is_trivially_copyable_v<Server_Text>,
              "Server_Text must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Connect
{
  static constexpr Message_Type type = Message_Type::Connect;
  uint32_t protocol_version = {};
  network::pascal_string_t<32> player_name = {};
  uint32_t schema_hash = {};
  uint32_t asset_table_hash = {};
};
static_assert(std::is_trivially_copyable_v<Connect>,
              "Connect must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Disconnect
{
  static constexpr Message_Type type = Message_Type::Disconnect;
  network::pascal_string_t<64> reason = {};
};
static_assert(std::is_trivially_copyable_v<Disconnect>,
              "Disconnect must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Accept
{
  static constexpr Message_Type type = Message_Type::Accept;
  int32_t client_slot = {};
  uint32_t server_tickrate = {};
  Server_Map map = {};
};
static_assert(std::is_trivially_copyable_v<Accept>,
              "Accept must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Reject
{
  static constexpr Message_Type type = Message_Type::Reject;
  network::pascal_string_t<255> reason = {};
  uint32_t server_schema_hash = {};
  uint32_t server_asset_table_hash = {};
};
static_assert(std::is_trivially_copyable_v<Reject>,
              "Reject must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Change_Map
{
  static constexpr Message_Type type = Message_Type::Change_Map;
  Server_Map map = {};
};
static_assert(std::is_trivially_copyable_v<Change_Map>,
              "Change_Map must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Cvar_Values
{
  static constexpr Message_Type type = Message_Type::Cvar_Values;
  Pascal_Array<Cvar_Value, 64> values = {};
};
static_assert(std::is_trivially_copyable_v<Cvar_Values>,
              "Cvar_Values must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Map_Data_Request
{
  static constexpr Message_Type type = Message_Type::Map_Data_Request;
  uint32_t map_generation = {};
  network::pascal_string_t<64> map_name = {};
};
static_assert(std::is_trivially_copyable_v<Map_Data_Request>,
              "Map_Data_Request must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Ghost_Available
{
  static constexpr Message_Type type = Message_Type::Ghost_Available;
  uint32_t party_size = {};
  uint32_t ghost_hash = {};
  uint32_t byte_count = {};
};
static_assert(std::is_trivially_copyable_v<Ghost_Available>,
              "Ghost_Available must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Ghost_Request
{
  static constexpr Message_Type type = Message_Type::Ghost_Request;
  uint32_t ghost_hash = {};
};
static_assert(std::is_trivially_copyable_v<Ghost_Request>,
              "Ghost_Request must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Client_Input
{
  static constexpr Message_Type type = Message_Type::Client_Input;
  int32_t input_number = {};
  Tick_Input input = {};
  uint32_t held_snapshot_tick = {};
  uint32_t map_content_hash = {};
  uint32_t interpolated_from_tick = {};
  uint32_t interpolated_towards_tick = {};
  float interpolation_fraction = {};
  float view_fov_degrees = {};
  float view_aspect = {};
};
static_assert(std::is_trivially_copyable_v<Client_Input>,
              "Client_Input must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Entity_Package
{
  static constexpr Message_Type type = Message_Type::Entity_Package;
  int32_t latest_processed_input_number = {};
  uint32_t delta_from_tick = {};
  Span<const uint8_t> entity_data = {};
};
static_assert(std::is_trivially_copyable_v<Entity_Package>,
              "Entity_Package must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Shot_Debug
{
  static constexpr Message_Type type = Message_Type::Shot_Debug;
  int32_t input_number = {};
  uint8_t fire_subtick_slot = {};
  linalg::vec3f eye = {};
  linalg::vec3f direction = {};
  Bracket_Status bracket_status = {};
  bool used_rewind = {};
  uint32_t requested_from_tick = {};
  uint32_t requested_towards_tick = {};
  float requested_fraction = {};
  uint32_t used_from_tick = {};
  uint32_t used_towards_tick = {};
  float used_fraction = {};
  Pascal_Array<Shot_Debug_Target, 16> targets = {};
  shared::entity_uid_t hit_uid = {};
  entities::Hit_Region hit_region = {};
  linalg::vec3f impact_point = {};
  float nearest_miss_distance = {};
};
static_assert(std::is_trivially_copyable_v<Shot_Debug>,
              "Shot_Debug must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

struct Bot_Debug
{
  static constexpr Message_Type type = Message_Type::Bot_Debug;
  Pascal_Array<Bot_Debug_Entry, 16> bots = {};
};
static_assert(std::is_trivially_copyable_v<Bot_Debug>,
              "Bot_Debug must stay trivially copyable: the codec addresses its fields "
              "through byte offsets");

// Each writes the tag, then the fields, straight into the stream. A @Reliable
// row writes only into a reliable_stream_t and an @Unreliable row only into a
// message_stream_t: the signature is what keeps a message on its transport.
void write_rocket_detonated(reliable_stream_t& stream, const Rocket_Detonated& payload);
void write_player_died(reliable_stream_t& stream, const Player_Died& payload);
void write_player_spawned(reliable_stream_t& stream, const Player_Spawned& payload);
void write_objective_reached(reliable_stream_t& stream, const Objective_Reached& payload);
void write_rocket_explosion(message_stream_t& stream, const Rocket_Explosion& payload);
void write_footstep(message_stream_t& stream, const Footstep& payload);
void write_jump(message_stream_t& stream, const Jump& payload);
void write_land(message_stream_t& stream, const Land& payload);
void write_shot_impact(message_stream_t& stream, const Shot_Impact& payload);
void write_jump_pad_launch(message_stream_t& stream, const Jump_Pad_Launch& payload);
void write_ping(message_stream_t& stream, const Ping& payload);
void write_pilot_impact(message_stream_t& stream, const Pilot_Impact& payload);
void write_console_line(reliable_stream_t& stream, const Console_Line& payload);
void write_server_text(reliable_stream_t& stream, const Server_Text& payload);
void write_connect(message_stream_t& stream, const Connect& payload);
void write_disconnect(message_stream_t& stream, const Disconnect& payload);
void write_accept(message_stream_t& stream, const Accept& payload);
void write_reject(message_stream_t& stream, const Reject& payload);
void write_change_map(reliable_stream_t& stream, const Change_Map& payload);
void write_cvar_values(reliable_stream_t& stream, const Cvar_Values& payload);
void write_map_data_request(reliable_stream_t& stream, const Map_Data_Request& payload);
void write_ghost_available(reliable_stream_t& stream, const Ghost_Available& payload);
void write_ghost_request(reliable_stream_t& stream, const Ghost_Request& payload);
void write_client_input(message_stream_t& stream, const Client_Input& payload);
void write_entity_package(message_stream_t& stream, const Entity_Package& payload);
void write_shot_debug(message_stream_t& stream, const Shot_Debug& payload);
void write_bot_debug(message_stream_t& stream, const Bot_Debug& payload);

// Empty when a field's value is outside this build's tables, which leaves
// the reader mid-record: the caller stops.
[[nodiscard]] std::optional<Rocket_Detonated> try_read_rocket_detonated(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Player_Died> try_read_player_died(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Player_Spawned> try_read_player_spawned(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Objective_Reached> try_read_objective_reached(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Rocket_Explosion> try_read_rocket_explosion(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Footstep> try_read_footstep(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Jump> try_read_jump(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Land> try_read_land(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Shot_Impact> try_read_shot_impact(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Jump_Pad_Launch> try_read_jump_pad_launch(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Ping> try_read_ping(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Pilot_Impact> try_read_pilot_impact(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Console_Line> try_read_console_line(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Server_Text> try_read_server_text(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Connect> try_read_connect(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Disconnect> try_read_disconnect(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Accept> try_read_accept(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Reject> try_read_reject(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Change_Map> try_read_change_map(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Cvar_Values> try_read_cvar_values(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Map_Data_Request> try_read_map_data_request(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Ghost_Available> try_read_ghost_available(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Ghost_Request> try_read_ghost_request(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Client_Input> try_read_client_input(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Entity_Package> try_read_entity_package(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Shot_Debug> try_read_shot_debug(network::Bit_Reader& reader);
[[nodiscard]] std::optional<Bot_Debug> try_read_bot_debug(network::Bit_Reader& reader);

std::string to_text(const Rocket_Detonated& value);
std::string to_text(const Player_Died& value);
std::string to_text(const Player_Spawned& value);
std::string to_text(const Objective_Reached& value);
std::string to_text(const Rocket_Explosion& value);
std::string to_text(const Footstep& value);
std::string to_text(const Jump& value);
std::string to_text(const Land& value);
std::string to_text(const Shot_Impact& value);
std::string to_text(const Jump_Pad_Launch& value);
std::string to_text(const Ping& value);
std::string to_text(const Pilot_Impact& value);
std::string to_text(const Console_Line& value);
std::string to_text(const Server_Text& value);
std::string to_text(const Connect& value);
std::string to_text(const Disconnect& value);
std::string to_text(const Accept& value);
std::string to_text(const Reject& value);
std::string to_text(const Change_Map& value);
std::string to_text(const Cvar_Values& value);
std::string to_text(const Map_Data_Request& value);
std::string to_text(const Ghost_Available& value);
std::string to_text(const Ghost_Request& value);
std::string to_text(const Client_Input& value);
std::string to_text(const Entity_Package& value);
std::string to_text(const Shot_Debug& value);
std::string to_text(const Bot_Debug& value);

// Every record pending in the stream, decoded back out of the bytes that
// will actually be sent.
std::string message_stream_to_text(const message_stream_t& stream);

} // namespace shared

template <> struct enum_traits<shared::Message_Type>
{
  static constexpr uint32_t count = shared::MESSAGE_TYPE_COUNT;
};

template <> struct enum_traits<shared::Bracket_Status>
{
  static constexpr uint32_t count = shared::Bracket_Status_COUNT;
};

template <> struct enum_traits<shared::Bot_Goal>
{
  static constexpr uint32_t count = shared::Bot_Goal_COUNT;
};

template <> struct enum_traits<shared::Bot_Behavior>
{
  static constexpr uint32_t count = shared::Bot_Behavior_COUNT;
};

