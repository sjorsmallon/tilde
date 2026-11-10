// messages_test -- the round-trip guard for messages.def, the way cvar_test
// guards the cvar family.
//
// Four things it holds down, in order of how expensive they are to get wrong:
//
//   1. The generated codec round-trips EVERY declared message. Adding a row to
//      the .def and forgetting to exercise it is the failure this test exists
//      to make impossible, so the loop is over the closed enum rather than a
//      list of types by hand (-Werror=switch is the reminder).
//   2. A message's field order and per-type encoding are what the table says.
//      The reference below is network::write_field driven by a table spelled
//      out here -- so what it pins is the ORDER (a component's leaves in place,
//      then the row's own fields) and that the write helper adds nothing but
//      the 16-bit tag in front.
//   3. A Message payload is finished streams back to back, each starting on a
//      byte, and a datagram is cut at a record boundary: the receiver's loop
//      (count, records, align) finds every stream, and a cut stream's patched
//      count names exactly the records it kept.
//   4. The count reaches the receiver. It is backpatched into 16 reserved bits
//      rather than prepended, because joining two bitstreams needs a
//      bit-shifted copy -- so the poke and the reader have to agree.

#include "../shared/network/generated/messages_generated.hpp"
#include "../shared/network/field_codec.hpp"
#include "../shared/network/quantization.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using namespace shared;

