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
#include "../shared/network/entity_snapshot.hpp"
#include "../shared/network/snapshot_history.hpp"
#include "../shared/weapons.hpp"

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

// Round-trips a whole frame and reports what the encoder actually put on the
// wire. `out_record_count` is the var_uint at the head of the stream, which is
// the thing the "unchanged entities cost nothing" claim is really about.
//
// `decode_baseline` is what the RECEIVER reconstructs against, and it defaults
// to the sender's. Passing a different one models the two ends disagreeing --
// which is the bug the acked rule prevents, so a subtest reproducing that bug
// has to be able to express it.
size_t transmit_snapshot(const network::snapshot_frame_t& current,
                         const network::snapshot_frame_t* baseline,
                         network::snapshot_frame_t&       destination,
                         uint32_t*                        out_record_count = nullptr,
                         const network::snapshot_frame_t* decode_baseline  = nullptr)
{
  if (decode_baseline == nullptr)
    decode_baseline = baseline;

  network::Bit_Writer writer;
  network::serialize_snapshot(writer, current, baseline);

  if (out_record_count != nullptr)
  {
    network::Bit_Reader counter(writer.buffer.data(), writer.buffer.size());
    *out_record_count = network::read_var_uint(counter);
  }

  // A trailing payload, because the real packet has one: the cosmetic effect
  // batch rides in the same bitstream directly after the entity records. That
  // only works if the reader stops on exactly the bit the writer stopped on,
  // so assert the position AND read the tail back.
  constexpr uint32_t trailing_sentinel = 0xABCD;
  const int          writer_end_bit    = writer.bit_index;
  network::write_var_uint(writer, trailing_sentinel);

  network::Bit_Reader reader(writer.buffer.data(), writer.buffer.size());
  const bool decoded = network::deserialize_snapshot(reader, decode_baseline, destination);
  assert(decoded && "a snapshot this test wrote must decode");
  (void)decoded;

  assert(reader.bit_index == writer_end_bit &&
         "reader and writer disagree on where the snapshot ends -- anything "
         "appended after it decodes as garbage");
  assert(network::read_var_uint(reader) == trailing_sentinel);

  return (size_t)((writer_end_bit + 7) / 8);
}

