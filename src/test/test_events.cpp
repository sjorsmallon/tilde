// events_test -- the event family's round-trip guard, the way cvar_test guards
// the cvar family.
//
// Four things it holds down, in order of how expensive they are to get wrong:
//
//   1. The generated codec round-trips EVERY declared member of both channels.
//      Adding one to a .def and forgetting to exercise it is the failure this
//      test exists to make impossible, so the effect half loops over the closed
//      enum rather than listing types by hand.
//   2. A member's field order and per-type encoding are what the table says.
//      The reference below is network::write_field driven by the SAME table the
//      codec walks -- so what it pins is the ORDER (channel fields, then the
//      member's own) and the fact that the fire helper adds nothing but the
//      16-bit kind in front.
//   3. The effect batch survives its protobuf wrapper. It used to be spliced
//      into the snapshot behind a byte-align; now it rides S2C_EffectBatch,
//      whose `bytes` field is a std::string holding embedded NULs. What this
//      pins is that the client's decode expression -- a Bit_Reader over
//      .data()/.size() -- reads the stream from bit 0 with nothing in front.
//   4. The count reaches the receiver. It is backpatched into 16 reserved bits
//      rather than prepended, because joining two bitstreams needs a
//      bit-shifted copy -- so the poke and the reader have to agree.

#include "../shared/effects/generated/effects_generated.hpp"
#include "../shared/events/generated/events_generated.hpp"
#include "../shared/network/field_codec.hpp"
#include "../shared/network/quantization.hpp"
#include "game.pb.h"

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

void fill_effect(Effect& effect, uint32_t seed)
{
  effect.origin           = {exact(10.0f, 1) * (float)seed, exact(-20.0f, 2), exact(30.0f, 3)};
  effect.normal           = {0.0f, exact(1.0f, 0), 0.0f};
  effect.color            = {exact(0.0f, 8), exact(0.0f, 16), exact(1.0f, 0)};
  effect.scale            = exact(128.0f, 5);
  effect.attached_entity  = 7 + seed;
  effect.surface_material = (uint16_t)(300 + seed);
}

// Field by field, not memcmp: Effect has tail padding (46 bytes of members,
// 48 of struct) and padding bytes are indeterminate in both operands. A memcmp
// here passed locally and would have failed on someone else's build.
bool effects_match(const Effect& left, const Effect& right)
{
  return left.origin.x == right.origin.x && left.origin.y == right.origin.y &&
         left.origin.z == right.origin.z && left.normal.x == right.normal.x &&
         left.normal.y == right.normal.y && left.normal.z == right.normal.z &&
         left.color.x == right.color.x && left.color.y == right.color.y &&
         left.color.z == right.color.z && left.scale == right.scale &&
         left.attached_entity == right.attached_entity &&
         left.surface_material == right.surface_material;
}

// Fires one of each effect kind into `stream`, seeded so no two records carry
// the same bytes.
void fire_every_effect(event_stream_t& stream)
{
  Rocket_Explosion rocket_explosion; fill_effect(rocket_explosion, 0);
  Bullet_Impact    bullet_impact;    fill_effect(bullet_impact, 1);
  Footstep         footstep;         fill_effect(footstep, 2);
  Jump             jump;             fill_effect(jump, 3);
  Land             land;             fill_effect(land, 4);
  Flesh_Impact     flesh_impact;     fill_effect(flesh_impact, 5);

  fire_rocket_explosion(stream, rocket_explosion);
  fire_bullet_impact(stream, bullet_impact);
  fire_footstep(stream, footstep);
  fire_jump(stream, jump);
  fire_land(stream, land);
  fire_flesh_impact(stream, flesh_impact);
}

