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

#include "../shared/assets/asset_state.hpp"
#include "../shared/entities/entity_reflection.hpp"
#include "../shared/network/entity_serialization.hpp"
#include "../shared/network/field_codec.hpp"
#include "../shared/pascal_array.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities/shot_entity_generated.hpp"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <string>

namespace
{

// A `T<N>` field as the generator rows it: one row over the whole Pascal_Array,
// the element's type, the list column saying where the elements lie.
struct list_holder_t
{
  uint32_t                      before = 0;
  Pascal_Array<linalg::vec3f, 8> points;
  uint32_t                      after = 0;
};

constexpr field_info_t LIST_ROW = {
    .name            = "points",
    .type            = FIELD_TYPE_V3,
    .offset          = (uint32_t)offsetof(list_holder_t, points),
    .size_in_bytes   = (uint32_t)sizeof(list_holder_t::points),
    .flags           = 0u,
    .component_id    = NOT_A_COMPONENT,
    .string_capacity = NOT_A_STRING,
    .asset_class_id  = NOT_AN_ASSET_CLASS,
    .enum_info       = NOT_AN_ENUM,
    .list            = {.bound                 = 8u,
                        .element_size_in_bytes = (uint32_t)sizeof(decltype(list_holder_t::points)::element_t),
                        .elements_offset       = (uint32_t)offsetof(decltype(list_holder_t::points), elements)}};

void test_list_field_round_trip()
{
  std::cout << "  [Subtest] A counted list rides as count then elements..." << std::endl;

  list_holder_t sent;
  assert(sent.points.try_push_back({1.0f, 2.0f, 3.0f}));
  assert(sent.points.try_push_back({4.5f, -6.0f, 7.0f}));
  assert(sent.points.try_push_back({0.0f, 0.0f, -8.0f}));

  network::Bit_Writer writer;
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&sent), LIST_ROW, LIST_ROW.offset,
                       nullptr);

  list_holder_t received;
  received.points.elements.data[5] = {99.0f, 99.0f, 99.0f};
  network::Bit_Reader reader(writer.buffer.data(), writer.buffer.size());
  assert(network::read_field(reader, reinterpret_cast<uint8_t*>(&received), LIST_ROW, LIST_ROW.offset));

  assert(received.points.count == 3);
  assert((received.points[0] == linalg::vec3f{1.0f, 2.0f, 3.0f}));
  assert((received.points[1] == linalg::vec3f{4.5f, -6.0f, 7.0f}));
  assert((received.points[2] == linalg::vec3f{0.0f, 0.0f, -8.0f}));
  assert((received.points.elements.data[5] == linalg::vec3f{99.0f, 99.0f, 99.0f}));
  assert(received.before == 0 && received.after == 0);

  std::string text;
  assert(field_to_text(&received.points, LIST_ROW, text));
  assert(text == "[1 2 3, 4.5 -6 7, 0 0 -8]");

  std::cout << "    PASSED!" << std::endl;
}

// The list's own delta: against a baseline only the elements that differ ride,
// plus the tail the baseline did not hold, and the reader rebuilds the value
// over its copy of the baseline.
void test_list_field_delta()
{
  std::cout << "  [Subtest] A list against a baseline sends the changed and the new elements..."
            << std::endl;

  list_holder_t baseline;
  assert(baseline.points.try_push_back({1.0f, 2.0f, 3.0f}));
  assert(baseline.points.try_push_back({4.0f, 5.0f, 6.0f}));
  assert(baseline.points.try_push_back({7.0f, 8.0f, 9.0f}));

  list_holder_t current = baseline;
  current.points[1]     = {-4.0f, 5.0f, 6.0f};
  assert(current.points.try_push_back({10.0f, 11.0f, 12.0f}));
  assert(current.points.try_push_back({13.0f, 14.0f, 15.0f}));

  network::Bit_Writer full;
  network::write_field(full, reinterpret_cast<const uint8_t*>(&current), LIST_ROW, LIST_ROW.offset,
                       nullptr);

  network::Bit_Writer delta;
  network::write_field(delta, reinterpret_cast<const uint8_t*>(&current), LIST_ROW, LIST_ROW.offset,
                       reinterpret_cast<const uint8_t*>(&baseline));

  // Three elements in place of five, for three bits more.
  std::cout << "    full=" << full.bit_index << " bits, delta=" << delta.bit_index << " bits"
            << std::endl;
  assert(delta.bit_index < full.bit_index);

  list_holder_t received = baseline;
  network::Bit_Reader reader(delta.buffer.data(), delta.buffer.size());
  assert(network::read_field(reader, reinterpret_cast<uint8_t*>(&received), LIST_ROW, LIST_ROW.offset));
  assert(received.points.count == 5);
  for (uint32_t index = 0; index < 5; ++index)
    assert((received.points[index] == current.points[index]));

  std::cout << "    PASSED!" << std::endl;
}

