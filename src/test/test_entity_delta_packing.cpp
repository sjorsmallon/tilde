#include "../shared/entity.hpp"
#include "../shared/network/entity_serialization.hpp"
#include "../shared/network/schema.hpp"
#include "game.pb.h"
#include <cassert>
#include <iostream>

using namespace network;

// --- Define an Entity for Testing ---

class TestPlayer : public Entity
{
public:
  SCHEMA_FIELD(int32, health, Schema_Flags::Networked);
  SCHEMA_FIELD(float32, x, Schema_Flags::Networked);
  SCHEMA_FIELD(float32, y, Schema_Flags::Networked);
  SCHEMA_FIELD(int32, ammo, Schema_Flags::Networked);

  DECLARE_SCHEMA(TestPlayer)
};

// --- Define Schema ---
BEGIN_SCHEMA(TestPlayer)
REGISTER_FIELD(health)
REGISTER_FIELD(x)
REGISTER_FIELD(y)
REGISTER_FIELD(ammo)
END_SCHEMA(TestPlayer)

int main()
{
  std::cout << "[TEST] Starting Entity Delta Packing Test..." << std::endl;

  // 1. Register Schema
  TestPlayer::register_schema();

  // 2. Create Entity
  TestPlayer player;
  player.health = 100;
  player.x = 10.0f;
  player.y = 20.0f;
  player.ammo = 30;

  // 3. Pack Delta (Full update, no baseline)
  {
    std::cout << "  [Subtest] Packing full update..." << std::endl;
    game::S2C_EntityPackage packet;
    pack_entity_delta_for_update(packet, player, (const TestPlayer *)nullptr);

    assert(packet.is_delta() == true);
    assert(packet.has_entity_data());
    assert(packet.entity_data().size() > 0);

    // Verify content by deserializing
    TestPlayer received_player;
    // Initialize with different values to ensure overwrite
    received_player.health = 0;

    Bit_Reader reader((const uint8 *)packet.entity_data().data(),
                      packet.entity_data().size());
    received_player.deserialize(reader);

    assert(received_player.health == 100);
    assert(received_player.x == 10.0f);
    assert(received_player.y == 20.0f);
    assert(received_player.ammo == 30);
    std::cout << "    -> Success!" << std::endl;
  }

  // 4. Pack Delta (Partial update with baseline)
  {
    std::cout << "  [Subtest] Packing partial delta..." << std::endl;
    TestPlayer baseline_player;
    baseline_player.health = 100; // Same
    baseline_player.x = 10.0f;    // Same
    baseline_player.y = 20.0f;    // Same
    baseline_player.ammo = 30;    // Same

    // Change only health
    player.health = 90;

    game::S2C_EntityPackage packet;
    pack_entity_delta_for_update(packet, player, &baseline_player);

    assert(packet.is_delta() == true);
    assert(packet.has_entity_data());

    // Size should be smaller than full update + overhead
    // New format: Mask (4 bits/4 fields) + Data.
    // 4 fields -> 4 bits mask -> 1 byte (aligned).
    // Changed Health: 90 -> VarInt (approx 1-2 bytes)
    // Total approx 2-3 bytes?
    std::cout << "    Delta package size: " << packet.entity_data().size()
              << " bytes" << std::endl;

    // Verify content
    TestPlayer received_player;
    received_player.health = 100; // Current client state (baseline)
    received_player.x = 10.0f;
    received_player.y = 20.0f;
    received_player.ammo = 30;

    Bit_Reader reader((const uint8 *)packet.entity_data().data(),
                      packet.entity_data().size());
    received_player.deserialize(reader); // Apply delta

    assert(received_player.health == 90);
    assert(received_player.x == 10.0f); // Should remain same
    std::cout << "    -> Success!" << std::endl;
  }

  // 5. Quantization Check
  {
    std::cout << "  [Subtest] Checking Float Quantization..." << std::endl;
    TestPlayer q_player;
    q_player.health = 100;
    q_player.x = 0.123456f; // Testing this value
    q_player.y = -0.5f;     // Testing negative 0.5 (exact)
    q_player.ammo = 0;

    game::S2C_EntityPackage packet;
    pack_entity_delta_for_update(packet, q_player, (const TestPlayer *)nullptr);

    TestPlayer received;
    received.health = 0;
    received.x = 0.0f;
    received.y = 0.0f;

    Bit_Reader reader((const uint8 *)packet.entity_data().data(),
                      packet.entity_data().size());
    received.deserialize(reader);

    std::cout << "    Input: " << q_player.x << ", Output: " << received.x
              << std::endl;
    std::cout << "    Input: " << q_player.y << ", Output: " << received.y
              << std::endl;

    // 0.123456 * 32 = 3.95 -> round to 4. 4/32 = 0.125.
    // expected error is within 1/64 = 0.015625
    float diff = std::abs(received.x - q_player.x);
    assert(diff < 0.016f);

    // -0.5 is exact in 1/32 steps (16/32).
    assert(received.y == -0.5f);

    std::cout << "    -> Success (within precision limits)!" << std::endl;
  }

  std::cout << "[TEST] All Tests Passed." << std::endl;
  return 0;
}
