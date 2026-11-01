// Entity deltas as they actually travel: serialize_entity's stream as the
// entity_data of an Entity_Package record, read back through the generated
// codec into the view a handler sees. Drives a real generated entity, since
// the closed entity_type enum leaves a test no synthetic one to declare.

#include "../shared/assets/asset_state.hpp"
#include "../shared/entities/entity_reflection.hpp"
#include "../shared/message_stream.hpp"
#include "../shared/network/entity_serialization.hpp"
#include "../shared/network/generated/messages_generated.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>
#include <vector>

namespace
{

std::vector<uint8_t> entity_bytes(const entities::Entity& entity, const entities::Entity* baseline)
{
  network::Bit_Writer writer;
  network::serialize_entity(writer, entity, baseline);
  return writer.buffer;
}

// One Entity_Package carrying `bytes` into `stream`, read back out of it. The
// returned entity_data views the stream, which is what the receive buffer is.
shared::Entity_Package travel(shared::message_stream_t& stream, Span<const uint8_t> bytes)
{
  stream.reset();
  shared::Entity_Package sent;
  sent.latest_processed_input_number = 7;
  sent.delta_from_tick               = 3;
  sent.entity_data                   = bytes;
  shared::write_entity_package(stream, sent);
  stream.finish();

  network::Bit_Reader reader(stream.writer.buffer.data(), stream.writer.buffer.size());
  assert(reader.read_bits(shared::MESSAGE_STREAM_COUNT_BITS) == 1);
  assert(reader.read_bits(16) == (uint32_t)shared::Message_Type::Entity_Package);
  const std::optional<shared::Entity_Package> received = shared::try_read_entity_package(reader);
  assert(received);
  assert(received->latest_processed_input_number == 7 && received->delta_from_tick == 3);
  assert(received->entity_data.size() == bytes.size());
  return *received;
}

} // namespace

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  std::cout << "[TEST] Starting Entity Delta Packing Test..." << std::endl;

  shared::message_stream_t stream;

  entities::Player_Entity player;
  player.health.current_health = 100;
  player.position              = {10.0f, 20.0f, 0.0f};
  player.last_fire_tick        = 30;

  {
    std::cout << "  [Subtest] Packing full update..." << std::endl;

    const std::vector<uint8_t> bytes = entity_bytes(player, nullptr);
    assert(!bytes.empty());
    const shared::Entity_Package package = travel(stream, Span<const uint8_t>(bytes));

    entities::Player_Entity received;
    received.health.current_health = 0; // start different, so an overwrite is observable

    network::Bit_Reader reader(package.entity_data.data, package.entity_data.size());
    network::deserialize_entity(reader, received);

    assert(received.health.current_health == 100);
    assert(received.position.x == 10.0f);
    assert(received.position.y == 20.0f);
    assert(received.last_fire_tick == 30);
    std::cout << "    -> Success!" << std::endl;
  }

  {
    std::cout << "  [Subtest] Packing partial delta..." << std::endl;

    entities::Player_Entity baseline;
    baseline.health.current_health = 100;
    baseline.position              = {10.0f, 20.0f, 0.0f};
    baseline.last_fire_tick        = 30;

    player.health.current_health = 90; // the only difference from the baseline

    const std::vector<uint8_t> bytes = entity_bytes(player, &baseline);
    std::cout << "    Delta package size: " << bytes.size() << " bytes" << std::endl;
    const shared::Entity_Package package = travel(stream, Span<const uint8_t>(bytes));

    // The receiver's current state IS the baseline; the delta only has to carry
    // what changed on top of it.
    entities::Player_Entity received = baseline;

    network::Bit_Reader reader(package.entity_data.data, package.entity_data.size());
    network::deserialize_entity(reader, received);

    assert(received.health.current_health == 90);
    assert(received.position.x == 10.0f); // unchanged
    assert(received.last_fire_tick == 30); // unchanged
    std::cout << "    -> Success!" << std::endl;
  }

  {
    std::cout << "  [Subtest] Checking Float Quantization..." << std::endl;

    entities::Player_Entity quantized;
    quantized.position = {0.123456f, -0.5f, 0.0f};

    const std::vector<uint8_t> bytes   = entity_bytes(quantized, nullptr);
    const shared::Entity_Package package = travel(stream, Span<const uint8_t>(bytes));

    entities::Player_Entity received;
    network::Bit_Reader     reader(package.entity_data.data, package.entity_data.size());
    network::deserialize_entity(reader, received);

    std::cout << "    Input: " << quantized.position.x << ", Output: " << received.position.x << std::endl;
    std::cout << "    Input: " << quantized.position.y << ", Output: " << received.position.y << std::endl;

    // 0.123456 * 32 = 3.95 -> rounds to 4; 4/32 = 0.125. The error is bounded
    // by half a step, 1/64 = 0.015625.
    assert(std::abs(received.position.x - quantized.position.x) < 0.016f);

    // -0.5 is exact in 1/32 steps (16/32).
    assert(received.position.y == -0.5f);

    std::cout << "    -> Success (within precision limits)!" << std::endl;
  }

  {
    std::cout << "  [Subtest] A record cut short of its bytes does not decode..." << std::endl;

    const std::vector<uint8_t> bytes = entity_bytes(player, nullptr);
    travel(stream, Span<const uint8_t>(bytes));

    // The same stream, minus its last byte: the length claims more than remains.
    network::Bit_Reader reader(stream.writer.buffer.data(), stream.writer.buffer.size() - 1);
    assert(reader.read_bits(shared::MESSAGE_STREAM_COUNT_BITS) == 1);
    assert(reader.read_bits(16) == (uint32_t)shared::Message_Type::Entity_Package);
    assert(!shared::try_read_entity_package(reader));
    std::cout << "    -> Success!" << std::endl;
  }

  std::cout << "[TEST] All Tests Passed." << std::endl;
  return 0;
}
