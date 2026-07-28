// Snapshot delta compression: the acked-baseline rule, and the change mask.
//
// The rule this file exists to pin down: a delta may only be taken against a
// snapshot the receiver has ACKED. Deltaing against the last-SENT snapshot is
// the obvious implementation and it is wrong on an unreliable channel -- the
// third subtest reproduces the exact desync it causes, so the reason the rule
// exists stays visible instead of being folklore in a comment.
//
// Everything here drives the real shared pieces: network::Snapshot_History,
// serialize_entity / deserialize_entity. What it cannot reach is the wiring in
// server_impl.cpp and play_state.cpp -- that is exercised by running a
// MyGame_Client against a MyGame_Server.

#include "../shared/entities/entity_reflection.hpp"
#include "../shared/network/entity_serialization.hpp"
#include "../shared/network/snapshot_history.hpp"

#include <cassert>
#include <iostream>
#include <vector>

namespace
{

// A stand-in for the per-tick frames the server and client actually store. The
// history only requires the `tick` member; the payload is the caller's problem.
struct Test_Frame
{
  uint32_t                 tick = 0;
  entities::Player_Entity  player;
};

// Round-trips one entity through the wire path and returns the delta size in
// bytes, so a subtest can assert a delta is actually smaller than a full update.
size_t transmit(const entities::Player_Entity& source,
                const entities::Player_Entity* baseline,
                entities::Player_Entity&       destination,
                network::changed_fields_t*     out_changed = nullptr)
{
  network::Bit_Writer writer;
  network::serialize_entity(writer, source, baseline);

  network::Bit_Reader reader(writer.buffer.data(), writer.buffer.size());
  network::deserialize_entity(reader, destination, out_changed);

  return writer.buffer.size();
}

uint32_t leaf_index_of(entities::entity_type type, const char* dotted_name)
{
  const Span<const entities::leaf_field_t> leaves = entities::networked_leaf_fields(type);
  for (uint32_t index = 0; index < leaves.size(); ++index)
    if (leaves[index].name == dotted_name)
      return index;

  std::cerr << "    !! no networked leaf named '" << dotted_name << "'\n";
  assert(false && "leaf_index_of: unknown field -- did entities.def change?");
  return 0;
}

} // namespace

