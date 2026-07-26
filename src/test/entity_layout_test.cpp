// Verifies the properties the inheritance layout was chosen for, plus the
// factory / placeable-type surface the generator emits on top of it.
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

static int32_t failure_count = 0;

static void check(bool condition, const char* description)
{
  printf("%s %s\n", condition ? "  ok  " : "FAILED", description);
  if (!condition)
    ++failure_count;
}

int main()
{
  Light_Entity light;
  light.position = {1.0f, 2.0f, 3.0f};

  // No cast, no aliasing violation: a real derived-to-base conversion.
  Entity* base = &light;
  check(base->type == entity_type::Light_Entity, "tag survives a derived-to-base conversion");
  check(base->position.x == 1.0f && base->position.y == 2.0f && base->position.z == 3.0f,
        "position readable through the base pointer");

  // The tag survives through the base pointer, so a generic caller can recover
  // the concrete type without being told what it is.
  const entity_type_info_t& info = entity_info(base->type);
  printf("classname=%s display=\"%s\" fields=%u size=%u runtime_only=%s\n", info.classname,
         info.display_name, info.field_count, info.size_in_bytes,
         info.runtime_only ? "true" : "false");
  check(strcmp(info.classname, "light_entity") == 0, "classname derived from the declared name");
  check(strcmp(info.display_name, "Light") == 0, "display name strips the _Entity suffix");

  // memcmp diffing against a baseline.
  Light_Entity baseline;
  check(memcmp(&light, &baseline, sizeof(Light_Entity)) != 0,
        "a changed field is visible to a whole-struct memcmp");

  // Component lookup through the generated tables.
  Trigger_Volume_Entity trigger;
  check(has_component(entity_type::Trigger_Volume_Entity, component_type::Box_Volume),
        "Trigger_Volume declares a Box_Volume component");
  check(!has_component(entity_type::Light_Entity, component_type::Box_Volume),
        "Light declares no Box_Volume component");

  int32_t offset = component_byte_offset(entity_type::Trigger_Volume_Entity,
                                         component_type::Box_Volume);
  Box_Volume* volume = (Box_Volume*)((char*)&trigger + offset);
  check(volume->half_extents.x == 1.0f && volume->half_extents.y == 1.0f &&
            volume->half_extents.z == 1.0f,
        "component reached by table offset carries its declared defaults");

  check(entity_type_from_classname("light_entity") == entity_type::Light_Entity,
        "classname lookup round trip");
  check(entity_type_from_classname("no_such_entity") == entity_type::Invalid,
        "an unknown classname resolves to Invalid rather than a wrong type");

  check(strcmp(to_string(Fire_Mode::Every_Tick), "Every_Tick") == 0, "enum to_string");
  Fire_Mode parsed = Fire_Mode::On_Enter;
  check(from_string("Every_Tick", &parsed) && parsed == Fire_Mode::Every_Tick,
        "enum from_string round trip");
  check(!from_string("Sometimes", &parsed), "enum from_string rejects an unknown name");

  // --- construction ---
  //
  // construct_at is the type-erased hook: the caller owns the memory and only
  // wants the declared defaults written into it. Undo, baselines and (later)
  // pooled storage all reach entities this way.
  {
    alignas(16) unsigned char storage[512];
    const entity_type_info_t& particle_info = entity_info(entity_type::Particle_Emitter_Entity);
    check(particle_info.size_in_bytes <= sizeof(storage) && particle_info.alignment <= 16,
          "the test's stack buffer is big enough for the type being constructed");

    memset(storage, 0xCD, sizeof(storage));
    Entity* constructed = particle_info.construct_at(storage);

    check(constructed == (Entity*)storage, "construct_at builds in place, allocating nothing");
    check(constructed->type == entity_type::Particle_Emitter_Entity,
          "construct_at sets the tag through the generated constructor");

    Particle_Emitter_Entity* emitter = static_cast<Particle_Emitter_Entity*>(constructed);
    check(emitter->emit_rate == 20.0f && emitter->max_particles == 64,
          "construct_at writes the .def defaults over whatever was in the memory");
  }

  // Heap factory over the same switch. The map loader and the editor both want
  // an instance rather than a tag.
  {
    Entity* created = create_entity(entity_type::Rocket_Entity);
    check(created != nullptr && created->type == entity_type::Rocket_Entity,
          "create_entity returns an instance carrying its own tag");
    destroy_entity(created);

    Entity* by_name = entity_from_classname("physics_body_entity");
    check(by_name != nullptr && by_name->type == entity_type::Physics_Body_Entity,
          "entity_from_classname resolves a classname off disk to an instance");
    check(static_cast<Physics_Body_Entity*>(by_name)->shape == Shape_Kind::Box,
          "the instance carries its declared defaults");
    destroy_entity(by_name);

    check(entity_from_classname("no_such_entity") == nullptr,
          "entity_from_classname returns null for an unknown classname");

    destroy_entity(nullptr); // must not crash
  }

  // --- placeable types ---
  //
  // The editor placement menu's source of truth: everything the .def did not
  // mark @runtime_only, contiguous so the menu can index it.
  {
    uint32_t           placeable_count = 0;
    const entity_type* placeable       = placeable_entity_types(&placeable_count);

    check(placeable_count > 0 && placeable_count < ENTITY_TYPE_COUNT,
          "some but not all entity types are placeable");

    bool every_type_is_placeable_and_not_runtime_only = true;
    bool contains_light                               = false;
    for (uint32_t index = 0; index < placeable_count; ++index)
    {
      if (entity_info(placeable[index]).runtime_only)
        every_type_is_placeable_and_not_runtime_only = false;
      if (placeable[index] == entity_type::Light_Entity)
        contains_light = true;
    }

    check(every_type_is_placeable_and_not_runtime_only,
          "no @runtime_only type appears in the placeable list");
    check(contains_light, "a map-placed type (Light) does appear in it");

    printf("placeable types (%u):", placeable_count);
    for (uint32_t index = 0; index < placeable_count; ++index)
      printf(" %s", entity_info(placeable[index]).display_name);
    printf("\n");
  }

  // --- asset manifest ---
  //
  // The manifest models identity. A consumer holding a mesh_asset must have no
  // way to ask where the bytes come from, and no reason to want to: the source
  // column below is read by the asset system's init and by nothing else.
  {
    Render render;
    check(render.mesh == mesh_asset::Missing,
          "an unassigned mesh field reads as Missing, not as whichever asset sorted first");

    Particle_Emitter_Entity emitter;
    check(emitter.sprite == sprite_asset::Smoke, "a declared asset default resolves by name");

    check(strcmp(to_string(mesh_asset::Cube), "Cube") == 0, "asset to_string");
    mesh_asset parsed_mesh = mesh_asset::Missing;
    check(from_string("Unit_Sphere", &parsed_mesh) && parsed_mesh == mesh_asset::Unit_Sphere,
          "asset from_string round trip");
    check(!from_string("No_Such_Mesh", &parsed_mesh),
          "asset from_string rejects an unknown name");

    uint32_t           mesh_count = 0;
    const asset_info_t* meshes    = mesh_asset_manifest(&mesh_count);

    check(mesh_count == mesh_asset_COUNT, "the manifest covers every id in the enum");
    check(meshes[0].source_kind == ASSET_SOURCE_FILE &&
              strcmp(meshes[0].source, "resources/obj/error.obj") == 0,
          "slot 0 resolves to the declared placeholder, so nothing renders as a plausible cube");

    // Both source kinds are present and neither is distinguishable through the
    // id -- only through this table, which is the point.
    bool saw_file       = false;
    bool saw_procedural = false;
    bool every_entry_is_resolvable = true;
    for (uint32_t index = 0; index < mesh_count; ++index)
    {
      if (meshes[index].source_kind == ASSET_SOURCE_FILE)
        saw_file = true;
      if (meshes[index].source_kind == ASSET_SOURCE_PROCEDURAL)
        saw_procedural = true;
      if (meshes[index].source[0] == '\0')
        every_entry_is_resolvable = false;
    }

    check(saw_file && saw_procedural, "one class carries both file-backed and generated meshes");
    check(every_entry_is_resolvable,
          "every mesh id has a source, so init can populate the whole manifest eagerly");

    // The field table records which class a field draws from, so a generic
    // consumer (the editor inspector) can offer the right closed set without
    // being told the class by name.
    const entity_type_info_t& emitter_info = entity_info(entity_type::Particle_Emitter_Entity);
    bool                      sprite_field_names_its_class = false;
    for (uint32_t index = 0; index < emitter_info.field_count; ++index)
    {
      const field_info_t& field = emitter_info.fields[index];
      if (strcmp(field.name, "sprite") == 0)
        sprite_field_names_its_class =
            field.type == FIELD_TYPE_ASSET && field.asset_class_id >= 0;
    }
    check(sprite_field_names_its_class, "an asset field records which asset class it draws from");

    printf("mesh manifest (%u):", mesh_count);
    for (uint32_t index = 0; index < mesh_count; ++index)
      printf(" %s", meshes[index].name);
    printf("\n");
  }

  printf("schema hash: 0x%08x\n", SCHEMA_HASH);
  printf("%s (%d failure%s)\n", failure_count == 0 ? "PASSED" : "FAILED", failure_count,
         failure_count == 1 ? "" : "s");
  return failure_count == 0 ? 0 : 1;
}
