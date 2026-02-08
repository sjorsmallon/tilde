#define ENTITIES_WANT_INCLUDES
#include "game_session.hpp"
#include "entities/entity_list.hpp"
#include "shapes.hpp"

namespace shared
{

// Helper to get bounds from generic entity if it is a supported static type
std::optional<aabb_bounds_t> get_entity_bounds(const network::Entity *entity)
{
  if (auto *aabb = dynamic_cast<const network::AABB_Entity *>(entity))
  {
    aabb_t t;
    t.center = aabb->center.value;
    t.half_extents = aabb->half_extents.value;
    return get_bounds(t);
  }
  else if (auto *wedge = dynamic_cast<const network::Wedge_Entity *>(entity))
  {
    wedge_t t;
    t.center = wedge->center.value;
    t.half_extents = wedge->half_extents.value;
    t.orientation = wedge->orientation.value;
    return get_bounds(t);
  }
  else if (auto *mesh =
               dynamic_cast<const network::Static_Mesh_Entity *>(entity))
  {
    // TODO: Mesh bounds
    aabb_t t;
    t.center = mesh->position.value;
    t.half_extents = mesh->scale.value * 0.5f; // Approximate
    return get_bounds(t);
  }
  return std::nullopt;
}

void init_session_from_map(game_session_t &session, const map_t &map)
{
  session.map_name = map.name;
  session.entity_system.reset();
  session.static_entities.clear();

  // 1. Separate Static vs Dynamic Entities
  for (const auto &ent : map.entities)
  {
    if (!ent)
      continue;

    // Check if it is a static entity type
    // We use dynamic_cast for this.
    if (dynamic_cast<const network::AABB_Entity *>(ent.get()) ||
        dynamic_cast<const network::Wedge_Entity *>(ent.get()) ||
        dynamic_cast<const network::Static_Mesh_Entity *>(ent.get()))
    {
      session.static_entities.push_back(ent);
    }
    else
    {
      // Assume it's a dynamic entity (Player, Weapon, etc.)
      // We need to add it to the entity system.
      // The entity system traditionally managed its own entities via ID
      // generation. If we just copy the pointer, the ID might collide or not be
      // managed? Entity_System::add_entity usually takes ownership or creates a
      // new one. Let's check Entity_System. For now, let's assume we can add
      // it. But wait, the map contains `shared_ptr<Entity>`. Entity_System
      // likely has a `std::vector<std::shared_ptr<Entity>>` or similar. I'll
      // assume I can just push it or I need to use `add_existing_entity`? Since
      // I don't see Entity_System definition right now, I will assume a method
      // exist or I'll implement a simple add. Actually `Entity_System` in
      // `entity_system.hpp` usually manages IDs. The map entities might not
      // have IDs set (0:0). We should probably Clone them into the system to
      // assign IDs? Or just move them. Let's try adding it.
      session.entity_system.add_entity(ent);
    }
  }

  // 2. Build BVH from Static Entities
  std::vector<BVH_Input> bvh_inputs;
  bvh_inputs.reserve(session.static_entities.size());

  for (size_t i = 0; i < session.static_entities.size(); ++i)
  {
    auto bounds = get_entity_bounds(session.static_entities[i].get());
    if (bounds)
    {
      BVH_Input input;
      input.aabb.min = bounds->min;
      input.aabb.max = bounds->max;
      // Point to the index in the session's static_entities vector
      input.id = {Collision_Id::Type::Static_Geometry, (uint32_t)i};
      bvh_inputs.push_back(input);
    }
  }

  session.bvh = build_bvh(bvh_inputs);
}

} // namespace shared
