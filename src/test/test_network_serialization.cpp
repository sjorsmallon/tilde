#include "../shared/entity.hpp"
#include <cassert>
#include <iostream>

using namespace network;

// --- Define an Entity ---

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
    std::cout << "  [Subtest] Full update..." << std::endl;
    auto updates = diff(nullptr, &server_p1, server_p1.get_schema());

    // Full update should produce all fields
    assert(updates.size() > 0);

    // Apply to client
    apply_diff(&client_p, updates, client_p.get_schema());

    assert(client_p.health == 100);
    assert(client_p.x == 10.0f);
    assert(client_p.y == 20.0f);
    assert(client_p.ammo == 30);

    std::cout << "    PASSED!" << std::endl;
  }

  // 4. Test Delta Update
  {
    std::cout << "  [Subtest] Delta update..." << std::endl;

    // Before: client_p matches server_p1
    // Now server changed to server_p2

    // Diff from old state to new
    auto updates = diff(&server_p1, &server_p2, server_p2.get_schema());

    // Only changed fields should appear
    // health changed (100->90), x changed (10->12), y unchanged, ammo unchanged
    assert(updates.size() == 2); // health and x

    // Apply to client
    apply_diff(&client_p, updates, client_p.get_schema());

    assert(client_p.health == 90);
    assert(client_p.x == 12.0f);
    assert(client_p.y == 20.0f); // unchanged
    assert(client_p.ammo == 30); // unchanged

    std::cout << "    PASSED!" << std::endl;
  }

  std::cout << "[TEST] All Network Serialization Tests Passed!" << std::endl;
  return 0;
}