// Reads back what fire_every_effect wrote, asserting kind and payload per
// record. `reader` must already be positioned past the count slot.
void expect_every_effect(network::Bit_Reader& reader)
{
  Effect expected;

  for (uint32_t index = 0; index < EFFECT_TYPE_COUNT; ++index)
  {
    fill_effect(expected, index);
    assert(reader.read_bits(16) == index);

    switch ((effect_type)index)
    {
      case effect_type::Rocket_Explosion:
      {
        const std::optional<Rocket_Explosion> value = try_read_rocket_explosion(reader);
        assert(value && effects_match(*value, expected));
        break;
      }
      case effect_type::Bullet_Impact:
      {
        const std::optional<Bullet_Impact> value = try_read_bullet_impact(reader);
        assert(value && effects_match(*value, expected));
        break;
      }
      case effect_type::Footstep:
      {
        const std::optional<Footstep> value = try_read_footstep(reader);
        assert(value && effects_match(*value, expected));
        break;
      }
      case effect_type::Jump:
      {
        const std::optional<Jump> value = try_read_jump(reader);
        assert(value && effects_match(*value, expected));
        break;
      }
      case effect_type::Land:
      {
        const std::optional<Land> value = try_read_land(reader);
        assert(value && effects_match(*value, expected));
        break;
      }
      case effect_type::Flesh_Impact:
      {
        const std::optional<Flesh_Impact> value = try_read_flesh_impact(reader);
        assert(value && effects_match(*value, expected));
        break;
      }
    }
  }
}

// --- 1. Every effect round-trips ---------------------------------------------

void test_effect_round_trip()
{
  event_stream_t stream;
  fire_every_effect(stream);
  assert(stream.count == EFFECT_TYPE_COUNT);
  stream.finish();

  network::Bit_Reader reader(stream.writer.buffer.data(), stream.writer.buffer.size());
  assert(reader.read_bits(16) == EFFECT_TYPE_COUNT);
  expect_every_effect(reader);

  printf("  effect round trip (%u kinds): ok\n", EFFECT_TYPE_COUNT);
}

// --- 2. A record is the kind, then the table, in table order -----------------

void test_record_layout()
{
  Flesh_Impact payload;
  fill_effect(payload, 3);

  event_stream_t stream;
  fire_flesh_impact(stream, payload);

  // The same fields through the same codec, but driven by a table spelled out
  // here rather than by the generated one. What this pins is the ORDER -- the
  // channel's fields first, then the member's own -- and that the fire helper
  // prepends exactly a 16-bit kind and nothing else.
  //
  // Offsets come off the struct, so this stays a statement about the WIRE and
  // not about the layout the compiler chose.
  const field_info_t reference_fields[] = {
      {"origin", FIELD_TYPE_V3, (uint32_t)offsetof(Flesh_Impact, origin),
       (uint32_t)sizeof(payload.origin), 0u, NOT_A_COMPONENT, NOT_A_STRING, NOT_AN_ASSET_CLASS,
       NOT_AN_ENUM},
      {"normal", FIELD_TYPE_V3, (uint32_t)offsetof(Flesh_Impact, normal),
       (uint32_t)sizeof(payload.normal), 0u, NOT_A_COMPONENT, NOT_A_STRING, NOT_AN_ASSET_CLASS,
       NOT_AN_ENUM},
      {"color", FIELD_TYPE_V3, (uint32_t)offsetof(Flesh_Impact, color),
       (uint32_t)sizeof(payload.color), 0u, NOT_A_COMPONENT, NOT_A_STRING, NOT_AN_ASSET_CLASS,
       NOT_AN_ENUM},
      {"scale", FIELD_TYPE_F32, (uint32_t)offsetof(Flesh_Impact, scale),
       (uint32_t)sizeof(payload.scale), 0u, NOT_A_COMPONENT, NOT_A_STRING, NOT_AN_ASSET_CLASS,
       NOT_AN_ENUM},
      {"attached_entity", FIELD_TYPE_U32, (uint32_t)offsetof(Flesh_Impact, attached_entity),
       (uint32_t)sizeof(payload.attached_entity), 0u, NOT_A_COMPONENT, NOT_A_STRING,
       NOT_AN_ASSET_CLASS, NOT_AN_ENUM},
      {"surface_material", FIELD_TYPE_U16, (uint32_t)offsetof(Flesh_Impact, surface_material),
       (uint32_t)sizeof(payload.surface_material), 0u, NOT_A_COMPONENT, NOT_A_STRING,
       NOT_AN_ASSET_CLASS, NOT_AN_ENUM},
  };

  network::Bit_Writer reference;
  reference.write_bits(0, 16); // the stream's count slot
  reference.write_bits((uint32_t)effect_type::Flesh_Impact, 16);
  for (const field_info_t& field : reference_fields)
    network::write_field(reference, reinterpret_cast<const uint8_t*>(&payload), field,
                         field.offset);

  assert(stream.writer.bit_index == reference.bit_index);
  assert(stream.writer.buffer.size() == reference.buffer.size());
  assert(std::memcmp(stream.writer.buffer.data(), reference.buffer.data(),
                     reference.buffer.size()) == 0);

  printf("  record layout is the kind then the table, in order: ok\n");
}

