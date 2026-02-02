#include "shared/old_ideas/ecs.hpp"
#include <cassert>
#include <iostream>

struct Position
{
  float x, y;
};

struct ComplexComp
{
  int a;
  float b;
  bool c;
  ComplexComp(int _a, float _b, bool _c) : a(_a), b(_b), c(_c) {}
};

int main()
{
  ecs::Registry registry;
  auto entity = registry.create_entity();

  // Test existing add_component
  registry.add_component(entity, Position{10.0f, 20.0f});
  auto &pos = registry.get_component<Position>(entity);
  assert(pos.x == 10.0f && pos.y == 20.0f);
  std::cout << "Existing add_component passed." << std::endl;

  auto entity2 = registry.create_entity();
  // Test new emplace add_component
  ComplexComp &comp =
      registry.add_component<ComplexComp>(entity2, 1, 2.5f, true);
  assert(comp.a == 1);
  assert(comp.b == 2.5f);
  assert(comp.c == true);

  auto &got_comp = registry.get_component<ComplexComp>(entity2);
  assert(got_comp.a == 1);
  assert(&comp == &got_comp); // Check reference validity (until resize)

  std::cout << "Emplace add_component passed." << std::endl;

  // Test 0-arg add_component (user request)
  auto entity3 = registry.create_entity();
  Position &p = registry.add_component<Position>(entity3);
  p.x = 100.0f;
  p.y = 200.0f;
  assert(registry.get_component<Position>(entity3).x == 100.0f);
  std::cout << "0-arg add_component passed." << std::endl;

  return 0;
}