int main()
{
  std::cout << "[TEST] Starting Snapshot Delta Test..." << std::endl;

  {
    std::cout << "  [Subtest] Snapshot history ring..." << std::endl;

    network::Snapshot_History<Test_Frame, 4> history;

    assert(history.find(0) == nullptr);  // 0 is the "no baseline" sentinel
    assert(history.find(1) == nullptr);  // nothing stored yet
    assert(history.baseline() == nullptr);

    for (uint32_t tick = 1; tick <= 4; ++tick)
    {
      Test_Frame& frame  = history.slot_for(tick);
      frame.tick         = tick;
      frame.player.health = (int32_t)(100 - tick);
    }

    assert(history.find(1) != nullptr);
    assert(history.find(4)->player.health == 96);

    // Tick 5 lands in tick 1's slot: the window is 4 wide, so 1 has aged out.
    Test_Frame& frame = history.slot_for(5);
    frame.tick        = 5;
    assert(history.find(1) == nullptr);
    assert(history.find(5) != nullptr);

    history.acknowledge(3);
    assert(history.acked_tick == 3);
    history.acknowledge(2); // reordered datagram carrying a stale ack
    assert(history.acked_tick == 3);
    assert(history.baseline() != nullptr && history.baseline()->tick == 3);

    // An ack for a tick that has aged out is not a lie, just useless: the
    // lookup misses and the caller falls back to a full update.
    history.acknowledge(1000);
    assert(history.baseline() == nullptr);

    history.clear();
    assert(history.acked_tick == 0);
    assert(history.find(5) == nullptr);

    std::cout << "    -> Success!" << std::endl;
  }

  {
    std::cout << "  [Subtest] Delta against an acked baseline is exact..." << std::endl;

    entities::Player_Entity server_state;
    server_state.health   = 100;
    server_state.ammo     = 30;
    server_state.position = {10.0f, 20.0f, 30.0f};

    entities::Player_Entity client_state;
    const size_t full_size = transmit(server_state, nullptr, client_state);
    assert(client_state.health == 100);
    assert(client_state.ammo == 30);

    // One field moves. The client holds the exact baseline, so the delta only
    // has to carry that field.
    entities::Player_Entity acked_baseline = client_state;
    server_state.health                    = 75;

    const size_t delta_size = transmit(server_state, &acked_baseline, client_state);
    assert(client_state.health == 75);
    assert(client_state.ammo == 30);           // untouched, carried by the baseline
    assert(client_state.position.x == 10.0f);  // ditto

    std::cout << "    Full update: " << full_size << " bytes, delta: " << delta_size
              << " bytes" << std::endl;
    assert(delta_size < full_size);

    std::cout << "    -> Success!" << std::endl;
  }

  {
    std::cout << "  [Subtest] A dropped snapshot desyncs a last-sent baseline..."
              << std::endl;

    // The bug this whole mechanism exists to prevent, reproduced. The server
    // deltas against what it LAST SENT and the packet carrying the change is
    // lost. The field then stops changing, so it is never in a delta again --
    // and the client is wrong about it forever.
    entities::Player_Entity server_state;
    server_state.health = 100;

    entities::Player_Entity client_state;
    transmit(server_state, nullptr, client_state); // tick 1, arrives
    assert(client_state.health == 100);

    entities::Player_Entity last_sent = server_state;

    server_state.health = 40; // tick 2: took damage
    {
      entities::Player_Entity discarded_by_packet_loss = client_state;
      transmit(server_state, &last_sent, discarded_by_packet_loss);
      last_sent = server_state; // server assumes it landed -- this is the error
    }

    // Tick 3: health is stable at 40, so a delta against last_sent says nothing
    // about it, and the client keeps rendering 100.
    transmit(server_state, &last_sent, client_state);
    assert(client_state.health == 100); // WRONG, and it never self-corrects

    // Now the same loss with the acked rule. The client's ack still names tick
    // 1, so the server deltas against tick 1 -- health differs there, so it
    // rides again and the client converges.
    entities::Player_Entity acked_baseline;
    acked_baseline.health = 100;

    entities::Player_Entity recovered_client;
    recovered_client.health = 100;
    transmit(server_state, &acked_baseline, recovered_client);
    assert(recovered_client.health == 40);

    std::cout << "    -> Success (desync reproduced, ack rule fixes it)!" << std::endl;
  }

  {
    std::cout << "  [Subtest] Changed-field mask names exactly what moved..." << std::endl;

    entities::Player_Entity baseline;
    baseline.health   = 100;
    baseline.position = {1.0f, 2.0f, 3.0f};

    entities::Player_Entity server_state = baseline;
    server_state.health                  = 60;

    entities::Player_Entity  client_state = baseline;
    network::changed_fields_t changed;
    transmit(server_state, &baseline, client_state, &changed);

    const uint32_t health_leaf   = leaf_index_of(entities::entity_type::Player_Entity, "health");
    const uint32_t position_leaf = leaf_index_of(entities::entity_type::Player_Entity, "position");

    assert(changed.any());
    assert(changed.is_set(health_leaf));
    assert(!changed.is_set(position_leaf));
    assert(!changed.is_set(changed.count)); // out of range reads false, not UB

    // A no-op update writes a mask with nothing in it.
    entities::Player_Entity  unchanged_client = baseline;
    network::changed_fields_t nothing_changed;
    transmit(baseline, &baseline, unchanged_client, &nothing_changed);
    assert(!nothing_changed.any());

    std::cout << "    -> Success!" << std::endl;
  }

  std::cout << "[TEST] All Tests Passed." << std::endl;
  return 0;
}