// --- 3. The effect batch rides its own message -------------------------------
//
// This replaces an align-and-splice test. The batch used to be memcpy'd into
// every client's snapshot packet behind an entity delta of a different bit
// length, so the block started at a byte boundary neither side could compute
// and a missed align() on the reader decoded as plausible garbage. Its own
// message means the stream starts at bit 0 with no second half to keep in step.
//
// What is left to get wrong is the wrapper. A protobuf `bytes` field is a
// std::string and the payload is full of embedded NULs, so what gets exercised
// here is the expression the client actually writes -- a Bit_Reader over
// .data()/.size() -- through a real serialize/parse rather than a field copy.

void test_effect_batch_message()
{
  event_stream_t effects;
  fire_every_effect(effects);
  effects.finish();

  game::S2C_EffectBatch sent;
  sent.set_effect_data(effects.writer.buffer.data(), effects.writer.buffer.size());
  sent.set_server_tick(1234);

  std::vector<network::uint8> wire(sent.ByteSizeLong());
  assert(sent.SerializeToArray(wire.data(), (int)wire.size()));

  game::S2C_EffectBatch received;
  assert(received.ParseFromArray(wire.data(), (int)wire.size()));
  assert(received.server_tick() == 1234);
  assert(received.effect_data().size() == effects.writer.buffer.size());

  network::Bit_Reader reader(
      reinterpret_cast<const network::uint8*>(received.effect_data().data()),
      received.effect_data().size());

  assert(reader.read_bits(16) == EFFECT_TYPE_COUNT);
  expect_every_effect(reader);

  printf("  effect batch message round trip: ok\n");
}

// --- 4. Every gameplay event round-trips through the stream ------------------
//
// Fired the way the server fires them, then read back the way the client reads
// them: past the count slot, then kind + payload per record. That is the whole
// path, minus the protobuf wrapper that only carries the bytes.

