#include "map.hpp"
#include "asset.hpp"
#include "log.hpp"
#include "lightmap_sidecar.hpp"
#include "map_blocks.hpp"
#include "player_constants.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace shared
{

namespace
{

// Binary sidecar format for .navmesh files (polygon mesh, v3).
// Layout: magic(4) + version(4) + num_vertices(4) + num_polygons(4)
//       + vertices[num_vertices * (float x, float y, float z)]
//       + polygons[num_polygons * (uint32 num_verts, int32 verts[N], int32 neighbors[N], int32 island)]
static constexpr uint32_t NAVMESH_MAGIC   = 0x504F4C59; // "POLY"
static constexpr uint32_t NAVMESH_VERSION = 3;

static std::string navmesh_path_for(const std::string &map_path)
{
  auto dot = map_path.rfind('.');
  if (dot != std::string::npos)
    return map_path.substr(0, dot) + ".navmesh";
  return map_path + ".navmesh";
}

static void save_navmesh(const std::string &map_path, const navmesh_t &nav)
{
  std::ofstream out(navmesh_path_for(map_path), std::ios::binary);
  if (!out.is_open())
    return;

  auto write = [&](const auto &v) {
    out.write(reinterpret_cast<const char *>(&v), sizeof(v));
  };

  write(NAVMESH_MAGIC);
  write(NAVMESH_VERSION);
  write((uint32_t)nav.vertices.size());
  write((uint32_t)nav.polygons.size());

  for (const auto &v : nav.vertices)
  {
    write(v.position.x);
    write(v.position.y);
    write(v.position.z);
  }

  for (const auto &p : nav.polygons)
  {
    uint32_t n = (uint32_t)p.vertices.size();
    write(n);
    for (uint32_t k = 0; k < n; ++k) write(p.vertices[k]);
    for (uint32_t k = 0; k < n; ++k) write(p.neighbors[k]);
    write(p.island);
  }
}

static bool load_navmesh(const std::string &map_path, navmesh_t &nav)
{
  std::ifstream in(navmesh_path_for(map_path), std::ios::binary);
  if (!in.is_open())
    return false;

  auto read = [&](auto &v) {
    in.read(reinterpret_cast<char *>(&v), sizeof(v));
  };

  uint32_t magic, version;
  read(magic);
  read(version);
  if (magic != NAVMESH_MAGIC || version != NAVMESH_VERSION)
    return false;

  uint32_t num_verts, num_polys;
  read(num_verts);
  read(num_polys);

  nav.vertices.resize(num_verts);
  for (auto &v : nav.vertices)
  {
    read(v.position.x);
    read(v.position.y);
    read(v.position.z);
  }

  nav.polygons.resize(num_polys);
  for (auto &p : nav.polygons)
  {
    uint32_t n;
    read(n);
    p.vertices.resize(n);
    p.neighbors.resize(n);
    for (uint32_t k = 0; k < n; ++k) read(p.vertices[k]);
    for (uint32_t k = 0; k < n; ++k) read(p.neighbors[k]);
    read(p.island);
  }

  return in.good();
}



// ============================================================================
// One-time conversion of pre-exit geometry entities
//
// Geometry used to be entities, so it was written as 'entity' blocks with
// schema-serialized properties — including component fields flattened into a
// single "key:value|key:value" blob ("render", "volume"). Nothing writes that
// form any more; this reads it, and the next save emits proper geometry blocks.
// One conversion, no versioned format, no permanent shim.
// ============================================================================

// Split a legacy component blob into its key/value pairs. Splits on '|', then at
// the FIRST ':' of each part. Nested components were flattened into the same
// list, so "material:shader_type:lit|color:1 1 1" yields
// {"material": "shader_type:lit", "color": "1 1 1"} — the nested value keeps its
// own "key:value" text, which convert_legacy_render_component() below unpicks.
std::map<std::string, std::string> parse_legacy_component_blob(const std::string &blob)
{
  std::map<std::string, std::string> fields;
  size_t start = 0;
  while (start <= blob.size())
  {
    const size_t bar = blob.find('|', start);
    const std::string part =
        blob.substr(start, (bar == std::string::npos) ? std::string::npos : bar - start);

    const size_t colon = part.find(':');
    if (colon != std::string::npos)
      fields[part.substr(0, colon)] = part.substr(colon + 1);

    if (bar == std::string::npos)
      break;
    start = bar + 1;
  }
  return fields;
}

linalg::vec3 parse_legacy_vec3(const std::string &text, linalg::vec3 fallback)
{
  std::istringstream stream(text);
  linalg::vec3 parsed;
  if (stream >> parsed.x >> parsed.y >> parsed.z)
    return parsed;
  return fallback;
}

// Read a legacy "render" blob into the geometry surface it becomes.
void convert_legacy_render_component(const std::string &render_blob,
                                     geometry_surface_t &surface)
{
  const std::map<std::string, std::string> fields =
      parse_legacy_component_blob(render_blob);

  auto field = [&](const char *key) -> const std::string *
  {
    auto it = fields.find(key);
    return (it == fields.end()) ? nullptr : &it->second;
  };

  if (const std::string *mesh_path = field("mesh_path"))
    surface.mesh_path = *mesh_path;
  if (const std::string *visible = field("visible"))
    surface.visible = (*visible == "true" || *visible == "1");
  if (const std::string *is_wireframe = field("is_wireframe"))
    surface.is_wireframe = (*is_wireframe == "true" || *is_wireframe == "1");
  if (const std::string *color = field("color"))
    surface.color = parse_legacy_vec3(*color, surface.color);
  if (const std::string *roughness = field("roughness"))
  {
    try
    {
      surface.roughness = std::stof(*roughness);
    }
    catch (const std::exception &)
    {
      log_error("map conversion: legacy render roughness \"{}\" is not a float",
                *roughness);
    }
  }

  // material's own fields were flattened in, so its first one arrives as the
  // value of the "material" key: "shader_type:lit".
  if (const std::string *material = field("material"))
  {
    const size_t colon = material->find(':');
    if (colon != std::string::npos && material->substr(0, colon) == "shader_type")
      surface.shader_type = material->substr(colon + 1);
  }
}

// Convert one legacy geometry entity block. Returns false if this classname was
// never geometry (so the caller keeps treating it as an entity).
bool convert_legacy_geometry_entity(const std::string &classname,
                                    const std::map<std::string, std::string> &properties,
                                    geometry_value_t &out_geometry)
{
  auto property = [&](const char *key) -> const std::string *
  {
    auto it = properties.find(key);
    return (it == properties.end()) ? nullptr : &it->second;
  };

  // Legacy geometry's shape came from a box_volume_t component field named
  // "volume", holding just "half_extents:X Y Z" — and before THAT refactor, from
  // a flat "half_extents" property on the entity itself. Both forms are read
  // here, which is what retires the old "center"/"half_extents" compatibility
  // shims: they don't move to the geometry writer, they end at this conversion.
  auto legacy_half_extents = [&](linalg::vec3 fallback) -> linalg::vec3
  {
    if (const std::string *volume = property("volume"))
    {
      const std::map<std::string, std::string> fields =
          parse_legacy_component_blob(*volume);
      auto it = fields.find("half_extents");
      if (it != fields.end())
        return parse_legacy_vec3(it->second, fallback);
    }

    // Pre-box_volume_t form.
    if (const std::string *half_extents = property("half_extents"))
      return parse_legacy_vec3(*half_extents, fallback);

    return fallback;
  };

  // "center" is the pre-position name, from when the shape carried its own
  // center instead of the entity carrying a position.
  auto legacy_position = [&]() -> linalg::vec3
  {
    if (const std::string *position = property("position"))
      return parse_legacy_vec3(*position, {0, 0, 0});
    if (const std::string *center = property("center"))
      return parse_legacy_vec3(*center, {0, 0, 0});
    return {0, 0, 0};
  };

  auto legacy_surface = [&](geometry_surface_t &surface)
  {
    if (const std::string *render = property("render"))
      convert_legacy_render_component(*render, surface);
  };

  if (classname == "aabb_entity")
  {
    // An axis-aligned box is a brush now (geometry_def.md Track B), so the
    // legacy arm mints one directly rather than converting twice.
    brush_geometry_t brush =
        make_box_brush(legacy_position(), legacy_half_extents({1.f, 1.f, 1.f}));
    legacy_surface(brush.surface);
    out_geometry = std::move(brush);
    return true;
  }

  if (classname == "static_mesh_entity")
  {
    static_mesh_geometry_t static_mesh;
    static_mesh.position = legacy_position();
    if (const std::string *orientation = property("orientation"))
      static_mesh.orientation =
          linalg::from_euler_degrees(parse_legacy_vec3(*orientation, {0, 0, 0}));
    legacy_surface(static_mesh.surface);

    // Scale lived on the render component, not on the entity.
    if (const std::string *render = property("render"))
    {
      const std::map<std::string, std::string> fields = parse_legacy_component_blob(*render);
      auto it = fields.find("scale");
      if (it != fields.end())
        static_mesh.scale = parse_legacy_vec3(it->second, static_mesh.scale);
    }

    out_geometry = std::move(static_mesh);
    return true;
  }

  if (classname == "displacement_entity")
  {
    // A displacement was a BOX with one subdivided face, and that is now what a
    // brush with one subdivided face is (geometry_def.md Track D). So the arm
    // mints the box, finds the plane of the face the grid was on, and hands the
    // grid straight over -- the legacy row-major (i along the face's u tangent,
    // j along its v) is the order face_grid_base_vertex walks, so no index is
    // remapped and no vertex is resampled.
    const linalg::vec3 position     = legacy_position();
    const linalg::vec3 half_extents = legacy_half_extents({1.f, 1.f, 1.f});

    box_face_t active_face = box_face_t::Invalid;
    if (const std::string *raw = property("active_face"))
    {
      try
      {
        const int32_t parsed = (int32_t)std::stol(*raw);
        if (parsed < 0 || parsed >= (int32_t)box_face_count)
          log_error("map conversion: legacy active_face {} is not a box face", parsed);
        else
          active_face = (box_face_t)parsed;
      }
      catch (const std::exception &)
      {
        log_error("map conversion: legacy active_face \"{}\" is not an int", *raw);
      }
    }

    int32_t subdivision_level = 4;
    if (const std::string *raw = property("subdivision_level"))
    {
      try
      {
        subdivision_level = (int32_t)std::stol(*raw);
      }
      catch (const std::exception &)
      {
        log_error("map conversion: legacy subdivision_level \"{}\" is not an int", *raw);
      }
    }
    if (subdivision_level < 0)
    {
      log_error("map conversion: legacy subdivision_level {} is negative — flat",
                subdivision_level);
      subdivision_level = 0;
    }

    // The legacy array was "<float_count> f f f ...", three floats per vertex.
    std::vector<linalg::vec3> grid;
    if (const std::string *raw = property("displacements"))
    {
      std::istringstream stream(*raw);
      size_t announced_float_count = 0;
      if (stream >> announced_float_count)
      {
        linalg::vec3 value;
        while (stream >> value.x >> value.y >> value.z)
          grid.push_back(value);

        if (grid.size() * 3 != announced_float_count)
          log_error("map conversion: legacy displacements announced {} floats, "
                    "read {} vertices ({} floats)",
                    announced_float_count, grid.size(), grid.size() * 3);
      }
      else
      {
        log_error("map conversion: legacy displacements has no leading float "
                  "count — grid left flat");
      }
    }

    brush_geometry_t brush = make_box_brush(position, half_extents);
    legacy_surface(brush.surface);

    if (active_face == box_face_t::Invalid)
    {
      // A displacement that never had a face was a box, and converts to one.
      out_geometry = std::move(brush);
      return true;
    }

    const linalg::vec3 normal = get_box_face_normal(active_face);
    Plane plane;
    plane.normal = normal;
    plane.point  = position + linalg::vec3{normal.x * half_extents.x,
                                           normal.y * half_extents.y,
                                           normal.z * half_extents.z};

    face_surface_t &face = face_surface_for(brush, plane);
    resize_face_grid(face, subdivision_level);

    const size_t expected = face.offsets.size();
    if (grid.size() != expected)
    {
      log_error("map conversion: legacy displacement grid holds {} vertices, "
                "subdivision {} wants {} — padding with flat",
                grid.size(), subdivision_level, expected);
      grid.resize(expected, linalg::vec3{0, 0, 0});
    }
    face.offsets = std::move(grid);

    out_geometry = std::move(brush);
    return true;
  }

  return false;
}

// ============================================================================
// Entity blocks: classnames and fields
// ============================================================================

// The classnames the macro system wrote, for the types whose generated
// classname differs. The generator derives a classname from the declared type
// name (Spot_Light_Entity -> "spot_light_entity"); the X-macro let each entry
// pick its own string, and five of them picked something else.
//
// This is the "renames via one-time map conversion" rule from the phase plan,
// running on load rather than as a separate pass: a map written before the
// cutover still loads, and the first save rewrites it with the generated name,
// after which the alias is never consulted for that file again. No version
// number is involved, and none is needed -- the old name either appears or it
// does not.
struct legacy_classname_t
{
  const char *legacy;
  const char *current;
};

constexpr legacy_classname_t LEGACY_CLASSNAMES[] = {
    {"player_start", "player_spawn_entity"},
    {"weapon_basic", "weapon_entity"},
    {"particle_emitter", "particle_emitter_entity"},
    {"trigger_volume", "trigger_volume_entity"},
    {"physics_body", "physics_body_entity"},
};

// DELIBERATELY NOT ALIASED: "light_entity", which was one type carrying a
// Light_Type of {Point, Spot, Directional} before the split into three. An
// alias can only name ONE successor, so a spot light in an old file would load
// silently as a point light -- the wrong shape, with its cone angles dropped as
// unknown fields. No map in the tree ever had one, so the loud "unknown entity
// classname" the loader already logs is the correct outcome for a file that
// somehow does.

} // namespace

std::shared_ptr<entities::Entity> create_map_entity(const std::string &classname)
{
  entities::entity_type type = entities::entity_type_from_classname(classname.c_str());

  if (type == entities::entity_type::Invalid)
  {
    for (const legacy_classname_t &alias : LEGACY_CLASSNAMES)
    {
      if (classname != alias.legacy)
        continue;
      type = entities::entity_type_from_classname(alias.current);
      break;
    }
  }

  return make_entity(type);
}

std::shared_ptr<entities::Entity> make_entity(entities::entity_type type)
{
  if (type == entities::entity_type::Invalid)
    return nullptr;

  // create_entity() gives back the CONCRETE type's raw pointer but erases it
  // to Entity*, so the deleter has to be attached explicitly here -- entities
  // have no virtual destructor and never will, and destroy_entity is what
  // recovers the concrete type from the runtime tag before delete.
  entities::Entity *entity = entities::create_entity(type);
  return std::shared_ptr<entities::Entity>(entity, &entities::destroy_entity);
}

std::pair<entity_uid_t, std::shared_ptr<entities::Entity>>
spawn_entity(map_t &map, entities::entity_type type)
{
  std::shared_ptr<entities::Entity> entity = make_entity(type);
  if (!entity)
    return {0, nullptr};

  entity_uid_t uid = map.add_entity(entity);
  return {uid, entity};
}

namespace
{

// Rewrites a pre-cutover entity block into the shape read_entity_fields expects.
// The third and last piece of the one-time conversion, after the classname
// aliases and the numeric-enum fallback. Three transformations, all of them
// consequences of decisions recorded in entities.def:
//
//   1. COMPONENT BLOBS -> DOTTED KEYS. A component used to serialize into one
//      value ("volume" = "half_extents:1 2 3"); it is now one key per leaf
//      ("volume.half_extents"). Same split rule as the geometry conversion.
//   2. RENAMED KEYS. Only where a field's NAME changed, which is the one
//      versioning case the no-version-numbers rule cannot absorb.
// A third legacy form -- enum values as lowercase strings -- is handled in
// read_entity_fields instead; see the note at the bottom of this function.
//
// Values already in the new form pass through untouched, so this is idempotent
// and costs a converted map nothing.
void rewrite_legacy_entity_properties(std::map<std::string, std::string> &properties)
{
  // 1. Component blobs.
  // "hitbox" is deliberately absent: that component is gone, so a legacy blob
  // naming it should take the unknown-key path in read_entity_fields (logged and
  // ignored) rather than be split into three keys that are equally unknown.
  static constexpr const char *BLOB_KEYS[] = {"volume", "render", "material"};
  for (const char *blob_key : BLOB_KEYS)
  {
    auto it = properties.find(blob_key);
    if (it == properties.end() || it->second.find(':') == std::string::npos)
      continue;

    for (const auto &[inner_key, inner_value] : parse_legacy_component_blob(it->second))
      properties.emplace(std::string(blob_key) + "." + inner_key, inner_value);

    properties.erase(it);
  }

  // 2. Renamed keys. `action_name` held the string the server looked up in the
  //    trigger action registry; it is the Trigger_Action enum `action` now.
  static constexpr legacy_classname_t RENAMED_KEYS[] = {
      {"action_name", "action"},
  };
  for (const legacy_classname_t &rename : RENAMED_KEYS)
  {
    auto it = properties.find(rename.legacy);
    if (it == properties.end())
      continue;
    properties.emplace(rename.current, it->second);
    properties.erase(it);
  }

  // The third legacy form -- enum values spelled as lowercase strings -- is
  // deliberately NOT handled here. This function sees keys and text but no
  // field types, so it cannot tell an enum from a string, and blanket
  // title-casing turned the string field param_target_name "spawn_a" into
  // "Spawn_A". It lives in read_entity_fields, which has the field record.
}

// Reads a block's properties into an already-constructed entity.
//
// VERSIONING, which has no version numbers by design:
//   * a @Saveable key the file does not carry keeps the DSL default, so adding
//     a field is free and old maps load;
//   * a key the entity has no field for is ignored with a warning, so removing
//     a field is free and the warning says what was dropped;
//   * a key that does not parse is left at its default with an error, never
//     half-written and never silently zeroed.
// Renames are the one case that needs a real conversion, which is what the
// legacy table above is for.
void read_entity_fields(entities::Entity &entity, const std::string &classname,
                        std::map<std::string, std::string> properties)
{
  // Taken by value: the legacy rewrite below edits it, and the caller's block
  // is what the map file literally said. One conversion, in one place.
  rewrite_legacy_entity_properties(properties);

  const std::vector<entities::leaf_field_t> leaves =
      entities::collect_leaf_fields(entity.type, entities::FIELD_FLAG_SAVEABLE);

  uint8_t *base = reinterpret_cast<uint8_t *>(&entity);

  // Which keys were consumed, so the leftovers can be reported rather than
  // vanishing. Sized off the block, not the schema: an unknown key is exactly
  // what this is looking for.
  std::map<std::string, bool> consumed;
  for (const auto &[key, value] : properties)
    consumed[key] = false;

  for (const entities::leaf_field_t &leaf : leaves)
  {
    auto it = properties.find(leaf.name);
    if (it == properties.end())
      continue; // additive change: keep the DSL default

    consumed[leaf.name] = true;

    if (field_from_text(it->second, *leaf.info, base + leaf.offset))
      continue;

    // Legacy value form, the second half of the one-time conversion the
    // classname aliases above are the first half of. Fields that are enums now
    // were plain ints in the macro system (spawn_type "0", light kind "2"), so
    // a pre-cutover map spells them as a NUMBER where field_from_text wants the
    // value's NAME. Accept the number, once, and the next save writes the name.
    //
    // Deliberately not in field_from_text: that is the canonical reader, and an
    // enum whose text is "2" is exactly the sort of thing it should reject.
    // This is map.cpp's business because only map.cpp is reading a file that
    // might predate the change.
    // Legacy value form: a rotation was three euler degrees until the quaternion
    // cutover, and is four components now. The GEOMETRY reader tells the two
    // apart by the KEY (`orientation` is read-only there, `rotation` is what the
    // writer emits) -- which is not available here, because an entity's key IS
    // its field name in entities.def. So this one discriminates on the value's
    // shape instead, converts through the same from_euler_degrees, and the next
    // save writes four components under the same key.
    if (leaf.info->type == FIELD_TYPE_QUAT)
    {
      std::istringstream stream(it->second);
      linalg::vec3       euler_degrees;
      float              extra = 0.f;

      if ((stream >> euler_degrees.x >> euler_degrees.y >> euler_degrees.z) &&
          !(stream >> extra))
      {
        const linalg::quatf rotation = linalg::from_euler_degrees(euler_degrees);
        std::memcpy(base + leaf.offset, &rotation, sizeof(rotation));
        log_warning("map parse: {}.{} was the legacy euler \"{}\"; read as a rotation. "
                    "The next save writes four components.",
                    classname, leaf.name, it->second);
        continue;
      }
    }

    if (leaf.info->type == FIELD_TYPE_ENUM)
    {
      const enum_type_info_t &enum_info = *leaf.info->enum_info;

      char *parse_end = nullptr;
      const long numeric = std::strtol(it->second.c_str(), &parse_end, 10);

      const bool parsed_whole = parse_end != nullptr && *parse_end == '\0' &&
                                parse_end != it->second.c_str();

      if (parsed_whole && numeric >= 0 && (uint32_t)numeric < enum_info.value_names.size())
      {
        base[leaf.offset] = (uint8_t)numeric;
        log_warning("map parse: {}.{} was the legacy numeric \"{}\"; read as {}. "
                    "The next save writes the name.",
                    classname, leaf.name, it->second,
                    enum_info.value_names[(uint32_t)numeric]);
        continue;
      }

      // Legacy form B: the value was a lowercase string ("on_enter",
      // "warp_to_spawn", "lit", "sphere") where the generated name is
      // Title_Case. Capitalise the first letter and each one after an
      // underscore, then look that up.
      std::string title_case = it->second;
      bool at_word_start = true;
      for (char &character : title_case)
      {
        if (at_word_start && character >= 'a' && character <= 'z')
          character = (char)(character - 'a' + 'A');
        at_word_start = (character == '_');
      }

      if (field_from_text(title_case, *leaf.info, base + leaf.offset))
      {
        log_warning("map parse: {}.{} was the legacy spelling \"{}\"; read as {}. "
                    "The next save writes the name.",
                    classname, leaf.name, it->second, title_case);
        continue;
      }
    }

    log_error("map parse: {}.{} could not be read from \"{}\" — left at its default",
              classname, leaf.name, it->second);
  }

  for (const auto &[key, was_consumed] : consumed)
  {
    if (was_consumed || key == "classname" || key == "_uid")
      continue;

    // A warning, not an error: this is the documented behavior for a REMOVED
    // field, and removals are meant to be free. Pre-cutover maps all carry at
    // least one (entity_id, which the macro system saved because it saved every
    // field regardless of flags and is @Networked-only now), so treating it as
    // an error would bury the real errors in expected noise.
    log_warning("map parse: {} has no saveable field named \"{}\" — key ignored", classname,
                key);
  }
}

} // namespace

// ============================================================================
// Per-entity picking bounds
//
// One exhaustive switch over the closed enum, where this used to be a template
// whose primary was deliberately left undefined so a missing specialization
// became a link error. The switch is the better version of the same trick: an
// unhandled entity_type is a -Wswitch warning at COMPILE time, in this file,
// naming the type -- rather than an undefined symbol at link time.
// ============================================================================

namespace
{

// Mesh-bounds-or-default-box. Shared by every entity whose picking shape is
// "whatever the Render component's mesh says, with a small fallback if the mesh
// has not loaded yet".
aabb_bounds_t mesh_or_point_bounds(const entities::Entity *entity,
                                   float fallback_half = 0.5f)
{
  if (const entities::Render *render = entities::get_render(entity))
  {
    const assets::mesh_asset_t *mesh = assets::get(assets::get_mesh(render->mesh));

    if (mesh && !mesh->vertices.empty())
    {
      aabb_bounds_t mesh_bounds = assets::compute_mesh_bounds(mesh);
      vec3f mesh_center = (mesh_bounds.min + mesh_bounds.max) * 0.5f;
      vec3f mesh_half = (mesh_bounds.max - mesh_bounds.min) * 0.5f;
      vec3f s = render->scale;
      vec3f world_center =
          entity->position + vec3f{mesh_center.x * s.x, mesh_center.y * s.y,
                                   mesh_center.z * s.z};
      vec3f world_half = vec3f{mesh_half.x * s.x, mesh_half.y * s.y,
                               mesh_half.z * s.z};
      return {world_center - world_half, world_center + world_half};
    }
  }
  return {entity->position -
              vec3f{fallback_half, fallback_half, fallback_half},
          entity->position +
              vec3f{fallback_half, fallback_half, fallback_half}};
}

// The player hull, used by both player-shaped types.
//
// The origin is at the FEET -- the same convention as `player_eye_height`, the
// hitbox table and the runtime's `player->position = spawn_position` -- so the
// hull rises from position rather than straddling it.
aabb_bounds_t player_hull_bounds(const entities::Entity *entity)
{
  return {{entity->position.x - player_half_width,
           entity->position.y,
           entity->position.z - player_half_width},
          {entity->position.x + player_half_width,
           entity->position.y + 2.f * player_half_height,
           entity->position.z + player_half_width}};
}

} // namespace

aabb_bounds_t compute_entity_bounds(const entities::Entity *entity)
{
  if (!entity)
  {
    log_error("compute_entity_bounds called with null entity");
    return {{0, 0, 0}, {0, 0, 0}};
  }

  switch (entity->type)
  {
    case entities::entity_type::Player_Spectate_Entity:
      // Drawn as the camera frustum, so it picks as one. A spectate spot is a
      // camera and nothing ever stands there, so the player hull it used to
      // report was a volume the picture never occupies.
      return get_bounds(make_spectate_frustum(entity->position, entity->orientation));

    case entities::entity_type::Player_Spawn_Entity:
      return player_hull_bounds(entity);

    case entities::entity_type::Player_Entity:
    {
      // Drawn as the pyramid marker, so it picks as one -- the Render component
      // is not what a player is drawn from.
      const assets::mesh_asset_t *mesh = assets::get(assets::get_mesh(assets::mesh_asset::Pyramid));
      if (mesh && !mesh->vertices.empty())
      {
        aabb_bounds_t mesh_bounds = assets::compute_mesh_bounds(mesh);
        return {entity->position + mesh_bounds.min, entity->position + mesh_bounds.max};
      }
      return player_hull_bounds(entity);
    }

    case entities::entity_type::Trigger_Volume_Entity:
    {
      const entities::Box_Volume *volume = entities::get_box_volume(entity);
      assert(volume != nullptr && "Trigger_Volume_Entity lost its Box_Volume component");
      return get_bounds(*volume, entity->position);
    }

    case entities::entity_type::Damageable_Entity:
    {
      // Picks as the volume you SHOOT, not as the mesh you see. Those can
      // differ -- a mesh is art and a hitbox is gameplay -- and when they do it
      // is the hitbox an author is placing, so it is the hitbox the editor's
      // handle has to wrap.
      const entities::Damageable_Entity *damageable =
          entities::entity_as<entities::Damageable_Entity>(entity);
      assert(damageable != nullptr && "Damageable_Entity failed its own type test");
      return {entity->position - damageable->hitbox_half_extents,
              entity->position + damageable->hitbox_half_extents};
    }

    case entities::entity_type::Weapon_Entity:
    case entities::entity_type::Rocket_Entity:
    case entities::entity_type::Particle_Emitter_Entity:
    case entities::entity_type::Point_Light_Entity:
    case entities::entity_type::Spot_Light_Entity:
    case entities::entity_type::Directional_Light_Entity:
    case entities::entity_type::Physics_Body_Entity:
      return mesh_or_point_bounds(entity);

    case entities::entity_type::Invalid:
      break;
  }

  log_error("compute_entity_bounds: entity carries an invalid type tag ({})",
            (int)entity->type);
  return {entity->position - vec3f{0.5f, 0.5f, 0.5f},
          entity->position + vec3f{0.5f, 0.5f, 0.5f}};
}

std::vector<Plane> compute_entity_collision_planes(const entities::Entity *entity)
{
  // Entities are not collision geometry — that's the map's geometry list now
  // (see get_collision_pieces in map_geometry.hpp). What's left here serves the
  // callers that want an entity's shape for overlap tests: a box volume if it
  // has one (trigger volumes), otherwise its picking bounds.
  if (const entities::Box_Volume *volume = entities::get_box_volume(entity))
    return compute_collision_planes(to_aabb(*volume, entity->position));

  // The one entity whose hull is not its bound: the frustum's corner is empty
  // space, and a click there should fall through to whatever is behind it.
  if (entity->type == entities::entity_type::Player_Spectate_Entity)
    return compute_collision_planes(
        make_spectate_frustum(entity->position, entity->orientation));

  auto bounds = compute_entity_bounds(entity);
  aabb_t t;
  t.center = (bounds.min + bounds.max) * 0.5f;
  t.half_extents = (bounds.max - bounds.min) * 0.5f;
  return compute_collision_planes(t);
}

// ============================================================================
// Uniform per-uid accessors over both regimes
// ============================================================================

aabb_bounds_t compute_object_bounds(const map_t &map, entity_uid_t uid)
{
  if (const map_geometry_t *entry = map.find_geometry_by_uid(uid))
    return get_bounds(entry->value);

  if (const map_entity_t *entry = map.find_by_uid(uid))
    return compute_entity_bounds(entry->entity.get());

  log_error("compute_object_bounds: no map object has uid {}", uid);
  return {{0, 0, 0}, {0, 0, 0}};
}

std::optional<linalg::vec3> try_get_object_position(const map_t &map, entity_uid_t uid)
{
  if (const map_geometry_t *entry = map.find_geometry_by_uid(uid))
    return get_position(entry->value);

  if (const map_entity_t *entry = map.find_by_uid(uid); entry && entry->entity)
    return entry->entity->position;

  return std::nullopt;
}

std::optional<aabb_t> try_get_object_box(const map_t &map, entity_uid_t uid)
{
  if (const map_geometry_t *entry = map.find_geometry_by_uid(uid))
  {
    switch (get_kind(entry->value))
    {
    case geometry_kind_t::Static_Mesh:
      return std::nullopt; // sized by its mesh asset

    case geometry_kind_t::Brush:
    {
      return to_aabb(
          compute_brush_bounds(std::get<brush_geometry_t>(entry->value).hull_points));
    }
    }

    return std::nullopt;
  }

  if (const map_entity_t *entry = map.find_by_uid(uid); entry && entry->entity)
  {
    if (const entities::Box_Volume *volume = entities::get_box_volume(entry->entity.get()))
      return aabb_t{entry->entity->position + volume->position, volume->half_extents};
  }

  return std::nullopt;
}

bool try_set_object_box(map_t &map, entity_uid_t uid, const aabb_t &box)
{
  const linalg::vec3 &center       = box.center;
  const linalg::vec3 &half_extents = box.half_extents;

  if (map_geometry_t *entry = map.find_geometry_by_uid(uid))
  {
    switch (get_kind(entry->value))
    {
    case geometry_kind_t::Static_Mesh:
      return false;

    case geometry_kind_t::Brush:
    {
      // Scale the point set so its bound becomes the requested box. A brush has
      // no extents of its own to assign, so a gizmo resize has to reach the
      // vertices -- and an axis the brush is flat on has no scale factor, so it
      // only translates.
      brush_geometry_t   &brush = std::get<brush_geometry_t>(entry->value);
      const aabb_bounds_t bounds = compute_brush_bounds(brush.hull_points);
      const linalg::vec3  old_center      = (bounds.min + bounds.max) * 0.5f;
      const linalg::vec3  old_half_extents = (bounds.max - bounds.min) * 0.5f;

      const linalg::vec3 scale = {
          old_half_extents.x > 1e-4f ? half_extents.x / old_half_extents.x : 1.f,
          old_half_extents.y > 1e-4f ? half_extents.y / old_half_extents.y : 1.f,
          old_half_extents.z > 1e-4f ? half_extents.z / old_half_extents.z : 1.f,
      };

      for (linalg::vec3 &vertex : brush.hull_points)
      {
        vertex = {center.x + (vertex.x - old_center.x) * scale.x,
                  center.y + (vertex.y - old_center.y) * scale.y,
                  center.z + (vertex.z - old_center.z) * scale.z};
      }

      // A non-uniform scale ROTATES every face that is not axis-aligned, so
      // unlike a translation there is no key rewrite to do by hand -- re-key
      // against the hull the scale produced.
      sync_face_surfaces(brush);
      return true;
    }
    }

    return false;
  }

  if (map_entity_t *entry = map.find_by_uid(uid); entry && entry->entity)
  {
    if (entities::Box_Volume *volume = entities::get_box_volume(entry->entity.get()))
    {
      // The entity moves; the volume's local offset is left alone, so a box
      // authored off-center stays off-center when it is dragged.
      entry->entity->position = center - volume->position;
      volume->half_extents = half_extents;
      return true;
    }
  }

  return false;
}

std::vector<std::pair<entity_uid_t, aabb_bounds_t>>
collect_object_bounds(const map_t &map)
{
  std::vector<std::pair<entity_uid_t, aabb_bounds_t>> result;
  result.reserve(map.object_count());

  for (const map_geometry_t &entry : map.geometry)
    result.emplace_back(entry.uid, get_bounds(entry.value));

  for (const map_entity_t &entry : map.entities)
  {
    if (!entry.entity)
      continue;
    result.emplace_back(entry.uid, compute_entity_bounds(entry.entity.get()));
  }

  return result;
}

std::optional<linalg::quatf> try_get_object_orientation(const map_t &map, entity_uid_t uid)
{
  if (const map_geometry_t *entry = map.find_geometry_by_uid(uid))
  {
    if (get_kind(entry->value) != geometry_kind_t::Static_Mesh)
      return std::nullopt;

    return std::get<static_mesh_geometry_t>(entry->value).orientation;
  }

  if (const map_entity_t *entry = map.find_by_uid(uid); entry && entry->entity)
    return entry->entity->orientation;

  return std::nullopt;
}

bool try_set_object_orientation(map_t &map, entity_uid_t uid,
                                const linalg::quatf &orientation)
{
  if (map_geometry_t *entry = map.find_geometry_by_uid(uid))
  {
    if (get_kind(entry->value) != geometry_kind_t::Static_Mesh)
      return false;

    std::get<static_mesh_geometry_t>(entry->value).orientation = orientation;
    return true;
  }

  if (map_entity_t *entry = map.find_by_uid(uid); entry && entry->entity)
  {
    entry->entity->orientation = orientation;
    return true;
  }

  return false;
}

bool try_set_object_position(map_t &map, entity_uid_t uid, const linalg::vec3 &position)
{
  if (map_geometry_t *entry = map.find_geometry_by_uid(uid))
  {
    set_position(entry->value, position);
    return true;
  }

  if (map_entity_t *entry = map.find_by_uid(uid); entry && entry->entity)
  {
    entry->entity->position = position;
    return true;
  }

  return false;
}

std::vector<std::vector<linalg::vec3>> compute_entity_face_polygons(const entities::Entity *entity)
{
  // Any entity that owns a box volume -> 6 axis-aligned face quads
  if (const entities::Box_Volume *volume = entities::get_box_volume(entity))
    return compute_face_polygons(to_aabb(*volume, entity->position));

  // Parallel to compute_entity_collision_planes above, which means this one
  // needs the frustum too.
  if (entity->type == entities::entity_type::Player_Spectate_Entity)
    return compute_face_polygons(
        make_spectate_frustum(entity->position, entity->orientation));

  // Fallback: use entity bounds as an AABB
  auto bounds = compute_entity_bounds(entity);
  aabb_t t;
  t.center = (bounds.min + bounds.max) * 0.5f;
  t.half_extents = (bounds.max - bounds.min) * 0.5f;
  return compute_face_polygons(t);
}

cvar_line_t split_cvar_line(std::string_view line)
{
  const size_t name_start = line.find_first_not_of(" \t");
  if (name_start == std::string_view::npos)
    return {};

  const size_t name_end = line.find_first_of(" \t", name_start);
  if (name_end == std::string_view::npos)
    return {std::string(line.substr(name_start)), {}};

  const size_t value_start = line.find_first_not_of(" \t", name_end);
  return {std::string(line.substr(name_start, name_end - name_start)),
          value_start == std::string_view::npos
              ? std::string()
              : std::string(line.substr(value_start))};
}

std::string make_cvar_line(std::string_view name, std::string_view value)
{
  if (value.empty())
    return std::string(name);
  return std::string(name) + " " + std::string(value);
}

namespace
{

std::optional<uint32_t> try_material_index_from_text(const std::string &text)
{
  if (text.empty())
    return std::nullopt;

  uint32_t value = 0;
  for (const char character : text)
  {
    if (character < '0' || character > '9')
      return std::nullopt;
    value = value * 10 + (uint32_t)(character - '0');
    if (value > UINT16_MAX)
      return std::nullopt;
  }
  return value;
}

// Which entries of `materials` a brush face actually names, and where each one
// lands once the unreferenced ones are dropped. Entry 0 is the map default and
// is always kept, so a map with no textured face still writes a one-entry table
// and every index in it stays valid.
struct material_remap_t
{
  std::vector<std::string> kept;
  std::vector<uint16_t> old_to_new;
};

material_remap_t build_material_remap(const map_t &map)
{
  std::vector<bool> referenced(map.materials.size(), false);
  if (!referenced.empty())
    referenced[0] = true;

  for (const map_geometry_t &entry : map.geometry)
  {
    const brush_geometry_t *brush = std::get_if<brush_geometry_t>(&entry.value);
    if (!brush)
      continue;

    for (const face_surface_t &face : brush->face_surfaces)
    {
      // Every LAYER's material counts as a reference, not just the base one:
      // an entry named only by a blend layer that got dropped here would
      // silently retarget that layer at the next save.
      for (int layer = 0; layer < BLEND_LAYER_COUNT; ++layer)
      {
        const uint16_t material = face_layer_material(face, layer);
        if (material < referenced.size())
          referenced[material] = true;
      }
    }
  }

  material_remap_t remap;
  remap.old_to_new.assign(map.materials.size(), 0);
  for (size_t i = 0; i < map.materials.size(); ++i)
  {
    if (!referenced[i])
      continue;
    remap.old_to_new[i] = (uint16_t)remap.kept.size();
    remap.kept.push_back(map.materials[i]);
  }

  if (remap.kept.empty())
    remap.kept.emplace_back();

  return remap;
}

} // namespace

map_t parse_map_from_string(const std::string &content)
{
  const std::vector<map_block_t> blocks = parse_map_content(content);
  map_t out_map;

  // "_uid" is written by both regimes; absent (a hand-authored map) means
  // auto-assign. Returns 0 for "not present".
  auto read_uid = [](const map_block_t &block) -> entity_uid_t
  {
    auto it = block.properties.find("_uid");
    if (it == block.properties.end())
      return 0;

    try
    {
      return (entity_uid_t)std::stoul(it->second);
    }
    catch (const std::exception &)
    {
      log_error("map parse: block \"{}\" has a malformed _uid \"{}\" — "
                "auto-assigning",
                block.keyword, it->second);
      return 0;
    }
  };

  for (const map_block_t &block : blocks)
  {
    const entity_uid_t uid = read_uid(block);

    // --- the map's own cvar settings ---
    if (block.keyword == "cvars")
    {
      for (const auto &[name, value] : block.properties)
        out_map.attached_cvars.push_back(make_cvar_line(name, value));
      continue;
    }

    // --- the material table brush faces index into ---
    //
    // Keyed by INDEX rather than written as a list, because a block's properties
    // are a map and "10" sorts before "2": the index has to be said, not implied
    // by position. A gap reads as the map default rather than shifting every
    // entry after it, which is the failure that would silently retexture a map.
    if (block.keyword == "materials")
    {
      for (const auto &[key, path] : block.properties)
      {
        const std::optional<uint32_t> index = try_material_index_from_text(key);
        if (!index)
        {
          log_error("map parse: material key \"{}\" is not an index — entry skipped", key);
          continue;
        }
        if (out_map.materials.size() <= *index)
          out_map.materials.resize(*index + 1);
        out_map.materials[*index] = path;
      }
      continue;
    }

    // --- geometry blocks ---
    if (block.keyword != "entity")
    {
      geometry_value_t geometry;
      if (!parse_geometry(block.keyword, block.properties, block.children, geometry))
      {
        log_error("map parse: unknown block keyword \"{}\" — block skipped",
                  block.keyword);
        continue;
      }

      if (uid != 0)
        out_map.add_geometry_with_uid(uid, std::move(geometry));
      else
        out_map.add_geometry(std::move(geometry));
      continue;
    }

    // --- entity blocks ---
    auto classname_it = block.properties.find("classname");
    if (classname_it == block.properties.end())
    {
      log_error("map parse: entity block has no \"classname\" — block skipped");
      continue;
    }
    const std::string &classname = classname_it->second;

    if (classname == "worldspawn")
    {
      auto name_it = block.properties.find("name");
      if (name_it != block.properties.end())
        out_map.name = name_it->second;
      continue;
    }

    // Geometry written by a pre-exit build arrives as an entity block. Convert
    // it in place; the next save writes it as a geometry block and the legacy
    // form is gone from that file forever.
    {
      geometry_value_t geometry;
      if (convert_legacy_geometry_entity(classname, block.properties, geometry))
      {
        if (uid != 0)
          out_map.add_geometry_with_uid(uid, std::move(geometry));
        else
          out_map.add_geometry(std::move(geometry));
        continue;
      }
    }

    // Wedges were retired before the geometry exit and never came back — the
    // geometry kinds are brush / static mesh. Drop them loudly;
    // they are real data being discarded.
    if (classname == "wedge_entity")
    {
      auto position_it = block.properties.find("position");
      log_error("map parse: dropped a retired wedge_entity at position \"{}\" — "
                "wedges are not a geometry kind; rebuild it from boxes",
                position_it == block.properties.end() ? "?" : position_it->second);
      continue;
    }

    std::shared_ptr<entities::Entity> new_entity = create_map_entity(classname);
    if (!new_entity)
    {
      log_error("map parse: unknown entity classname \"{}\" — entity skipped",
                classname);
      continue;
    }

    read_entity_fields(*new_entity, classname, block.properties);

    if (uid != 0)
      out_map.add_entity_with_uid(uid, new_entity);
    else
      out_map.add_entity(new_entity);
  }

  return out_map;
}

std::optional<map_t> try_load_map(const std::string &filename)
{
  std::ifstream in(filename);
  if (!in.is_open())
    return std::nullopt;

  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string content = buffer.str();
  in.close();

  map_t map = parse_map_from_string(content);
  load_navmesh(filename, map.navmesh);
  map.lightmap = load_lightmap_sidecar(filename, compute_map_content_hash(map));

  return map;
}

std::string serialize_map_to_string(const map_t &map)
{
  std::vector<map_block_out_t> blocks;
  blocks.reserve(map.object_count() + 1);

  // Worldspawn — the map's own properties, not an entity in either list.
  {
    map_block_out_t worldspawn;
    worldspawn.keyword = "entity";
    worldspawn.properties.emplace_back("classname", "worldspawn");
    worldspawn.properties.emplace_back("name", map.name);
    blocks.push_back(std::move(worldspawn));
  }

  // The map's own cvar settings, split back into the name/value pairs the block
  // format stores. A line with nothing but a name writes an empty value.
  if (!map.attached_cvars.empty())
  {
    map_block_out_t block;
    block.keyword = "cvars";
    for (const std::string &line : map.attached_cvars)
    {
      const cvar_line_t split = split_cvar_line(line);
      if (split.name.empty())
        continue;
      block.properties.emplace_back(split.name, split.value);
    }
    if (!block.properties.empty())
      blocks.push_back(std::move(block));
  }

  // The material table, minus anything no face names any more. Dropping happens
  // HERE and only here: an index is only meaningful against the table it was
  // minted from, so compacting mid-session would invalidate every undo snapshot
  // holding one.
  const material_remap_t material_remap = build_material_remap(map);
  if (material_remap.kept.size() > 1 || !material_remap.kept[0].empty())
  {
    map_block_out_t block;
    block.keyword = "materials";
    for (size_t i = 0; i < material_remap.kept.size(); ++i)
      block.properties.emplace_back(std::to_string(i), material_remap.kept[i]);
    blocks.push_back(std::move(block));
  }

  // Geometry first, so a level's structure reads before its props.
  for (const map_geometry_t &entry : map.geometry)
  {
    map_block_out_t block;
    block.properties.emplace_back("_uid", std::to_string(entry.uid));
    serialize_geometry(entry.value, material_remap.old_to_new, block.keyword,
                       block.properties, block.children);
    blocks.push_back(std::move(block));
  }

  // Entities, from the generated tables: every @Saveable leaf, in DECLARATION
  // order. That order is the point — the std::map this replaced sorted keys
  // alphabetically, so a field's position in the file had nothing to do with
  // where it sits in entities.def, and reordering the .def produced no diff
  // while renaming a field reshuffled the whole block.
  for (const map_entity_t &entry : map.entities)
  {
    if (!entry.entity)
      continue;

    const entities::Entity *entity = entry.entity.get();
    if (entity->type == entities::entity_type::Invalid)
    {
      log_error("map save: entity uid {} carries an invalid type tag — dropped", entry.uid);
      continue;
    }

    map_block_out_t block;
    block.keyword = "entity";
    block.properties.emplace_back("classname", entities::classname_of(entity));
    block.properties.emplace_back("_uid", std::to_string(entry.uid));

    const uint8_t *base = reinterpret_cast<const uint8_t *>(entity);

    for (const entities::leaf_field_t &leaf :
         entities::collect_leaf_fields(entity->type, entities::FIELD_FLAG_SAVEABLE))
    {
      std::string value;
      if (!field_to_text(base + leaf.offset, *leaf.info, value))
      {
        log_error("map save: entity uid {} field {}.{} could not be written as text — "
                  "the key is omitted and the value will be lost on the next load",
                  entry.uid, entities::classname_of(entity), leaf.name);
        continue;
      }
      block.properties.emplace_back(leaf.name, value);
    }

    blocks.push_back(std::move(block));
  }

  return serialize_map_blocks(blocks);
}

bool save_map(const std::string &filename, const map_t &map)
{
  std::string content = serialize_map_to_string(map);
  std::ofstream out(filename);
  if (!out.is_open())
    return false;
  out << content;
  out.close();

  if (map.navmesh.valid())
    save_navmesh(filename, map.navmesh);

  // The .lightmap sidecar is deliberately NOT written here, unlike the navmesh.
  // It carries the content hash of the map it was baked from, and re-writing it
  // on every save would stamp the CURRENT hash onto stale pixels -- silencing
  // the one warning that tells an author to rebake. The bake writes it, and
  // only the bake.

  return true;
}

std::string resolve_map_path(const std::string &maps_dir,
                             const std::string &identifier)
{
  // Only the basename of the identifier matters — the directory part (if any) is
  // replaced by maps_dir. So a server's maps-relative wire id ("new_map.source")
  // and a last_map.txt entry (an absolute path) both resolve the same way, into
  // whatever maps_dir this process was told to use.
  std::filesystem::path leaf = std::filesystem::path(identifier).filename();
  return (std::filesystem::path(maps_dir) / leaf).generic_string();
}

bool save_navmesh_sidecar(const std::string &map_path, const navmesh_t &nav)
{
  if (!nav.valid())
    return false;
  save_navmesh(map_path, nav);
  return true;
}

uint32_t compute_map_content_hash(const map_t &map)
{
  const std::string content = serialize_map_to_string(map);

  uint32_t hash = 2166136261u; // FNV-1a offset basis
  for (unsigned char byte : content)
  {
    hash ^= byte;
    hash *= 16777619u; // FNV prime
  }
  return hash;
}

} // namespace shared