// A list that shrank sends its count and one clear bit per kept element; the
// kept elements stay what the baseline had.
void test_list_field_shrinks()
{
  std::cout << "  [Subtest] A shrunk list sends only its count..." << std::endl;

  list_holder_t baseline;
  for (uint32_t index = 0; index < 5; ++index)
    assert(baseline.points.try_push_back({(float)index, 0.0f, 0.0f}));

  list_holder_t current = baseline;
  current.points.count  = 2;

  network::Bit_Writer delta;
  network::write_field(delta, reinterpret_cast<const uint8_t*>(&current), LIST_ROW, LIST_ROW.offset,
                       reinterpret_cast<const uint8_t*>(&baseline));
  assert(delta.bit_index <= 16);

  list_holder_t received = baseline;
  network::Bit_Reader reader(delta.buffer.data(), delta.buffer.size());
  assert(network::read_field(reader, reinterpret_cast<uint8_t*>(&received), LIST_ROW, LIST_ROW.offset));
  assert(received.points.count == 2);
  assert((received.points[0] == linalg::vec3f{0.0f, 0.0f, 0.0f}));
  assert((received.points[1] == linalg::vec3f{1.0f, 0.0f, 0.0f}));

  std::cout << "    PASSED!" << std::endl;
}

void test_list_field_refuses_overflow()
{
  std::cout << "  [Subtest] A count past the bound is refused..." << std::endl;

  network::Bit_Writer writer;
  network::write_var_uint(writer, 9);

  list_holder_t received;
  network::Bit_Reader reader(writer.buffer.data(), writer.buffer.size());
  assert(!network::read_field(reader, reinterpret_cast<uint8_t*>(&received), LIST_ROW, LIST_ROW.offset));
  assert(received.points.count == 0);

  std::cout << "    PASSED!" << std::endl;
}

} // namespace

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  std::cout << "[TEST] Starting Network Serialization Test..." << std::endl;

  test_list_field_round_trip();
  test_list_field_delta();
  test_list_field_shrinks();
  test_list_field_refuses_overflow();

  // Values are chosen to survive write_coord, which keeps 5 fractional bits:
  // integers and multiples of 1/32 round-trip exactly, arbitrary floats do not.
  entities::Player_Entity server_tick_1;
  server_tick_1.health.current_health = 100;
  server_tick_1.position = {10.0f, 20.0f, 0.0f};
  server_tick_1.last_fire_tick = 30;

  entities::Player_Entity server_tick_2 = server_tick_1;
  server_tick_2.health.current_health = 90; // took damage
  server_tick_2.position.x = 12.0f; // moved
  // position.y, position.z and last_fire_tick deliberately unchanged.

  entities::Player_Entity client;

  {
    std::cout << "  [Subtest] Full update..." << std::endl;

    network::Bit_Writer writer;
    network::serialize_entity(writer, server_tick_1, nullptr);

    network::Bit_Reader reader(writer.buffer.data(), writer.buffer.size());
    network::deserialize_entity(reader, client);

    assert(client.health.current_health == 100);
    assert(client.position.x == 10.0f);
    assert(client.position.y == 20.0f);
    assert(client.last_fire_tick == 30);

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

    assert(client.health.current_health == 90);
    assert(client.position.x == 12.0f);
    assert(client.position.y == 20.0f); // unchanged, so untouched by the delta
    assert(client.last_fire_tick == 30);          // unchanged

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

    // What "does not arrive" means is UNTOUCHED, i.e. still whatever the .def
    // declares as the default -- not zero. Compared against a pristine entity
    // rather than a literal so that editing the default in entities.def cannot
    // make this test wrong again (it asserted == 0.0f while the .def said
    // = 50.0, so it failed on the default rather than on the wire behaviour).
    const entities::Shot_Entity pristine_rocket;

    entities::Shot_Entity server_rocket;
    server_rocket.lifetime = pristine_rocket.lifetime + 73.0f;
    server_rocket.position      = {1.0f, 2.0f, 3.0f};

    network::Bit_Writer writer;
    network::serialize_entity(writer, server_rocket, nullptr);

    entities::Shot_Entity client_rocket;
    network::Bit_Reader reader(writer.buffer.data(), writer.buffer.size());
    network::deserialize_entity(reader, client_rocket);

    assert(client_rocket.position.x == 1.0f); // @Networked, arrives
    // not @Networked, so the sender's 123 never lands and the receiver keeps
    // its own default. Both halves matter: the first catches the field riding
    // the wire, the second catches deserialize zeroing what it did not write.
    assert(client_rocket.lifetime != server_rocket.lifetime);
    assert(client_rocket.lifetime == pristine_rocket.lifetime);

    std::cout << "    PASSED!" << std::endl;
  }

  std::cout << "[TEST] All Network Serialization Tests Passed!" << std::endl;
  return 0;
}
