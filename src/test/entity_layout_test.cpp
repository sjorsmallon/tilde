// Verifies the properties the inheritance layout was chosen for.
#include "entities/generated/entities_generated.hpp"
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace entities;

// Trivial copyability is what blesses memcpy snapshots and memcmp diffing.
// This is the property inheritance was NOT supposed to cost us.
static_assert(std::is_trivially_copyable_v<Light_Entity>);
static_assert(std::is_trivially_copyable_v<Trigger_Volume_Entity>);
static_assert(std::is_trivially_copyable_v<Player_Entity>);
static_assert(std::is_trivially_copyable_v<Entity>);

// Derived-to-base conversion, the thing we traded standard-layout to get.
static_assert(std::is_base_of_v<Entity, Light_Entity>);
static_assert(std::is_convertible_v<Light_Entity*, Entity*>);

int main()
{
  Light_Entity light;
  light.position = {1.0f, 2.0f, 3.0f};

  // No cast, no aliasing violation: a real derived-to-base conversion.
  Entity* base = &light;
  printf("tag via base pointer: %u (expected %u)\n", (unsigned)base->type,
         (unsigned)entity_type::Light_Entity);
  printf("position via base pointer: %g %g %g\n", base->position.x, base->position.y,
         base->position.z);

  // The tag survives through the base pointer, so a generic caller can recover
  // the concrete type without being told what it is.
  const entity_type_info_t& info = entity_info(base->type);
  printf("classname=%s display=\"%s\" fields=%u size=%u runtime_only=%s\n", info.classname,
         info.display_name, info.field_count, info.size_in_bytes,
         info.runtime_only ? "true" : "false");

  // memcmp diffing against a baseline.
  Light_Entity baseline;
  printf("memcmp vs default: %s\n",
         memcmp(&light, &baseline, sizeof(Light_Entity)) != 0 ? "differs" : "identical");

  // Component lookup through the generated tables.
  Trigger_Volume_Entity trigger;
  printf("trigger has Box_Volume: %s at offset %d\n",
         has_component(entity_type::Trigger_Volume_Entity, component_type::Box_Volume) ? "yes"
                                                                                       : "no",
         component_byte_offset(entity_type::Trigger_Volume_Entity, component_type::Box_Volume));

  int32_t offset = component_byte_offset(entity_type::Trigger_Volume_Entity,
                                         component_type::Box_Volume);
  Box_Volume* volume = (Box_Volume*)((char*)&trigger + offset);
  printf("half_extents via table offset: %g %g %g (declared default 1,1,1)\n",
         volume->half_extents.x, volume->half_extents.y, volume->half_extents.z);

  printf("classname lookup round trip: %s\n",
         entity_type_from_classname("light_entity") == entity_type::Light_Entity ? "ok" : "FAILED");
  printf("enum to_string: %s / from_string round trip: ", to_string(Fire_Mode::Every_Tick));
  Fire_Mode parsed = Fire_Mode::On_Enter;
  printf("%s\n", from_string("Every_Tick", &parsed) && parsed == Fire_Mode::Every_Tick
                     ? "ok"
                     : "FAILED");
  printf("schema hash: 0x%08x\n", SCHEMA_HASH);
  return 0;
}