namespace
{

// write_coord keeps 5 bits of fraction, so a value of the form
// integer + k/32 survives a round trip exactly. Every literal below is one --
// otherwise the comparisons would be testing the quantizer's rounding rather
// than the codec.
constexpr float exact(float whole, int32_t thirty_seconds)
{
  return whole + (float)thirty_seconds / 32.0f;
}

void fill_site(Effect_Site& site, uint32_t seed)
{
  site.origin           = {exact(10.0f, 1) * (float)seed, exact(-20.0f, 2), exact(30.0f, 3)};
  site.normal           = {0.0f, exact(1.0f, 0), 0.0f};
  site.color            = {exact(0.0f, 8), exact(0.0f, 16), exact(1.0f, 0)};
  site.scale            = exact(128.0f, 5);
  site.attached_entity  = 7 + seed;
  site.surface_material = (uint16_t)(300 + seed);
}

void fill_shot_impact(Shot_Impact& impact, uint32_t seed)
{
  fill_site(impact.site, seed);
  impact.region  = entities::Hit_Region::Legs;
  impact.weapon  = entities::Weapon::Scout;
  impact.trigger = entities::Fire_Trigger::Secondary;
}

Server_Map make_server_map()
{
  Server_Map map;
  map.generation   = 5;
  map.path         = "new_map.source";
  map.name         = "new_map";
  map.content_hash = 0x9abcdef0;
  return map;
}

bool server_maps_match(const Server_Map& left, const Server_Map& right)
{
  return left.generation == right.generation && left.content_hash == right.content_hash &&
         std::string(left.path.c_str()) == right.path.c_str() &&
         std::string(left.name.c_str()) == right.name.c_str();
}

Cvar_Value make_cvar_value(uint32_t which)
{
  Cvar_Value value;
  value.id = (uint16_t)(10 + which);
  (void)value.text.set(which == 0 ? "320" : which == 1 ? "quake" : "1.001");
  return value;
}

// Field by field, not memcmp: Effect_Site has tail padding and padding bytes
// are indeterminate in both operands.
bool sites_match(const Effect_Site& left, const Effect_Site& right)
{
  return left.origin.x == right.origin.x && left.origin.y == right.origin.y &&
         left.origin.z == right.origin.z && left.normal.x == right.normal.x &&
         left.normal.y == right.normal.y && left.normal.z == right.normal.z &&
         left.color.x == right.color.x && left.color.y == right.color.y &&
         left.color.z == right.color.z && left.scale == right.scale &&
         left.attached_entity == right.attached_entity &&
         left.surface_material == right.surface_material;
}

// --- 1. Every message round-trips, and the stream dump agrees ----------------
//
// A switch over the closed enum, so a row added to messages.def without a case
// here is a compile error (-Werror=switch). Each row is written once, seeded so
// no two records carry the same bytes, and read back in the same order.

// A @Reliable row only writes into the reliable scope and an @Unreliable one only
// into the datagram scope; the signatures say so, which is why there are two.
// What the one `bytes` row carries: a view, so the bytes outlive the stream.
const uint8_t PACKAGE_BYTES[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};

void append_text(std::string& text, const std::string& line)
{
  if (!text.empty())
    text += '\n';
  text += line;
}

void write_one_of_each_message(message_stream_t& stream, reliable_stream_t& reliable,
                               std::string& expected_text, std::string& expected_reliable_text)
{
  for (uint32_t index = 0; index < MESSAGE_TYPE_COUNT; ++index)
  {
    switch ((Message_Type)index)
    {
      case Message_Type::Rocket_Detonated:
      {
        Rocket_Detonated value;
        value.attacker_id = 1;
        value.victim_id   = 2;
        value.weapon_id   = 3;
        write_rocket_detonated(reliable, value);
        append_text(expected_reliable_text, to_text(value));
        break;
      }
      case Message_Type::Player_Died:
      {
        Player_Died value;
        value.victim_id    = 4;
        value.attacker_id  = 5;
        value.weapon_id    = 6;
        value.was_headshot = true;
        write_player_died(reliable, value);
        append_text(expected_reliable_text, to_text(value));
        break;
      }
      case Message_Type::Player_Spawned:
      {
        Player_Spawned value;
        value.player_id         = 7;
        value.client_slot       = 8;
        value.spawn_position    = {exact(1.0f, 0), exact(2.0f, 0), exact(3.0f, 0)};
        value.spawn_orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        write_player_spawned(reliable, value);
        append_text(expected_reliable_text, to_text(value));
        break;
      }
      case Message_Type::Objective_Reached:
      {
        Objective_Reached value;
        value.completed_by  = 9;
        value.attempt_ticks = 10;
        value.best_ticks    = 11;
        write_objective_reached(reliable, value);
        append_text(expected_reliable_text, to_text(value));
        break;
      }
      case Message_Type::Rocket_Explosion:
      {
        Rocket_Explosion value;
        fill_site(value.site, index);
        write_rocket_explosion(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Footstep:
      {
        Footstep value;
        fill_site(value.site, index);
        write_footstep(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Jump:
      {
        Jump value;
        fill_site(value.site, index);
        write_jump(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Land:
      {
        Land value;
        fill_site(value.site, index);
        write_land(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Shot_Impact:
      {
        Shot_Impact value;
        fill_shot_impact(value, index);
        write_shot_impact(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Jump_Pad_Launch:
      {
        Jump_Pad_Launch value;
        fill_site(value.site, index);
        write_jump_pad_launch(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Ping:
      {
        Ping value;
        fill_site(value.site, index);
        write_ping(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Pilot_Impact:
      {
        Pilot_Impact value;
        fill_site(value.site, index);
        write_pilot_impact(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Console_Line:
      {
        Console_Line value;
        value.line = "noclip 1";
        write_console_line(reliable, value);
        append_text(expected_reliable_text, to_text(value));
        break;
      }
      case Message_Type::Server_Text:
      {
        Server_Text value;
        value.text = "OK: noclip 1";
        write_server_text(reliable, value);
        append_text(expected_reliable_text, to_text(value));
        break;
      }
      case Message_Type::Connect:
      {
        Connect value;
        value.protocol_version = 1;
        value.player_name      = "sjors";
        value.schema_hash      = 0x11223344;
        value.asset_table_hash = 0x55667788;
        write_connect(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Disconnect:
      {
        Disconnect value;
        value.reason = "Player left";
        write_disconnect(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Accept:
      {
        Accept value;
        value.client_slot     = 3;
        value.server_tickrate = 60;
        value.map             = make_server_map();
        write_accept(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Reject:
      {
        Reject value;
        value.reason                  = "Schema mismatch";
        value.server_schema_hash      = 0xdeadbeef;
        value.server_asset_table_hash = 0;
        write_reject(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Change_Map:
      {
        Change_Map value;
        value.map = make_server_map();
        write_change_map(reliable, value);
        append_text(expected_reliable_text, to_text(value));
        break;
      }
      case Message_Type::Cvar_Values:
      {
        Cvar_Values value;
        for (uint32_t which = 0; which < 3; ++which)
          assert(value.values.try_push_back(make_cvar_value(which)));
        write_cvar_values(reliable, value);
        append_text(expected_reliable_text, to_text(value));
        break;
      }
      case Message_Type::Client_Input:
      {
        Client_Input value;
        value.input_number           = 77;
        value.input.buttons_at_start = 0xABCDull;
        value.input.view_at_start    = {1.f, 2.f};
        value.input.view_at_end      = {3.f, 4.f};
        assert(value.input.button_changes.try_push_back({.slot = 5, .buttons_after = 6, .view_after = {7.f, 8.f}}));
        assert(value.input.button_changes.try_push_back({.slot = 9, .buttons_after = 10, .view_after = {11.f, 12.f}}));
        value.held_snapshot_tick        = 13;
        value.map_content_hash          = 14;
        value.interpolated_from_tick    = 15;
        value.interpolated_towards_tick = 16;
        value.interpolation_fraction    = 0.5f;
        value.view_fov_degrees          = 90.f;
        value.view_aspect               = 1.5f;
        write_client_input(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Entity_Package:
      {
        Entity_Package value;
        value.latest_processed_input_number = -1;
        value.delta_from_tick               = 17;
        value.entity_data                   = Span<const uint8_t>(PACKAGE_BYTES);
        write_entity_package(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Shot_Debug:
      {
        Shot_Debug value;
        value.input_number           = 77;
        value.fire_subtick_slot      = 9;
        value.eye                    = {1.f, 2.f, 3.f};
        value.direction              = {0.1f, 0.2f, 0.3f};
        value.bracket_status         = Bracket_Status::Clamped;
        value.used_rewind            = true;
        value.requested_from_tick    = 10;
        value.requested_towards_tick = 11;
        value.requested_fraction     = 0.3f;
        value.used_from_tick         = 12;
        value.used_towards_tick      = 13;
        value.used_fraction          = 0.7f;
        assert(value.targets.try_push_back({.player_uid = 5, .feet_position = {4.f, 5.f, 6.f}, .body_yaw = 7.f, .view_yaw = 8.f, .view_pitch = 9.f}));
        value.hit_uid               = 5;
        value.hit_region            = entities::Hit_Region::Legs;
        value.impact_point          = {7.f, 8.f, 9.f};
        value.nearest_miss_distance = 0.f;
        write_shot_debug(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Bot_Debug:
      {
        Bot_Debug value;
        Bot_Debug_Entry entry;
        entry.slot     = 33;
        entry.goal     = Bot_Goal::Attack;
        entry.behavior = Bot_Behavior::Regular;
        assert(entry.path.try_push_back({1.f, 2.f, 3.f}));
        assert(entry.path.try_push_back({4.f, 5.f, 6.f}));
        assert(value.bots.try_push_back(entry));
        write_bot_debug(stream, value);
        append_text(expected_text, to_text(value));
        break;
      }
      case Message_Type::Map_Data_Request:
      {
        Map_Data_Request value;
        value.map_generation = 7;
        value.map_name       = "new_map";
        write_map_data_request(reliable, value);
        append_text(expected_reliable_text, to_text(value));
        break;
      }
      case Message_Type::Ghost_Available:
      {
        Ghost_Available value;
        value.party_size = 2;
        value.ghost_hash = 0xabcdef01;
        value.byte_count = 70000;
        write_ghost_available(reliable, value);
        append_text(expected_reliable_text, to_text(value));
        break;
      }
      case Message_Type::Ghost_Request:
      {
        Ghost_Request value;
        value.ghost_hash = 0xabcdef01;
        write_ghost_request(reliable, value);
        append_text(expected_reliable_text, to_text(value));
        break;
      }
    }
  }
}

// Reads a whole payload back the way the generated dispatch does -- a count,
// its records, a byte align, until the bytes run out -- and asserts each
// record's payload. Every kind must appear exactly once. Only the rows whose
// payload the writer seeds are compared field by field; the rest are held by
// the text comparison in test_every_message_to_text.
void expect_one_of_each_message(network::Bit_Reader& reader)
{
  bool seen[MESSAGE_TYPE_COUNT] = {};
  while (reader.bits_remaining() >= (size_t)MESSAGE_STREAM_COUNT_BITS)
  {
  const uint32_t count = reader.read_bits(MESSAGE_STREAM_COUNT_BITS);
  for (uint32_t record = 0; record < count; ++record)
  {
    const uint32_t index = reader.read_bits(16);
    assert(index < MESSAGE_TYPE_COUNT && !seen[index]);
    seen[index] = true;

    Effect_Site expected_site;
    fill_site(expected_site, index);

    switch ((Message_Type)index)
    {
      case Message_Type::Rocket_Detonated:
      {
        const std::optional<Rocket_Detonated> value = try_read_rocket_detonated(reader);
        assert(value && value->attacker_id == 1 && value->victim_id == 2 && value->weapon_id == 3);
        break;
      }
      case Message_Type::Player_Died:
      {
        const std::optional<Player_Died> value = try_read_player_died(reader);
        assert(value && value->victim_id == 4 && value->attacker_id == 5 && value->weapon_id == 6 &&
               value->was_headshot);
        break;
      }
      case Message_Type::Player_Spawned:
      {
        const std::optional<Player_Spawned> value = try_read_player_spawned(reader);
        assert(value && value->player_id == 7 && value->client_slot == 8);
        assert(value->spawn_position.x == exact(1.0f, 0) && value->spawn_position.z == exact(3.0f, 0));
        assert(value->spawn_orientation.w == 1.0f);
        break;
      }
      case Message_Type::Objective_Reached:
      {
        const std::optional<Objective_Reached> value = try_read_objective_reached(reader);
        assert(value && value->completed_by == 9 && value->attempt_ticks == 10 && value->best_ticks == 11);
        break;
      }
      case Message_Type::Rocket_Explosion:
      {
        const std::optional<Rocket_Explosion> value = try_read_rocket_explosion(reader);
        assert(value && sites_match(value->site, expected_site));
        break;
      }
      case Message_Type::Footstep:
      {
        const std::optional<Footstep> value = try_read_footstep(reader);
        assert(value && sites_match(value->site, expected_site));
        break;
      }
      case Message_Type::Jump:
      {
        const std::optional<Jump> value = try_read_jump(reader);
        assert(value && sites_match(value->site, expected_site));
        break;
      }
      case Message_Type::Land:
      {
        const std::optional<Land> value = try_read_land(reader);
        assert(value && sites_match(value->site, expected_site));
        break;
      }
      case Message_Type::Shot_Impact:
      {
        const std::optional<Shot_Impact> value = try_read_shot_impact(reader);
        assert(value && sites_match(value->site, expected_site));
        assert(value->region == entities::Hit_Region::Legs);
        assert(value->weapon == entities::Weapon::Scout);
        assert(value->trigger == entities::Fire_Trigger::Secondary);
        break;
      }
      case Message_Type::Jump_Pad_Launch:
      {
        const std::optional<Jump_Pad_Launch> value = try_read_jump_pad_launch(reader);
        assert(value && sites_match(value->site, expected_site));
        break;
      }
      case Message_Type::Ping:
      {
        const std::optional<Ping> value = try_read_ping(reader);
        assert(value && sites_match(value->site, expected_site));
        break;
      }
      case Message_Type::Pilot_Impact:
      {
        const std::optional<Pilot_Impact> value = try_read_pilot_impact(reader);
        assert(value && sites_match(value->site, expected_site));
        break;
      }
      case Message_Type::Console_Line:
      {
        const std::optional<Console_Line> value = try_read_console_line(reader);
        assert(value && std::string(value->line.c_str()) == "noclip 1");
        break;
      }
      case Message_Type::Server_Text:
      {
        const std::optional<Server_Text> value = try_read_server_text(reader);
        assert(value && std::string(value->text.c_str()) == "OK: noclip 1");
        break;
      }
      case Message_Type::Connect:
      {
        const std::optional<Connect> value = try_read_connect(reader);
        assert(value && value->protocol_version == 1 && std::string(value->player_name.c_str()) == "sjors");
        assert(value->schema_hash == 0x11223344 && value->asset_table_hash == 0x55667788);
        break;
      }
      case Message_Type::Disconnect:
      {
        const std::optional<Disconnect> value = try_read_disconnect(reader);
        assert(value && std::string(value->reason.c_str()) == "Player left");
        break;
      }
      case Message_Type::Accept:
      {
        const std::optional<Accept> value = try_read_accept(reader);
        assert(value && value->client_slot == 3 && value->server_tickrate == 60);
        assert(server_maps_match(value->map, make_server_map()));
        break;
      }
      case Message_Type::Reject:
      {
        const std::optional<Reject> value = try_read_reject(reader);
        assert(value && std::string(value->reason.c_str()) == "Schema mismatch");
        assert(value->server_schema_hash == 0xdeadbeef && value->server_asset_table_hash == 0);
        break;
      }
      case Message_Type::Change_Map:
      {
        const std::optional<Change_Map> value = try_read_change_map(reader);
        assert(value && server_maps_match(value->map, make_server_map()));
        break;
      }
      case Message_Type::Cvar_Values:
      {
        // The counted list: the count, then exactly that many elements, each
        // through the component's codec.
        const std::optional<Cvar_Values> value = try_read_cvar_values(reader);
        assert(value && value->values.count == 3);
        for (uint32_t which = 0; which < 3; ++which)
        {
          const Cvar_Value expected = make_cvar_value(which);
          assert(value->values[which].id == expected.id);
          assert(std::string(value->values[which].text.c_str()) == expected.text.c_str());
        }
        break;
      }
      case Message_Type::Shot_Debug:
      {
        // A file-local enum, an exact direction, and a counted list of components.
        const std::optional<Shot_Debug> value = try_read_shot_debug(reader);
        assert(value && value->input_number == 77 && value->fire_subtick_slot == 9);
        assert(value->eye.y == 2.f && value->direction.x == 0.1f && value->direction.z == 0.3f);
        assert(value->bracket_status == Bracket_Status::Clamped && value->used_rewind);
        assert(value->requested_fraction == 0.3f && value->used_fraction == 0.7f && value->used_towards_tick == 13);
        assert(value->targets.count == 1 && value->targets[0].player_uid == 5 && value->targets[0].view_pitch == 9.f);
        assert(value->hit_uid == 5 && value->hit_region == entities::Hit_Region::Legs && value->impact_point.z == 9.f);
        break;
      }
      case Message_Type::Bot_Debug:
      {
        // A counted list of components each holding a counted list of leaves.
        const std::optional<Bot_Debug> value = try_read_bot_debug(reader);
        assert(value && value->bots.count == 1 && value->bots[0].slot == 33);
        assert(value->bots[0].goal == Bot_Goal::Attack && value->bots[0].behavior == Bot_Behavior::Regular);
        assert(value->bots[0].path.count == 2 && value->bots[0].path[1].y == 5.f);
        break;
      }
      case Message_Type::Map_Data_Request:
      {
        const std::optional<Map_Data_Request> value = try_read_map_data_request(reader);
        assert(value && value->map_generation == 7 && std::string(value->map_name.c_str()) == "new_map");
        break;
      }
      case Message_Type::Ghost_Available:
      {
        const std::optional<Ghost_Available> value = try_read_ghost_available(reader);
        assert(value && value->party_size == 2 && value->ghost_hash == 0xabcdef01 && value->byte_count == 70000);
        break;
      }
      case Message_Type::Ghost_Request:
      {
        const std::optional<Ghost_Request> value = try_read_ghost_request(reader);
        assert(value && value->ghost_hash == 0xabcdef01);
        break;
      }
      case Message_Type::Client_Input:
      {
        // A component holding a counted list of components, and the exact
        // floats the predicted step adopts with no tolerance.
        const std::optional<Client_Input> value = try_read_client_input(reader);
        assert(value && value->input_number == 77 && value->input.buttons_at_start == 0xABCDull);
        assert(value->input.view_at_start.yaw == 1.f && value->input.view_at_end.pitch == 4.f);
        assert(value->input.button_changes.count == 2);
        assert(value->input.button_changes[1].slot == 9 && value->input.button_changes[1].buttons_after == 10 &&
               value->input.button_changes[1].view_after.yaw == 11.f);
        assert(value->held_snapshot_tick == 13 && value->map_content_hash == 14);
        assert(value->interpolated_from_tick == 15 && value->interpolated_towards_tick == 16 &&
               value->interpolation_fraction == 0.5f);
        assert(value->view_fov_degrees == 90.f && value->view_aspect == 1.5f);
        break;
      }
      case Message_Type::Entity_Package:
      {
        // bytes: a u32 length, then a view into the payload from the next byte boundary.
        const std::optional<Entity_Package> value = try_read_entity_package(reader);
        assert(value && value->latest_processed_input_number == -1 && value->delta_from_tick == 17);
        assert(value->entity_data.size() == sizeof(PACKAGE_BYTES) && value->entity_data[0] == 0xDE &&
               value->entity_data[4] == 0x01);
        break;
      }
    }
  }
  reader.align();
  }

  for (bool was_seen : seen)
    assert(was_seen);
}

void test_message_round_trip()
{
  message_stream_t  stream;
  reliable_stream_t reliable;
  assert(stream.empty() && reliable.records.empty());
  std::string expected_text, expected_reliable_text;
  write_one_of_each_message(stream, reliable, expected_text, expected_reliable_text);
  assert(stream.count + reliable.records.count == MESSAGE_TYPE_COUNT);
  assert(!stream.empty() && !reliable.records.empty());
  stream.finish();
  reliable.records.finish();

  // Both scopes in one payload, the way a datagram carries a slot's own records
  // and then the broadcast: each stream whole, each starting on a byte.
  std::vector<network::uint8> payload;
  append_message_stream(payload, reliable.records);
  append_message_stream(payload, stream);
  assert(payload.size() == reliable.records.writer.buffer.size() + stream.writer.buffer.size());

  network::Bit_Reader reader(payload.data(), payload.size());
  expect_one_of_each_message(reader);

  printf("  message round trip (%u kinds): ok\n", MESSAGE_TYPE_COUNT);
}

// --- 2. A record is the tag, then the table, in table order ------------------

void test_record_layout()
{
  Shot_Impact payload;
  fill_shot_impact(payload, 3);

  message_stream_t stream;
  write_shot_impact(stream, payload);

  // The same fields through the same codec, but driven by a table spelled out
  // here rather than by the generated one. What this pins is the ORDER -- the
  // component's leaves where the component sits, then the row's own fields --
  // and that the write helper prepends exactly a 16-bit tag and nothing else.
  //
  // Offsets come off the struct, so this stays a statement about the WIRE and
  // not about the layout the compiler chose.
  const field_info_t reference_fields[] = {
      {"site.origin", FIELD_TYPE_V3, (uint32_t)offsetof(Shot_Impact, site) + (uint32_t)offsetof(Effect_Site, origin),
       (uint32_t)sizeof(payload.site.origin), 0u, NOT_A_COMPONENT, NOT_A_STRING, NOT_AN_ASSET_CLASS,
       NOT_AN_ENUM},
      {"site.normal", FIELD_TYPE_V3, (uint32_t)offsetof(Shot_Impact, site) + (uint32_t)offsetof(Effect_Site, normal),
       (uint32_t)sizeof(payload.site.normal), 0u, NOT_A_COMPONENT, NOT_A_STRING, NOT_AN_ASSET_CLASS,
       NOT_AN_ENUM},
      {"site.color", FIELD_TYPE_V3, (uint32_t)offsetof(Shot_Impact, site) + (uint32_t)offsetof(Effect_Site, color),
       (uint32_t)sizeof(payload.site.color), 0u, NOT_A_COMPONENT, NOT_A_STRING, NOT_AN_ASSET_CLASS,
       NOT_AN_ENUM},
      {"site.scale", FIELD_TYPE_F32, (uint32_t)offsetof(Shot_Impact, site) + (uint32_t)offsetof(Effect_Site, scale),
       (uint32_t)sizeof(payload.site.scale), 0u, NOT_A_COMPONENT, NOT_A_STRING, NOT_AN_ASSET_CLASS,
       NOT_AN_ENUM},
      {"site.attached_entity", FIELD_TYPE_ENTITY_UID,
       (uint32_t)offsetof(Shot_Impact, site) + (uint32_t)offsetof(Effect_Site, attached_entity),
       (uint32_t)sizeof(payload.site.attached_entity), 0u, NOT_A_COMPONENT, NOT_A_STRING,
       NOT_AN_ASSET_CLASS, NOT_AN_ENUM},
      {"site.surface_material", FIELD_TYPE_U16,
       (uint32_t)offsetof(Shot_Impact, site) + (uint32_t)offsetof(Effect_Site, surface_material),
       (uint32_t)sizeof(payload.site.surface_material), 0u, NOT_A_COMPONENT, NOT_A_STRING,
       NOT_AN_ASSET_CLASS, NOT_AN_ENUM},
      {"region", FIELD_TYPE_ENUM, (uint32_t)offsetof(Shot_Impact, region),
       (uint32_t)sizeof(payload.region), 0u, NOT_A_COMPONENT, NOT_A_STRING, NOT_AN_ASSET_CLASS,
       &entities::ENUM_INFOS[(uint32_t)entities::enum_type::Hit_Region]},
      {"weapon", FIELD_TYPE_ENUM, (uint32_t)offsetof(Shot_Impact, weapon),
       (uint32_t)sizeof(payload.weapon), 0u, NOT_A_COMPONENT, NOT_A_STRING, NOT_AN_ASSET_CLASS,
       &entities::ENUM_INFOS[(uint32_t)entities::enum_type::Weapon]},
      {"trigger", FIELD_TYPE_ENUM, (uint32_t)offsetof(Shot_Impact, trigger),
       (uint32_t)sizeof(payload.trigger), 0u, NOT_A_COMPONENT, NOT_A_STRING, NOT_AN_ASSET_CLASS,
       &entities::ENUM_INFOS[(uint32_t)entities::enum_type::Fire_Trigger]},
  };

  network::Bit_Writer reference;
  reference.write_bits(0, 16); // the stream's count slot
  reference.write_bits((uint32_t)Message_Type::Shot_Impact, 16);
  for (const field_info_t& field : reference_fields)
    network::write_field(reference, reinterpret_cast<const uint8_t*>(&payload), field,
                         field.offset, nullptr);

  assert(stream.writer.bit_index == reference.bit_index);
  assert(stream.writer.buffer.size() == reference.buffer.size());
  assert(std::memcmp(stream.writer.buffer.data(), reference.buffer.data(),
                     reference.buffer.size()) == 0);

  printf("  record layout is the tag then the table, in order: ok\n");
}

// --- 3. A datagram is cut at a record boundary ------------------------------
//
// The broadcast is appended to a slot's datagram only as far as it fits, whole
// records only. What the cut hands over must decode as a stream in its own
// right: a patched count naming exactly the records kept, nothing of the one
// that did not fit.

void test_records_that_fit()
{
  message_stream_t broadcast;
  for (uint32_t which = 0; which < 5; ++which)
  {
    Footstep step;
    fill_site(step.site, which);
    write_footstep(broadcast, step);
  }
  broadcast.finish();
  assert(broadcast.record_start_bits.size() == 5);

  const size_t whole = broadcast.writer.buffer.size();

  // Everything fits: the whole stream, count untouched.
  std::vector<network::uint8> payload;
  assert(append_message_records_that_fit(payload, broadcast, whole) == 0);
  assert(payload.size() == whole);
  assert(std::memcmp(payload.data(), broadcast.writer.buffer.data(), whole) == 0);

  // A budget one byte short of the whole loses exactly the last record.
  payload.clear();
  const uint16_t dropped = append_message_records_that_fit(payload, broadcast, whole - 1);
  assert(dropped == 1);
  assert(payload.size() < whole && payload.size() >= (broadcast.record_start_bits[4] + 7) / 8);
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    assert(reader.read_bits(16) == 4);
    for (uint32_t which = 0; which < 4; ++which)
    {
      assert(reader.read_bits(16) == (uint32_t)Message_Type::Footstep);
      const std::optional<Footstep> step = try_read_footstep(reader);
      Effect_Site                   expected;
      fill_site(expected, which);
      assert(step && sites_match(step->site, expected));
    }
    reader.align();
    assert(reader.bits_remaining() == 0);
  }

  // A budget below the first record appends nothing and reports every record dropped.
  payload.clear();
  assert(append_message_records_that_fit(payload, broadcast, 3) == 5);
  assert(payload.empty());

  // A cut stream followed by another stream still reads as two: the receiver's
  // loop aligns between them.
  payload.clear();
  append_message_records_that_fit(payload, broadcast, whole - 1);
  message_stream_t trailing;
  Ping             ping;
  fill_site(ping.site, 9);
  write_ping(trailing, ping);
  trailing.finish();
  append_message_stream(payload, trailing);
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    uint32_t            streams = 0, records = 0;
    while (reader.bits_remaining() >= (size_t)MESSAGE_STREAM_COUNT_BITS)
    {
      const uint32_t count = reader.read_bits(MESSAGE_STREAM_COUNT_BITS);
      for (uint32_t record = 0; record < count; ++record)
      {
        const uint32_t kind = reader.read_bits(16);
        if (kind == (uint32_t)Message_Type::Footstep)
          assert(try_read_footstep(reader));
        else
        {
          assert(kind == (uint32_t)Message_Type::Ping);
          const std::optional<Ping> read_back = try_read_ping(reader);
          assert(read_back && read_back->site.attached_entity == 7 + 9);
        }
        ++records;
      }
      reader.align();
      ++streams;
    }
    assert(streams == 2 && records == 5);
  }

  printf("  a datagram is cut at a record boundary: ok\n");
}

// --- 4. reset() keeps the allocation and re-reserves the count slot ----------

void test_stream_reset()
{
  Player_Died died;
  died.victim_id = 1;

  reliable_stream_t reliable;
  message_stream_t& stream = reliable.records;
  write_player_died(reliable, died);
  const size_t capacity_after_use = stream.writer.buffer.capacity();
  assert(capacity_after_use > 0);

  stream.reset();
  assert(stream.empty());
  assert(stream.count == 0);
  // The point of reusing it: this runs at 60Hz.
  assert(stream.writer.buffer.capacity() == capacity_after_use);
  // And it comes out ready to be written into -- the count slot is already there.
  assert(stream.writer.bit_index == 16);

  died.victim_id = 9;
  write_player_died(reliable, died);
  stream.finish();

  network::Bit_Reader reader(stream.writer.buffer.data(), stream.writer.buffer.size());
  assert(reader.read_bits(16) == 1);
  assert(reader.read_bits(16) == (uint32_t)Message_Type::Player_Died);
  const std::optional<Player_Died> read_back = try_read_player_died(reader);
  assert(read_back && read_back->victim_id == 9);

  printf("  stream reset keeps capacity and re-reserves the count: ok\n");
}

// --- 5. Every message reaches to_text, and the stream dump agrees -------------

void test_every_message_to_text()
{
  message_stream_t  stream;
  reliable_stream_t reliable;
  std::string       expected_text, expected_reliable_text;
  write_one_of_each_message(stream, reliable, expected_text, expected_reliable_text);

  assert(stream.count + reliable.records.count == MESSAGE_TYPE_COUNT);
  assert(message_stream_to_text(stream) == expected_text);
  assert(message_stream_to_text(reliable.records) == expected_reliable_text);

  for (uint32_t index = 0; index < MESSAGE_TYPE_COUNT; ++index)
  {
    assert(std::string(to_string((Message_Type)index)).size() > 0);
    assert(std::string(description((Message_Type)index)).size() > 0);
  }

  printf("  every message through to_text (%u kinds): ok\n", MESSAGE_TYPE_COUNT);
}

// --- 6. The formatter reads what will actually be sent -----------------------

void test_formatter()
{
  Shot_Impact impact;
  fill_shot_impact(impact, 0);
  const std::string impact_text = to_text(impact);
  assert(impact_text.find("Shot_Impact") == 0);
  assert(impact_text.find("attached_entity=7") != std::string::npos);
  assert(impact_text.find("region=Legs") != std::string::npos);

  reliable_stream_t reliable;
  assert(message_stream_to_text(reliable.records) == "<nothing pending>");

  Player_Died died;
  died.victim_id    = 4;
  died.attacker_id  = 5;
  died.weapon_id    = 6;
  died.was_headshot = true;
  write_player_died(reliable, died);

  const std::string pending = message_stream_to_text(reliable.records);
  assert(pending.find("Player_Died") == 0);
  assert(pending.find("victim_id=4") != std::string::npos);
  assert(pending.find("was_headshot=true") != std::string::npos);

  printf("  formatter: %s\n", pending.c_str());
}

} // namespace

int main()
{
  printf("messages_test\n");

  test_message_round_trip();
  test_record_layout();
  test_records_that_fit();
  test_stream_reset();
  test_every_message_to_text();
  test_formatter();

  printf("messages_test: all ok\n");
  return 0;
}
