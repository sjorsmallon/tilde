// Verifies the properties the inheritance layout was chosen for, plus the
// factory / placeable-type surface the generator emits on top of it.
#include "entities/generated/entities_generated.hpp"
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace entities;

// Trivial copyability, trivial destructibility and derivation from the base
// used to be asserted here, by hand, for four of the eight types. They are now
// emitted by def_gen for EVERY type, so including the generated header is the
// check -- and the type that gets forgotten is no longer the one a hand-kept
// list forgets. The one property left worth testing here is that a base
// pointer recovered from untyped memory is the same pointer the language
// would produce, which no static_assert can say (see "as_base" below).
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
         info.display_name, info.fields.size(), info.size_in_bytes,
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
  check(volume->half_extents.x == 64.0f && volume->half_extents.y == 64.0f &&
            volume->half_extents.z == 64.0f,
        "component reached by table offset carries its declared defaults");

  // Per-use component defaults (`volume: Box_Volume = { half_extents = ... }`).
  // The half_extents check above is the OVERRIDDEN half; this is the other half
  // and the one that actually needs guarding: a member the literal does not name
  // must keep the component's own default. Emitting the literal as anything but
  // a designated initializer would zero these instead.
  check(volume->position.x == 0.0f && volume->position.y == 0.0f && volume->position.z == 0.0f,
        "a field the use-site literal does not name keeps the component's own default");

  Player_Entity player;
  check(player.render.mesh == assets::mesh_asset::Leet_Full,
        "a use-site component default reaches the entity struct");
  check(player.render.visible && player.render.scale.x == 1.0f && player.render.scale.y == 1.0f &&
            player.render.scale.z == 1.0f,
        "and leaves Render's own defaults alone around it");

  // The scalar that replaced the Hitbox component on the rocket.
  Rocket_Entity rocket;
  check(rocket.collision_radius == 12.0f, "a rocket knows its own sweep radius");
  check(rocket.lifetime == 5.0f, "one lifetime, not one per spawn site");

  check(entity_type_from_classname("light_entity") == entity_type::Light_Entity,
        "classname lookup round trip");
  check(entity_type_from_classname("no_such_entity") == entity_type::Invalid,
        "an unknown classname resolves to Invalid rather than a wrong type");

  check(strcmp(to_string(Fire_Mode::Every_Tick), "Every_Tick") == 0, "enum to_string");
  check(try_from_string<Fire_Mode>("Every_Tick") == Fire_Mode::Every_Tick,
        "enum try_from_string round trip");
  check(!try_from_string<Fire_Mode>("Sometimes"),
        "enum try_from_string rejects an unknown name");

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

  // --- as_base ---
  //
  // construct_at's counterpart for storage that already holds a live entity.
  // Pooled storage addresses its elements as bytes and has to reach Entity*
  // from a std::byte* WITHOUT betting that Entity sits at offset 0 -- an entity
  // and its base both have data members, so they are not pointer-
  // interconvertible and no static_assert can vouch for that layout. The check
  // that means something is therefore a runtime one: the table's answer must be
  // the pointer the language itself produces.
  {
    Rocket_Entity rocket;
    rocket.position = {4.0f, 5.0f, 6.0f};

    const entity_type_info_t& rocket_info = entity_info(entity_type::Rocket_Entity);
    Entity* through_the_table    = rocket_info.as_base(&rocket);
    Entity* through_the_language = &rocket;

    check(through_the_table == through_the_language,
          "as_base agrees with a real derived-to-base conversion");
    check(through_the_table->type == entity_type::Rocket_Entity &&
              through_the_table->position.x == 4.0f,
          "the entity is readable through the base pointer as_base returned");

    // The pool indexes this table by tag and has no way to notice a hole, so a
    // missing hook would be a null call rather than a lookup failure.
    bool every_type_carries_both_hooks = true;
    for (uint32_t which = 1; which < ENTITY_TYPE_COUNT; ++which)
    {
      const entity_type_info_t& type_info = entity_info((entity_type)which);
      if (type_info.construct_at == nullptr || type_info.as_base == nullptr)
        every_type_carries_both_hooks = false;
    }
    check(every_type_carries_both_hooks, "every entity type carries construct_at and as_base");
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
    Span<const entity_type> placeable = placeable_entity_types();

    check(!placeable.empty() && placeable.size() < ENTITY_TYPE_COUNT,
          "some but not all entity types are placeable");

    bool every_type_is_placeable_and_not_runtime_only = true;
    bool contains_light                               = false;
    for (entity_type type : placeable)
    {
      if (entity_info(type).runtime_only)
        every_type_is_placeable_and_not_runtime_only = false;
      if (type == entity_type::Light_Entity)
        contains_light = true;
    }

    check(every_type_is_placeable_and_not_runtime_only,
          "no @runtime_only type appears in the placeable list");
    check(contains_light, "a map-placed type (Light) does appear in it");

    printf("placeable types (%u):", placeable.size());
    for (entity_type type : placeable)
      printf(" %s", entity_info(type).display_name);
    printf("\n");
  }

  // --- asset manifest ---
  //
  // The manifest models identity. A consumer holding a mesh_asset must have no
  // way to ask where the bytes come from, and no reason to want to: the source
  // column below is read by the asset system's init and by nothing else.
  {
    Render render;
    check(render.mesh == assets::mesh_asset::Missing,
          "an unassigned mesh field reads as Missing, not as whichever asset sorted first");

    Particle_Emitter_Entity emitter;
    check(emitter.sprite == assets::sprite_asset::Smoke, "a declared asset default resolves by name");

    check(strcmp(to_string(assets::mesh_asset::Pyramid), "Pyramid") == 0, "asset to_string");
    check(assets::try_from_string<assets::mesh_asset>("Sphere") == assets::mesh_asset::Sphere,
          "asset try_from_string round trip");
    check(!assets::try_from_string<assets::mesh_asset>("No_Such_Mesh"),
          "asset try_from_string rejects an unknown name");

    Span<const assets::asset_info_t> meshes = assets::mesh_asset_manifest();

    check(meshes.size() == assets::mesh_asset_COUNT, "the manifest covers every id in the enum");
    check(meshes[0].source_kind == assets::ASSET_SOURCE_FILE &&
              strcmp(meshes[0].source, "resources/obj/error.obj") == 0,
          "slot 0 resolves to the declared placeholder, so nothing renders as a plausible cube");

    // The placeholder's own file must not ALSO be scanned in under its stem, or
    // one file would hold two ids under two names. That is a collision the
    // generator creates itself, so its duplicate-name check could never catch
    // it -- the scan skips the placeholder path instead.
    uint32_t entries_naming_the_placeholder = 0;
    for (const assets::asset_info_t& mesh : meshes)
      if (strcmp(mesh.source, "resources/obj/error.obj") == 0)
        ++entries_naming_the_placeholder;
    check(entries_naming_the_placeholder == 1,
          "the placeholder file has one id, not one as Missing and another as its own stem");

    // Both source kinds are present and neither is distinguishable through the
    // id -- only through this table, which is the point.
    bool saw_file       = false;
    bool saw_procedural = false;
    bool every_entry_is_resolvable = true;
    for (const assets::asset_info_t& mesh : meshes)
    {
      if (mesh.source_kind == assets::ASSET_SOURCE_FILE)
        saw_file = true;
      if (mesh.source_kind == assets::ASSET_SOURCE_PROCEDURAL)
        saw_procedural = true;
      if (mesh.source[0] == '\0')
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
    for (const field_info_t& field : emitter_info.fields)
    {
      if (strcmp(field.name, "sprite") == 0)
        sprite_field_names_its_class =
            field.type == FIELD_TYPE_ASSET && field.asset_class_id != NOT_AN_ASSET_CLASS;
    }
    check(sprite_field_names_its_class, "an asset field records which asset class it draws from");

    printf("mesh manifest (%u):", meshes.size());
    for (const assets::asset_info_t& mesh : meshes)
      printf(" %s", mesh.name);
    printf("\n");
  }

  // --- flags (P4) ---
  //
  // The three flags became real with the generator, so these guard the rules
  // the audit settled. The generator rejects the first two at build time; these
  // check that what it emitted actually says what the .def claims, which is the
  // half a parse error cannot cover.
  {
    bool component_fields_are_unflagged = true;
    bool runtime_only_fields_are_wire_only = true;

    for (uint32_t which = 1; which < ENTITY_TYPE_COUNT; ++which)
    {
      const entity_type_info_t& type_info = entity_info((entity_type)which);

      for (const field_info_t& field : type_info.fields)
      {
        // A component's own field flags are the truth; a flag at the use site
        // would be read by nobody.
        if (field.type == FIELD_TYPE_COMPONENT && field.flags != FIELD_FLAG_NONE)
          component_fields_are_unflagged = false;

        // The base's position/orientation are @Fully_Serializable and are
        // inherited by @runtime_only types too -- they describe the map-placed
        // types, and are simply never read for a runtime one. So exempt them
        // by name rather than pretending the rule is universal.
        const bool inherited_from_base = strcmp(field.name, "entity_id") == 0 ||
                                         strcmp(field.name, "position") == 0 ||
                                         strcmp(field.name, "orientation") == 0;

        if (type_info.runtime_only && !inherited_from_base &&
            (field.flags & (FIELD_FLAG_EDITABLE | FIELD_FLAG_SAVEABLE)) != 0)
          runtime_only_fields_are_wire_only = false;
      }
    }

    check(component_fields_are_unflagged,
          "no component-typed field carries flags of its own");
    check(runtime_only_fields_are_wire_only,
          "a @runtime_only type declares no @Editable/@Saveable field of its own");

    // The two decisions most likely to be reverted by accident, pinned so that
    // reverting them is a test failure rather than a silent bandwidth change.
    bool any_light_field_is_networked = false;
    for (const field_info_t& field : entity_info(entity_type::Light_Entity).fields)
      if (strcmp(field.name, "position") != 0 && strcmp(field.name, "orientation") != 0 &&
          strcmp(field.name, "entity_id") != 0 && (field.flags & FIELD_FLAG_NETWORKED) != 0)
        any_light_field_is_networked = true;
    check(!any_light_field_is_networked,
          "a light's own config is not replicated -- the client loaded the same map");

    bool every_render_field_is_networked = true;
    for (const field_info_t& field : component_info(component_type::Render).fields)
      if (field.type != FIELD_TYPE_COMPONENT && (field.flags & FIELD_FLAG_NETWORKED) == 0)
        every_render_field_is_networked = false;
    check(every_render_field_is_networked,
          "Render IS replicated -- runtime-spawned rockets and bodies have no other source");
  }

  printf("schema hash: 0x%08x\n", SCHEMA_HASH);
  printf("%s (%d failure%s)\n", failure_count == 0 ? "PASSED" : "FAILED", failure_count,
         failure_count == 1 ? "" : "s");
  return failure_count == 0 ? 0 : 1;
}
