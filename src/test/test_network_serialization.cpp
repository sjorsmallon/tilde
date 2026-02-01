#include "../shared/network/entity.hpp"
#include <cassert>
#include <iostream>

using namespace network;

// --- Define an Entity ---

class TestPlayer : public Entity
{
public:
  Network_Var<int32> health;
  Network_Var<float32> x;
  Network_Var<float32> y;
  Network_Var<int32> ammo;

  DECLARE_SCHEMA(TestPlayer)
};

// --- Define Schema ---
BEGIN_SCHEMA(TestPlayer)
DEFINE_FIELD(health, Field_Type::Int32)
DEFINE_FIELD(x, Field_Type::Float32)
DEFINE_FIELD(y, Field_Type::Float32)
DEFINE_FIELD(ammo, Field_Type::Int32)
END_SCHEMA(TestPlayer)

int main()
{
  std::cout << "[TEST] Starting Network Serialization Test..." << std::endl;

  // 1. Register Schema (must be done once)
  TestPlayer::register_schema();

  // 2. Create Server Entities
  TestPlayer server_p1;
  server_p1.health = 100;
  server_p1.x = 10.0f;
  server_p1.y = 20.0f;
  server_p1.ammo = 30;

  TestPlayer server_p2;  // Next tick
  server_p2.health = 90; // Took damage logic
  server_p2.x = 12.0f;   // Moved
  server_p2.y = 20.0f;   // Unchanged
  server_p2.ammo = 30;   // Unchanged

  TestPlayer client_p; // Client representation

  // 3. Test Full Update (Baseline = nullptr)
  {
    std::cout << "[TEST] Testing Full Update..." << std::endl;
    Bit_Writer writer;
    server_p1.serialize(writer, nullptr);

    Bit_Reader reader(writer.buffer.data(), writer.buffer.size());
    client_p.deserialize(reader);

    assert(client_p.health == 100);
    assert(client_p.x == 10.0f);
    assert(client_p.y == 20.0f);
    assert(client_p.ammo == 30);
    std::cout << "  -> Success!" << std::endl;
  }

  // 4. Test Delta Update (Baseline = server_p1)
  {
    std::cout << "[TEST] Testing Delta Update..." << std::endl;
    Bit_Writer writer;
    server_p2.serialize(writer, &server_p1);

    // Verification of Delta Size
    // Fields: Health(4), X(4), Y(4), Ammo(4)
    // Changed: Health, X. Unchanged: Y, Ammo.
    // Bits: 1 (Health Changed) + 1 (X Changed) + 1 (Y Changed) + 1 (Ammo
    // Changed) = 4 bits Data: 4 bytes (Health) + 4 bytes (X) = 8 bytes. Total
    // should be roughly 8 bytes + 4 bits.
    std::cout << "  -> Delta Buffer Size: " << writer.buffer.size() << " bytes"
              << std::endl;

    Bit_Reader reader(writer.buffer.data(), writer.buffer.size());

    // In a real system, we'd apply this to our existing client_p
    // But Deserialize currently blindly reads.
    // We need to ensure we are DESERIALIZING into the object that was used as
    // baseline logic OR we are just updating the fields that are in the stream.
    // Our Deserialize implementation reads flags. If flag is 0, it does
    // NOTHING. So client_p will retain old values for unchanged fields.
    client_p.deserialize(reader);

    assert(client_p.health == 90); // Updated
    assert(client_p.x == 12.0f);   // Updated
    assert(client_p.y == 20.0f);   // Retained 20.0f
    assert(client_p.ammo == 30);   // Retained 30
    std::cout << "  -> Success!" << std::endl;
  }

  std::cout << "[TEST] All Tests Passed." << std::endl;
  return 0;
}
