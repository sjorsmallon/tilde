// Verifies the properties the inheritance layout was chosen for, plus the
// factory / placeable-type surface the generator emits on top of it.
#include "entities/entity_reflection.hpp"
#include "entities/generated/entities_generated.hpp"
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <vector>

using namespace entities;

// Trivial copyability, trivial destructibility and derivation from the base
// used to be asserted here, by hand, for four of the eight types. They are now
// emitted by def_gen for EVERY type, so including the generated header is the
// check -- and the type that gets forgotten is no longer the one a hand-kept
// list forgets. The one property left worth testing here is that a base
// pointer recovered from untyped memory is the same pointer the language
// would produce, which no static_assert can say (see "as_base" below).
static_assert(std::is_convertible_v<Spot_Light_Entity*, Entity*>);

static int32_t failure_count = 0;

static void check(bool condition, const char* description)
{
  printf("%s %s\n", condition ? "  ok  " : "FAILED", description);
  if (!condition)
    ++failure_count;
}

int main()
{
  Spot_Light_Entity light;
  light.position = {1.0f, 2.0f, 3.0f};

  // No cast, no aliasing violation: a real derived-to-base conversion.
  Entity* base = &light;
  check(base->type == entity_type::Spot_Light_Entity,
        "tag survives a derived-to-base conversion");
  check(base->position.x == 1.0f && base->position.y == 2.0f && base->position.z == 3.0f,
        "position readable through the base pointer");

  // The tag survives through the base pointer, so a generic caller can recover
  // the concrete type without being told what it is.
  const entity_type_info_t& info = entity_info(base->type);
  printf("classname=%s display=\"%s\" fields=%u size=%u runtime_only=%s\n", info.classname,
         info.display_name, info.fields.size(), info.size_in_bytes,
         info.runtime_only ? "true" : "false");
  check(strcmp(info.classname, "spot_light_entity") == 0,
        "classname derived from the declared name");
  check(strcmp(info.display_name, "Spot Light") == 0,
        "display name strips the _Entity suffix and unpacks the underscores");

  // memcmp diffing against a baseline.
  Spot_Light_Entity baseline;
  check(memcmp(&light, &baseline, sizeof(Spot_Light_Entity)) != 0,
        "a changed field is visible to a whole-struct memcmp");

  // Component lookup through the generated tables.
  Trigger_Volume_Entity trigger;
  check(has_component(entity_type::Trigger_Volume_Entity, component_type::Box_Volume),
        "Trigger_Volume declares a Box_Volume component");
  check(!has_component(entity_type::Spot_Light_Entity, component_type::Box_Volume),
        "Spot Light declares no Box_Volume component");

  // The split's structure, pinned: three types, one shared component. Collapsing
  // them back into one type carrying a kind enum fails right here.
  check(has_component(entity_type::Point_Light_Entity, component_type::Light) &&
            has_component(entity_type::Spot_Light_Entity, component_type::Light) &&
            has_component(entity_type::Directional_Light_Entity, component_type::Light),
        "all three light types share the Light component");
  check(!has_component(entity_type::Directional_Light_Entity, component_type::Render),
        "a light is not drawn from a Render component");

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

  check(entity_type_from_classname("spot_light_entity") == entity_type::Spot_Light_Entity,
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
      if (type == entity_type::Spot_Light_Entity)
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
  // The manifest models IDENTITY and nothing else. There is no source column
  // any more: it existed to tell a file-backed asset from a generated one, and
  // no consumer of an id ever asked. Two columns is the whole table.
  {
    Render render;
    check(render.mesh == assets::mesh_asset::Missing,
          "an unassigned mesh field reads as Missing, not as whichever asset sorted first");

    Particle_Emitter_Entity emitter;
    check(emitter.sprite == assets::texture_asset::Smoke,
          "a declared asset default resolves by name");

    check(strcmp(to_string(assets::mesh_asset::Pyramid), "Pyramid") == 0, "asset to_string");
    check(assets::try_from_string<assets::mesh_asset>("Sphere") == assets::mesh_asset::Sphere,
          "asset try_from_string round trip");
    check(!assets::try_from_string<assets::mesh_asset>("No_Such_Mesh"),
          "asset try_from_string rejects an unknown name");

    Span<const assets::asset_info_t> meshes = assets::mesh_asset_manifest();

    check(meshes.size() == assets::mesh_asset_COUNT, "the manifest covers every id in the enum");

    // Slot 0 has NO PATH, in every class. Its bytes are a compiled-in constant,
    // which is the whole job of a placeholder -- a placeholder that is a file
    // can be the thing that is missing. resources/obj/Error.obj is still an
    // asset, it is just an ordinary one now.
    check(strcmp(meshes[0].name, "Missing") == 0 && meshes[0].path == nullptr,
          "slot 0 is Missing with no file behind it");

    bool every_other_entry_has_a_path = true;
    for (uint32_t index = 1; index < meshes.size(); ++index)
      every_other_entry_has_a_path &= meshes[index].path != nullptr && meshes[index].path[0] != 0;
    check(every_other_entry_has_a_path,
          "every id but 0 names a file, so register_all can populate the manifest eagerly");

    // One class, two on-disk forms: a .obj static prop and a .mesh exported
    // with skin weights. Nothing that resolves a mesh_asset has to know which,
    // and that is why they are deliberately not two classes.
    auto ends_with = [](const char* text, const char* suffix)
    {
      const size_t text_length   = strlen(text);
      const size_t suffix_length = strlen(suffix);
      return text_length >= suffix_length &&
             strcmp(text + text_length - suffix_length, suffix) == 0;
    };

    bool saw_obj      = false;
    bool saw_dot_mesh = false;
    for (uint32_t index = 1; index < meshes.size(); ++index)
    {
      saw_obj |= ends_with(meshes[index].path, ".obj");
      saw_dot_mesh |= ends_with(meshes[index].path, ".mesh");
    }
    check(saw_obj && saw_dot_mesh, "one class carries both .obj and .mesh files");

    // Every class starts at Missing, not just this one -- the generated
    // get_<class> falls back to slot 0 for an id that came off the wire, so a
    // class whose slot 0 were an ordinary asset would resolve garbage to a real
    // thing.
    const Span<const assets::asset_info_t> classes[] = {
        assets::mesh_asset_manifest(),  assets::texture_asset_manifest(),
        assets::sound_asset_manifest(), assets::animation_asset_manifest(),
        assets::hitbox_rig_manifest(),  assets::font_asset_manifest(),
    };
    bool every_class_starts_at_missing = true;
    for (const Span<const assets::asset_info_t>& entries : classes)
      every_class_starts_at_missing &=
          entries.size() > 0 && strcmp(entries[0].name, "Missing") == 0 &&
          entries[0].path == nullptr;
    check(every_class_starts_at_missing, "id 0 of every class is the pathless placeholder");

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
    const entity_type light_types[] = {entity_type::Point_Light_Entity,
                                       entity_type::Spot_Light_Entity,
                                       entity_type::Directional_Light_Entity};
    for (entity_type light_type : light_types)
      for (const field_info_t& field : entity_info(light_type).fields)
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

  // --- enum-keyed arrays ---
  //
  // A `u32[Inventory_Slot]` field is one declaration and one TABLE ROW PER KEY, minted
  // by def_gen. Nothing downstream has an array case, so what has to hold is
  // that the rows it mints describe the real Enum_Array: right count, right
  // names, and an offset that lands on the element the key indexes. The last
  // one is what a wrong stride would break, and it would break it silently --
  // element 0 reads correctly and every element after it is garbage.
  {
    Span<const field_info_t> fields = component_info(component_type::Inventory).fields;

    int32_t weapon_row_count = 0;
    for (const field_info_t& field : fields)
      if (strncmp(field.name, "weapons.", 8) == 0)
        ++weapon_row_count;
    check(weapon_row_count == (int32_t)Inventory_Slot_COUNT,
          "an enum-keyed array mints one row per key");

    const char* expected_names[] = {"weapons.Primary", "weapons.Secondary", "weapons.Melee",
                                    "weapons.Utility_1", "weapons.Utility_2"};
    bool        names_in_order   = true;
    for (int32_t index = 0; index < (int32_t)Inventory_Slot_COUNT; ++index)
      if (strcmp(fields[index].name, expected_names[index]) != 0)
        names_in_order = false;
    check(names_in_order, "array rows are named field.KEY, in enum declaration order");

    // The offsets, against the storage they claim to describe. Writing through
    // the enum and reading back through the reflected offset is the whole
    // contract: it is what map I/O and the wire codec will do.
    Inventory inventory{};
    inventory.weapons[Inventory_Slot::Primary]   = 11u;
    inventory.weapons[Inventory_Slot::Secondary] = 22u;
    inventory.weapons[Inventory_Slot::Melee]     = 33u;
    inventory.weapons[Inventory_Slot::Utility_1] = 44u;
    inventory.weapons[Inventory_Slot::Utility_2] = 55u;

    const uint32_t expected_values[] = {11u, 22u, 33u, 44u, 55u};
    bool           offsets_land_right = true;
    for (int32_t index = 0; index < (int32_t)Inventory_Slot_COUNT; ++index)
    {
      uint32_t value = 0;
      memcpy(&value, (const uint8_t*)&inventory + fields[index].offset,
             fields[index].size_in_bytes);
      if (fields[index].size_in_bytes != sizeof(uint32_t) || value != expected_values[index])
        offsets_land_right = false;
    }
    check(offsets_land_right, "each array row's offset and size address its own element");

    // An array inside a component composes with the component's own offset the
    // way any leaf does -- no extra rule, which is the point of expanding into
    // ordinary rows.
    std::vector<leaf_field_t> leaves = collect_leaf_fields(entity_type::Player_Entity);

    bool found_dotted_leaf = false;
    for (const leaf_field_t& leaf : leaves)
      if (leaf.name == "inventory.weapons.Primary")
        found_dotted_leaf = true;
    check(found_dotted_leaf, "an array inside a component flattens to inventory.weapons.Primary");

    Player_Entity player{};
    player.inventory.weapons[Inventory_Slot::Primary] = 77u;
    for (const leaf_field_t& leaf : leaves)
      if (leaf.name == "inventory.weapons.Primary")
      {
        uint32_t value = 0;
        memcpy(&value, (const uint8_t*)&player + leaf.offset, sizeof(value));
        check(value == 77u, "the flattened leaf offset addresses the element through the entity");
      }
  }

  printf("schema hash: 0x%08x\n", SCHEMA_HASH);
  printf("%s (%d failure%s)\n", failure_count == 0 ? "PASSED" : "FAILED", failure_count,
         failure_count == 1 ? "" : "s");
  return failure_count == 0 ? 0 : 1;
}
