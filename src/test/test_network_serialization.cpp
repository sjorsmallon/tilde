// Wire round-trip for entity snapshots: full update, then delta.
//
// REWRITTEN AT THE P5 CUTOVER, and the rewrite explains the crash this test was
// known to have. It used to declare a synthetic TestPlayer through the schema
// macros and call network::diff(nullptr, &entity, schema) for the full-update
// case -- but diff() memcmp'd the baseline unconditionally, so a null baseline
// was a null dereference. The engine never hit it, because Entity::serialize had
// its own null-baseline branch and nothing else called diff() that way. The
// segfault was in this test's contract, not in the delta path it was covering.
//
// It now drives the real path (serialize_entity / deserialize_entity) against a
// real generated entity -- which is also the only option left, since the closed
// entity_type enum means a test cannot invent an entity type any more.

#include "../shared/entities/entity_reflection.hpp"
#include "../shared/network/entity_serialization.hpp"

#include <cassert>
#include <iostream>

int main()
{
  std::cout << "[TEST] Starting Network Serialization Test..." << std::endl;

  // Values are chosen to survive write_coord, which keeps 5 fractional bits:
  // integers and multiples of 1/32 round-trip exactly, arbitrary floats do not.
  entities::Player_Entity server_tick_1;
  server_tick_1.health   = 100;
  server_tick_1.position = {10.0f, 20.0f, 0.0f};
  server_tick_1.ammo     = 30;

  entities::Player_Entity server_tick_2 = server_tick_1;
  server_tick_2.health     = 90;    // took damage
  server_tick_2.position.x = 12.0f; // moved
  // position.y, position.z and ammo deliberately unchanged.

  entities::Player_Entity client;

  {
    std::cout << "  [Subtest] Full update..." << std::endl;

    network::Bit_Writer writer;
    network::serialize_entity(writer, server_tick_1, nullptr);

    network::Bit_Reader reader(writer.buffer.data(), writer.buffer.size());
    network::deserialize_entity(reader, client);

    assert(client.health == 100);
    assert(client.position.x == 10.0f);
    assert(client.position.y == 20.0f);
    assert(client.ammo == 30);

    std::cout << "    PASSED!" << std::endl;
  }

  {
    std::cout << "  [Subtest] Delta update..." << std::endl;

    // The client already matches tick 1, so tick 2 is sent against it as the
    // baseline and only the two changed fields ride the wire.
    network::Bit_Writer writer;
    network::serialize_entity(writer, server_tick_2, &server_tick_1);

    network::Bit_Reader reader(writer.buffer.data(), writer.buffer.size());
    network::deserialize_entity(reader, client);

    assert(client.health == 90);
    assert(client.position.x == 12.0f);
    assert(client.position.y == 20.0f); // unchanged, so untouched by the delta
    assert(client.ammo == 30);          // unchanged

    std::cout << "    PASSED!" << std::endl;
  }

  {
    // A delta is smaller than a full update. That is the property the whole
    // mechanism exists for, so it is asserted rather than assumed.
    std::cout << "  [Subtest] Delta is smaller than a full update..." << std::endl;

    network::Bit_Writer full;
    network::serialize_entity(full, server_tick_2, nullptr);

    network::Bit_Writer delta;
    network::serialize_entity(delta, server_tick_2, &server_tick_1);

    std::cout << "    full=" << full.buffer.size()
              << " bytes, delta=" << delta.buffer.size() << " bytes" << std::endl;
    assert(delta.buffer.size() < full.buffer.size());

    std::cout << "    PASSED!" << std::endl;
  }

  {
    // @Networked is real now, so a field without it must never reach the wire.
    // Rocket's damage numbers are the clearest case: all four were @Networked
    // under the macro system and lost it in the P4 audit.
    std::cout << "  [Subtest] Non-networked fields stay off the wire..." << std::endl;

    entities::Rocket_Entity server_rocket;
    server_rocket.damage_amount = 123.0f;
    server_rocket.position      = {1.0f, 2.0f, 3.0f};

    network::Bit_Writer writer;
    network::serialize_entity(writer, server_rocket, nullptr);

    entities::Rocket_Entity client_rocket;
    network::Bit_Reader reader(writer.buffer.data(), writer.buffer.size());
    network::deserialize_entity(reader, client_rocket);

    assert(client_rocket.position.x == 1.0f);    // @Networked, arrives
    assert(client_rocket.damage_amount == 0.0f); // not @Networked, does not

    std::cout << "    PASSED!" << std::endl;
  }

  std::cout << "[TEST] All Network Serialization Tests Passed!" << std::endl;
  return 0;
}