void test_game_event_stream_round_trip()
{
  event_stream_t stream;
  assert(stream.empty());

  Rocket_Detonated detonated;
  detonated.attacker_id = 11;
  detonated.victim_id   = 22;
  detonated.weapon_id   = 3;
  fire_rocket_detonated(stream, detonated);

  Player_Died died;
  died.victim_id    = 44;
  died.attacker_id  = 55;
  died.weapon_id    = 6;
  died.was_headshot = true;
  fire_player_died(stream, died);

  Player_Spawned spawned;
  spawned.player_id         = 77;
  spawned.spawn_position    = {exact(1.0f, 1), exact(2.0f, 2), exact(3.0f, 3)};
  spawned.spawn_orientation = {exact(0.0f, 0), exact(90.0f, 0), exact(0.0f, 0)};
  fire_player_spawned(stream, spawned);

  Round_Phase_Changed phase_changed;
  phase_changed.phase          = Round_Phase::Round_End;
  phase_changed.round_number   = 4;
  phase_changed.phase_end_tick = 900;
  fire_round_phase_changed(stream, phase_changed);

  assert(stream.count == GAME_EVENT_TYPE_COUNT);
  assert(!stream.empty());

  stream.finish();

  network::Bit_Reader reader(stream.writer.buffer.data(), stream.writer.buffer.size());

  // The backpatched count, read exactly the way the generated dispatch reads it.
  assert(reader.read_bits(16) == GAME_EVENT_TYPE_COUNT);

  assert(reader.read_bits(16) == (uint32_t)game_event_type::Rocket_Detonated);
  const std::optional<Rocket_Detonated> read_detonated = try_read_rocket_detonated(reader);
  assert(read_detonated);
  assert(read_detonated->attacker_id == 11);
  assert(read_detonated->victim_id == 22);
  assert(read_detonated->weapon_id == 3);

  assert(reader.read_bits(16) == (uint32_t)game_event_type::Player_Died);
  const std::optional<Player_Died> read_died = try_read_player_died(reader);
  assert(read_died);
  assert(read_died->victim_id == 44);
  assert(read_died->attacker_id == 55);
  assert(read_died->weapon_id == 6);
  assert(read_died->was_headshot);

  assert(reader.read_bits(16) == (uint32_t)game_event_type::Player_Spawned);
  const std::optional<Player_Spawned> read_spawned = try_read_player_spawned(reader);
  assert(read_spawned);
  assert(read_spawned->player_id == 77);
  assert(read_spawned->spawn_position.x == exact(1.0f, 1));
  assert(read_spawned->spawn_position.y == exact(2.0f, 2));
  assert(read_spawned->spawn_position.z == exact(3.0f, 3));
  assert(read_spawned->spawn_orientation.y == exact(90.0f, 0));

  assert(reader.read_bits(16) == (uint32_t)game_event_type::Round_Phase_Changed);
  const std::optional<Round_Phase_Changed> read_phase = try_read_round_phase_changed(reader);
  assert(read_phase);
  // The enum survives the narrowing to its wire width. It is the first enum
  // field on either channel, so this is the case that would catch the codec
  // treating it as something wider.
  assert(read_phase->phase == Round_Phase::Round_End);
  assert(read_phase->round_number == 4);
  assert(read_phase->phase_end_tick == 900);

  printf("  game event stream round trip (%u kinds): ok\n", GAME_EVENT_TYPE_COUNT);
}

// --- 5. reset() keeps the allocation and re-reserves the count slot ----------

void test_stream_reset()
{
  Player_Died died;
  died.victim_id = 1;

  event_stream_t stream;
  fire_player_died(stream, died);
  const size_t capacity_after_use = stream.writer.buffer.capacity();
  assert(capacity_after_use > 0);

  stream.reset();
  assert(stream.empty());
  assert(stream.count == 0);
  // The point of reusing it: this runs at 60Hz.
  assert(stream.writer.buffer.capacity() == capacity_after_use);
  // And it comes out ready to be fired into -- the count slot is already there.
  assert(stream.writer.bit_index == 16);

  died.victim_id = 9;
  fire_player_died(stream, died);
  stream.finish();

  network::Bit_Reader reader(stream.writer.buffer.data(), stream.writer.buffer.size());
  assert(reader.read_bits(16) == 1);
  assert(reader.read_bits(16) == (uint32_t)game_event_type::Player_Died);
  const std::optional<Player_Died> read_back = try_read_player_died(reader);
  assert(read_back && read_back->victim_id == 9);

  printf("  stream reset keeps capacity and re-reserves the count: ok\n");
}

// --- 6. The formatter reads what will actually be sent -----------------------

void test_formatter()
{
  Bullet_Impact impact;
  fill_effect(impact, 0);
  const std::string impact_text = to_text(impact);
  assert(impact_text.find("Bullet_Impact") == 0);
  assert(impact_text.find("attached_entity=7") != std::string::npos);

  event_stream_t stream;
  assert(game_event_stream_to_text(stream) == "<nothing pending>");

  Player_Died died;
  died.victim_id    = 4;
  died.attacker_id  = 5;
  died.weapon_id    = 6;
  died.was_headshot = true;
  fire_player_died(stream, died);

  const std::string pending = game_event_stream_to_text(stream);
  assert(pending.find("Player_Died") == 0);
  assert(pending.find("victim_id=4") != std::string::npos);
  assert(pending.find("was_headshot=true") != std::string::npos);

  printf("  formatter: %s\n", pending.c_str());
}

} // namespace

int main()
{
  printf("events_test\n");

  test_effect_round_trip();
  test_record_layout();
  test_effect_batch_message();
  test_game_event_stream_round_trip();
  test_stream_reset();
  test_formatter();

  printf("events_test: all ok\n");
  return 0;
}