entities::Rocket_Entity make_rocket(shared::entity_uid_t uid, float x)
{
  entities::Rocket_Entity rocket;
  rocket.entity_id = uid;
  rocket.position  = {x, 0.f, 0.f};
  return rocket;
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
    server_state.last_fire_tick = 30;
    server_state.position = {10.0f, 20.0f, 30.0f};

    entities::Player_Entity client_state;
    const size_t full_size = transmit(server_state, nullptr, client_state);
    assert(client_state.health == 100);
    assert(client_state.last_fire_tick == 30);

    // One field moves. The client holds the exact baseline, so the delta only
    // has to carry that field.
    entities::Player_Entity acked_baseline = client_state;
    server_state.health                    = 75;

    const size_t delta_size = transmit(server_state, &acked_baseline, client_state);
    assert(client_state.health == 75);
    assert(client_state.last_fire_tick == 30);           // untouched, carried by the baseline
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

  {
    std::cout << "  [Subtest] Absence means UNCHANGED, not gone..." << std::endl;

    // The property the old format could not express. Three rockets exist; one
    // moves. The wire carries ONE record, and the receiver still ends up with
    // three -- the other two ride across from the baseline it already holds.
    network::snapshot_frame_t server_frame;
    server_frame.tick             = 1;
    server_frame.rockets[10]      = make_rocket(10, 0.f);
    server_frame.rockets[11]      = make_rocket(11, 100.f);
    server_frame.rockets[12]      = make_rocket(12, 200.f);

    network::snapshot_frame_t client_frame;
    uint32_t                  record_count = 0;
    const size_t full_size = transmit_snapshot(server_frame, nullptr, client_frame,
                                               &record_count);
    assert(record_count == 3); // no baseline: every entity is a full record
    assert(client_frame.rockets.size() == 3);

    network::snapshot_frame_t acked = client_frame;

    server_frame.tick               = 2;
    server_frame.rockets[11].position.x = 140.f;

    network::snapshot_frame_t next_client_frame;
    const size_t delta_size = transmit_snapshot(server_frame, &acked, next_client_frame,
                                                &record_count);

    assert(record_count == 1); // only the rocket that moved
    assert(next_client_frame.rockets.size() == 3);
    assert(next_client_frame.rockets.at(11).position.x == 140.f);
    assert(next_client_frame.rockets.at(10).position.x == 0.f);   // carried over
    assert(next_client_frame.rockets.at(12).position.x == 200.f); // carried over

    std::cout << "    Full update: " << full_size << " bytes / 3 records, delta: "
              << delta_size << " bytes / 1 record" << std::endl;
    assert(delta_size < full_size);

    // A tick where nothing at all moved writes only the record count.
    network::snapshot_frame_t idle_client_frame;
    network::snapshot_frame_t idle_baseline = next_client_frame;
    transmit_snapshot(server_frame, &idle_baseline, idle_client_frame, &record_count);
    assert(record_count == 0);
    assert(idle_client_frame.rockets.size() == 3);

    std::cout << "    -> Success!" << std::endl;
  }

  {
    std::cout << "  [Subtest] A player's weapons replicate, one entity each..." << std::endl;

    // Ammo lives on the Weapon_Entity, so the weapons have to arrive for the
    // client to have a magazine at all. They spawn in the SAME tick as their
    // owner, which is what makes a frame self-consistent -- a receiver that
    // decodes the player decodes the weapons its inventory names.
    entities::Player_Entity shooter;
    shooter.entity_id                    = 1;
    shooter.client_slot_index            = 0;
    shooter.inventory.active_slot = entities::Inventory_Slot::Primary;

    // One uid per WEAPON, placed in the slot its definition names -- the same
    // route grant_default_inventory takes, so adding a weapon to the .def
    // extends this rather than leaving a slot holding uid 0.
    for (uint32_t index = 0; index < enum_traits<entities::Weapon>::count; ++index)
      shooter.inventory.weapons[shared::get_weapon_definition((entities::Weapon)index).slot] =
          20 + index;

    network::snapshot_frame_t server_frame;
    server_frame.tick       = 1;
    server_frame.players[1] = shooter;
    // Walked by WEAPON, placed by SLOT: each definition names where it is held,
    // which is the same route grant_default_inventory takes.
    for (uint32_t index = 0; index < enum_traits<entities::Weapon>::count; ++index)
    {
      const entities::Weapon             weapon     = (entities::Weapon)index;
      const shared::weapon_definition_t& definition = shared::get_weapon_definition(weapon);

      entities::Weapon_Entity carried;
      carried.entity_id = shooter.inventory.weapons[definition.slot];
      carried.weapon_id = weapon;
      carried.ammo      = definition.magazine_size;
      server_frame.weapons[carried.entity_id] = carried;
    }

    network::snapshot_frame_t client_frame;
    uint32_t                  record_count = 0;
    transmit_snapshot(server_frame, nullptr, client_frame, &record_count);

    // The player plus one entity per carried weapon.
    assert(record_count == 1 + enum_traits<entities::Weapon>::count);
    assert(client_frame.weapons.size() == enum_traits<entities::Weapon>::count);

    // The client resolves the same way the server does: one index into the
    // replicated forward list, never a scan for a weapon claiming this owner.
    const entities::Player_Entity& received = client_frame.players.at(1);
    const shared::entity_uid_t     held_uid =
        received.inventory.weapons[received.inventory.active_slot];
    assert(held_uid == 21);
    assert(client_frame.weapons.at(held_uid).weapon_id == entities::Weapon::Scout);
    assert(client_frame.weapons.at(held_uid).ammo ==
           shared::get_weapon_definition(entities::Weapon::Scout).magazine_size);

    // One shot costs ONE record. A holstered weapon's fields do not change, so
    // it costs nothing at all after the spawn -- which is the whole answer to
    // "three entities per player is expensive".
    network::snapshot_frame_t acked = client_frame;
    server_frame.tick               = 2;
    server_frame.weapons[21].ammo -= 1;

    network::snapshot_frame_t next_client_frame;
    transmit_snapshot(server_frame, &acked, next_client_frame, &record_count);

    assert(record_count == 1);
    assert(next_client_frame.weapons.at(21).ammo ==
           shared::get_weapon_definition(entities::Weapon::Scout).magazine_size - 1);
    assert(next_client_frame.weapons.at(20).ammo ==
           shared::get_weapon_definition(entities::Weapon::Knife).magazine_size);
    assert(next_client_frame.players.at(1)
               .inventory.weapons[entities::Inventory_Slot::Primary] == 21);

    // And a SWITCH costs one record on the player and none on either weapon:
    // the magazine stays where the last shot left it, which is the free instant
    // reload that used to hide in the switch handler.
    network::snapshot_frame_t acked_after_shot = next_client_frame;
    server_frame.tick                          = 3;
    server_frame.players[1].inventory.active_slot = entities::Inventory_Slot::Melee;

    network::snapshot_frame_t after_switch;
    transmit_snapshot(server_frame, &acked_after_shot, after_switch, &record_count);

    assert(record_count == 1);
    assert(after_switch.weapons.at(21).ammo ==
           shared::get_weapon_definition(entities::Weapon::Scout).magazine_size - 1);

    std::cout << "    -> Success!" << std::endl;
  }

  {
    std::cout << "  [Subtest] Removal is explicit, and spawn needs no opcode..." << std::endl;

    network::snapshot_frame_t server_frame;
    server_frame.tick        = 1;
    server_frame.rockets[10] = make_rocket(10, 0.f);
    server_frame.rockets[11] = make_rocket(11, 100.f);

    network::snapshot_frame_t client_frame;
    transmit_snapshot(server_frame, nullptr, client_frame);
    assert(client_frame.rockets.size() == 2);

    network::snapshot_frame_t acked = client_frame;

    // Rocket 10 explodes, rocket 12 is fired. One removal record, one full
    // record -- the spawn needs no opcode of its own, because "no baseline
    // entry" already means every mask bit is set.
    server_frame.tick = 2;
    server_frame.rockets.erase(10);
    server_frame.rockets[12] = make_rocket(12, 300.f);

    network::snapshot_frame_t next_client_frame;
    uint32_t                  record_count = 0;
    transmit_snapshot(server_frame, &acked, next_client_frame, &record_count);

    assert(record_count == 2);
    assert(next_client_frame.rockets.count(10) == 0); // gone, and said so
    assert(next_client_frame.rockets.count(11) == 1); // unchanged, carried over
    assert(next_client_frame.rockets.at(12).position.x == 300.f);

    std::cout << "    -> Success!" << std::endl;
  }

  {
    std::cout << "  [Subtest] A dropped removal re-rides on the next snapshot..."
              << std::endl;

    // The reason removal belongs IN the delta rather than on its own despawn
    // channel: it inherits the acked-baseline rule's reliability. Lose the
    // packet that says "gone" and the client's ack does not advance, so the
    // next snapshot is computed against a baseline that STILL HAS the entity
    // and says it again. No retransmit layer involved.
    network::snapshot_frame_t server_frame;
    server_frame.tick        = 1;
    server_frame.rockets[10] = make_rocket(10, 0.f);

    network::snapshot_frame_t client_frame;
    transmit_snapshot(server_frame, nullptr, client_frame);
    assert(client_frame.rockets.count(10) == 1);

    // Tick 1 is the newest thing the client reconstructed, so it is what it
    // acks -- and it keeps acking it, because tick 2 never arrives.
    const network::snapshot_frame_t acked_tick_1 = client_frame;

    server_frame.tick = 2;
    server_frame.rockets.erase(10);
    {
      network::snapshot_frame_t discarded_by_packet_loss;
      transmit_snapshot(server_frame, &acked_tick_1, discarded_by_packet_loss);
      assert(discarded_by_packet_loss.rockets.count(10) == 0); // it was in there
    }

    // Tick 3: nothing about rocket 10 changed since tick 2 -- it is still
    // absent. Against the acked tick 1 it is still a removal.
    server_frame.tick = 3;
    network::snapshot_frame_t recovered;
    uint32_t                  record_count = 0;
    transmit_snapshot(server_frame, &acked_tick_1, recovered, &record_count);

    assert(record_count == 1);
    assert(recovered.rockets.count(10) == 0);

    std::cout << "    -> Success (removal survived the loss)!" << std::endl;
  }

  {
    std::cout << "  [Subtest] A dropped fire stamp re-rides on the next snapshot..."
              << std::endl;

    // The claim the gunshot audio rests on (client/weapon_fire_audio.cpp):
    // firing is replicated STATE, not a cosmetic effect, so losing the packet
    // that carries a shot costs a tick of latency instead of the sound. A
    // cosmetic effect rides its packet once with no resend; last_fire_tick is
    // a field like any other, so it inherits the acked-baseline rule.
    //
    // What makes it worth its own subtest rather than being covered by the
    // health one above: a fire stamp is written ONCE and then sits still. That
    // is precisely the shape that a last-sent baseline loses forever, and the
    // second half here shows it doing so.
    network::snapshot_frame_t server_frame;
    server_frame.tick = 1;

    entities::Player_Entity shooter;
    shooter.entity_id         = 1;
    shooter.client_slot_index = 0;
    shooter.last_fire_tick    = 0; // never fired
    server_frame.players[1]   = shooter;

    network::snapshot_frame_t client_frame;
    transmit_snapshot(server_frame, nullptr, client_frame);
    assert(client_frame.players.at(1).last_fire_tick == 0);

    // Tick 1 is the newest frame the client reconstructed, so it is what it
    // acks -- and it goes on acking it, because tick 2 is lost.
    const network::snapshot_frame_t acked_tick_1 = client_frame;

    server_frame.tick                        = 2;
    server_frame.players[1].last_fire_tick   = 2;
    server_frame.players[1].last_fire_weapon = entities::Weapon::Scout;
    {
      network::snapshot_frame_t discarded_by_packet_loss;
      transmit_snapshot(server_frame, &acked_tick_1, discarded_by_packet_loss);
      assert(discarded_by_packet_loss.players.at(1).last_fire_tick == 2);
    }

    // Tick 3: the shot is over and nothing about the shooter has moved since,
    // so the stamp is a field that has stopped changing. Against the acked
    // tick 1 it still differs, so it rides again and the gunshot plays one
    // tick late.
    server_frame.tick = 3;
    network::snapshot_frame_t recovered;
    uint32_t                  record_count = 0;
    transmit_snapshot(server_frame, &acked_tick_1, recovered, &record_count);

    assert(record_count == 1);
    assert(recovered.players.at(1).last_fire_tick == 2);
    assert(recovered.players.at(1).last_fire_weapon == entities::Weapon::Scout);

    // The same loss deltaed against what was last SENT. The server believes
    // the client saw the stamp, so tick 3 says nothing about it -- while the
    // client is still reconstructing against tick 1, where it is 0. The
    // watcher never sees it advance and that shot is silent forever.
    network::snapshot_frame_t last_sent = server_frame; // tick 2, assumed landed
    last_sent.tick                      = 2;

    network::snapshot_frame_t deaf_client;
    transmit_snapshot(server_frame, &last_sent, deaf_client, &record_count,
                      &acked_tick_1);
    assert(record_count == 0);
    assert(deaf_client.players.at(1).last_fire_tick == 0); // shot never heard

    std::cout << "    -> Success (fire stamp survived the loss)!" << std::endl;
  }

  {
    std::cout << "  [Subtest] Mixed entity types share one record stream..." << std::endl;

    network::snapshot_frame_t server_frame;
    server_frame.tick = 1;

    entities::Player_Entity player;
    player.entity_id         = 1;
    player.client_slot_index = 0;
    player.health            = 100;
    server_frame.players[1]  = player;

    entities::Physics_Body_Entity body;
    body.entity_id                 = 20;
    body.position                  = {5.f, 6.f, 7.f};
    server_frame.physics_bodies[20] = body;

    server_frame.rockets[30] = make_rocket(30, 42.f);

    network::snapshot_frame_t client_frame;
    transmit_snapshot(server_frame, nullptr, client_frame);

    assert(client_frame.players.at(1).health == 100);
    assert(client_frame.players.at(1).client_slot_index == 0);
    assert(client_frame.physics_bodies.at(20).position.y == 6.f);
    assert(client_frame.rockets.at(30).position.x == 42.f);

    // The player leaves; the other two types must be untouched by that.
    network::snapshot_frame_t acked = client_frame;
    server_frame.players.clear();

    network::snapshot_frame_t next_client_frame;
    uint32_t                  record_count = 0;
    transmit_snapshot(server_frame, &acked, next_client_frame, &record_count);

    assert(record_count == 1);
    assert(next_client_frame.players.empty());
    assert(next_client_frame.physics_bodies.size() == 1);
    assert(next_client_frame.rockets.size() == 1);

    std::cout << "    -> Success!" << std::endl;
  }

  {
    std::cout << "  [Subtest] A damageable replicates only what the map cannot say..."
              << std::endl;

    // Damageable_Entity is the one MAP-PLACED type that is replicated
    // (generalization_def.md §3). The client already has its position and its
    // mesh from its own map load, so what has to survive the wire is the two
    // things that change at runtime: health, and whether it is still drawn.
    network::snapshot_frame_t server_frame;
    server_frame.tick = 1;

    entities::Damageable_Entity crate;
    crate.entity_id           = 70;
    crate.position            = {100.f, 0.f, 200.f};
    crate.health              = 100;
    crate.hitbox_half_extents = {16.f, 32.f, 16.f};
    crate.render.visible      = true;
    server_frame.damageables[70] = crate;

    network::snapshot_frame_t client_frame;
    transmit_snapshot(server_frame, nullptr, client_frame);

    assert(client_frame.damageables.size() == 1);
    assert(client_frame.damageables.at(70).health == 100);
    assert(client_frame.damageables.at(70).render.visible);

    // hitbox_half_extents is deliberately NOT @Networked, so it arrives as the
    // struct default rather than as what the server holds -- the client reads
    // the real one out of the map. Asserted so that flagging it later is a
    // decision somebody makes on purpose rather than a silent bandwidth
    // increase.
    const entities::Damageable_Entity fresh{};
    assert(client_frame.damageables.at(70).hitbox_half_extents.y ==
           fresh.hitbox_half_extents.y);

    // Destroyed: health crosses zero and the server hides it. That is TWO
    // changed leaves on one entity and must cost exactly one record.
    network::snapshot_frame_t acked = client_frame;
    server_frame.tick                        = 2;
    server_frame.damageables[70].health       = 0;
    server_frame.damageables[70].render.visible = false;

    network::snapshot_frame_t after_death;
    uint32_t                  record_count = 0;
    transmit_snapshot(server_frame, &acked, after_death, &record_count);

    assert(record_count == 1);
    assert(after_death.damageables.at(70).health == 0);
    assert(!after_death.damageables.at(70).render.visible);

    // And an untouched one costs nothing at all, which is what makes a level
    // full of crates free after the first full update.
    network::snapshot_frame_t acked_after_death = after_death;
    server_frame.tick                           = 3;

    network::snapshot_frame_t idle;
    transmit_snapshot(server_frame, &acked_after_death, idle, &record_count);

    assert(record_count == 0);
    assert(idle.damageables.at(70).health == 0);

    std::cout << "    -> Success!" << std::endl;
  }

  std::cout << "[TEST] All Tests Passed." << std::endl;
  return 0;
}
