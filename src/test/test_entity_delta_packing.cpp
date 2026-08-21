// Entity deltas as they actually travel: packed into an S2C_EntityPackage.
//
// Rewritten at the P5 cutover for the same reason as
// test_network_serialization.cpp -- the closed entity_type enum means a test can
// no longer declare its own synthetic entity, so this drives a real generated
// one through the real pack/unpack path.

#include "../shared/entities/entity_reflection.hpp"
#include "../shared/network/entity_serialization.hpp"
#include "game.pb.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
  std::cout << "[TEST] Starting Entity Delta Packing Test..." << std::endl;

  entities::Player_Entity player;
  player.health   = 100;
  player.position = {10.0f, 20.0f, 0.0f};
  player.ammo     = 30;

  {
    std::cout << "  [Subtest] Packing full update..." << std::endl;

    game::S2C_EntityPackage packet;
    network::pack_entity_delta_for_update(packet, player, nullptr);

    assert(packet.has_entity_data());
    assert(packet.entity_data().size() > 0);

    entities::Player_Entity received;
    received.health = 0; // start different, so an overwrite is observable

    network::Bit_Reader reader((const network::uint8 *)packet.entity_data().data(),
                               packet.entity_data().size());
    network::deserialize_entity(reader, received);

    assert(received.health == 100);
    assert(received.position.x == 10.0f);
    assert(received.position.y == 20.0f);
    assert(received.ammo == 30);
    std::cout << "    -> Success!" << std::endl;
  }

  {
    std::cout << "  [Subtest] Packing partial delta..." << std::endl;

    entities::Player_Entity baseline;
    baseline.health   = 100;
    baseline.position = {10.0f, 20.0f, 0.0f};
    baseline.ammo     = 30;

    player.health = 90; // the only difference from the baseline

    game::S2C_EntityPackage packet;
    network::pack_entity_delta_for_update(packet, player, &baseline);

    assert(packet.has_entity_data());

    std::cout << "    Delta package size: " << packet.entity_data().size()
              << " bytes" << std::endl;

    // The receiver's current state IS the baseline; the delta only has to carry
    // what changed on top of it.
    entities::Player_Entity received = baseline;

    network::Bit_Reader reader((const network::uint8 *)packet.entity_data().data(),
                               packet.entity_data().size());
    network::deserialize_entity(reader, received);

    assert(received.health == 90);
    assert(received.position.x == 10.0f); // unchanged
    assert(received.ammo == 30);          // unchanged
    std::cout << "    -> Success!" << std::endl;
  }

  {
    std::cout << "  [Subtest] Checking Float Quantization..." << std::endl;

    entities::Player_Entity quantized;
    quantized.position = {0.123456f, -0.5f, 0.0f};

    game::S2C_EntityPackage packet;
    network::pack_entity_delta_for_update(packet, quantized, nullptr);

    entities::Player_Entity received;
    network::Bit_Reader reader((const network::uint8 *)packet.entity_data().data(),
                               packet.entity_data().size());
    network::deserialize_entity(reader, received);

    std::cout << "    Input: " << quantized.position.x
              << ", Output: " << received.position.x << std::endl;
    std::cout << "    Input: " << quantized.position.y
              << ", Output: " << received.position.y << std::endl;

    // 0.123456 * 32 = 3.95 -> rounds to 4; 4/32 = 0.125. The error is bounded
    // by half a step, 1/64 = 0.015625.
    assert(std::abs(received.position.x - quantized.position.x) < 0.016f);

    // -0.5 is exact in 1/32 steps (16/32).
    assert(received.position.y == -0.5f);

    std::cout << "    -> Success (within precision limits)!" << std::endl;
  }

  std::cout << "[TEST] All Tests Passed." << std::endl;
  return 0;
}
