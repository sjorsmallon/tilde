#pragma once

#include "entities/entity_reflection.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "map_geometry.hpp"
#include "navmesh.hpp"
#include "shapes.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace shared
{
//@NOTE(SJM): Conceptually, the map is a collection of entities and of geometry. all of them are keyed by entity_uid_it,
// sorry for the misnomer. This means that everything in the editors mostly deals with these uids to retrieve the correct object.
// it simplifies a lot of interaction with the map from the editor side.
// this is _not_ the artifact that is played on. this is the "map" that is used to build the artifact.



struct map_entity_t
{
  entity_uid_t uid;
  std::shared_ptr<entities::Entity> entity;
};

// One piece of map-owned geometry (box / static mesh / displacement). A plain
// value — see map_geometry.hpp for why geometry is not an entity.
struct map_geometry_t
{
  entity_uid_t uid = 0;
  geometry_value_t value;
};

struct map_t
{
  std::string name;

  // ONE uid space across both lists below. Everything editor-side — selection,
  // hover, undo/redo, picking — is uid-keyed and does not care which regime
  // backs an object, so the uids must not collide.
  entity_uid_t next_uid = 1;

  std::vector<map_entity_t> entities;
  std::vector<map_geometry_t> geometry;
  std::vector<std::string> attached_cvars;
  

  // Populated by bake_map(). Loaded from a .navmesh sidecar alongside the map file.
  navmesh_t navmesh;

  // Add entity with auto-assigned uid
  entity_uid_t add_entity(std::shared_ptr<entities::Entity> ent)
  {
    entity_uid_t uid = next_uid++;
    entities.push_back({uid, std::move(ent)});
    return uid;
  }

  // Add entity with a specific uid (for undo/redo restore)
  void add_entity_with_uid(entity_uid_t uid, std::shared_ptr<entities::Entity> ent)
  {
    entities.push_back({uid, std::move(ent)});
    if (uid >= next_uid)
      next_uid = uid + 1;
  }

  // Remove entity by uid
  bool remove_entity(entity_uid_t uid)
  {
    auto it = std::find_if(entities.begin(), entities.end(),
                           [uid](const map_entity_t &e) { return e.uid == uid; });
    if (it == entities.end())
      return false;
    entities.erase(it);
    return true;
  }

  // Find entity by uid (linear scan)
  map_entity_t *find_by_uid(entity_uid_t uid)
  {
    for (auto &e : entities)
      if (e.uid == uid)
        return &e;
    return nullptr;
  }

  const map_entity_t *find_by_uid(entity_uid_t uid) const
  {
    for (const auto &e : entities)
      if (e.uid == uid)
        return &e;
    return nullptr;
  }

  // --- Geometry, mirroring the entity accessors above -----------------------

  entity_uid_t add_geometry(geometry_value_t value)
  {
    entity_uid_t uid = next_uid++;
    geometry.push_back({uid, std::move(value)});
    return uid;
  }

  void add_geometry_with_uid(entity_uid_t uid, geometry_value_t value)
  {
    geometry.push_back({uid, std::move(value)});
    if (uid >= next_uid)
      next_uid = uid + 1;
  }

  bool remove_geometry(entity_uid_t uid)
  {
    auto it = std::find_if(geometry.begin(), geometry.end(),
                           [uid](const map_geometry_t &g) { return g.uid == uid; });
    if (it == geometry.end())
      return false;
    geometry.erase(it);
    return true;
  }

  map_geometry_t *find_geometry_by_uid(entity_uid_t uid)
  {
    for (auto &g : geometry)
      if (g.uid == uid)
        return &g;
    return nullptr;
  }

  const map_geometry_t *find_geometry_by_uid(entity_uid_t uid) const
  {
    for (const auto &g : geometry)
      if (g.uid == uid)
        return &g;
    return nullptr;
  }

  // --- The uniform editable-object seam ------------------------------------
  //
  // Tools iterate and address "map objects" by uid and don't care which of the
  // two lists backs one. This is all the uniformity editing ever needed:
  // identity, transform, bounds (which is the hit-test, via the editor BVH),
  // and removal. Snapshot/restore needs no seam — an entity clones, a geometry
  // value copies. Uniform editing never actually required schemas.

  size_t object_count() const { return entities.size() + geometry.size(); }

  bool has_object(entity_uid_t uid) const
  {
    return find_by_uid(uid) != nullptr || find_geometry_by_uid(uid) != nullptr;
  }

  // Remove whichever list holds `uid`. False if no object has it.
  bool remove_object(entity_uid_t uid)
  {
    return remove_entity(uid) || remove_geometry(uid);
  }

  // Iterate every entity whose concrete type is exactly T.
  // Yields std::pair<entity_uid_t, T*> where the pointer is guaranteed non-null.
  //
  // Usage:
  //   for (auto [uid, emitter] : map.entities_of_type<entities::Particle_Emitter_Entity>())
  //     { ... }
  //
  // The type check compares the entity's runtime tag against T::static_type, a
  // constant the generator puts on every entity struct — one integer compare
  // per element, no RTTI walk. Because the comparison is exact, this matches
  // concrete T only. The entity hierarchy is closed and one level deep (every
  // type derives straight from Entity), so exact match is the desired behavior.
  //
  // The view is invalidated by anything that mutates `entities`, same as a raw
  // iterator.
  template <typename T>
  auto entities_of_type()
  {
    return entities
      | std::views::filter([](const map_entity_t &e) {
          return e.entity && e.entity->type == T::static_type;
        })
      | std::views::transform([](map_entity_t &e) {
          return std::pair<entity_uid_t, T *>{e.uid, static_cast<T *>(e.entity.get())};
        });
  }

  template <typename T>
  auto entities_of_type() const
  {
    return entities
      | std::views::filter([](const map_entity_t &e) {
          return e.entity && e.entity->type == T::static_type;
        })
      | std::views::transform([](const map_entity_t &e) {
          return std::pair<entity_uid_t, const T *>{e.uid, static_cast<const T *>(e.entity.get())};
        });
  }
};

// --- Canonical (pure, no-I/O) serialization ---
//
// serialize_map_to_string / parse_map_from_string are the canonical in-memory
// text form of a map's entities. They do NOT touch the filesystem and do NOT
// include the .navmesh sidecar (that's derived/baked data, handled separately by
// the file-level save_map/load_map). This canonical string is what the content
// hash is taken over and what the future compiled-package streaming embeds — so
// keep them pure and deterministic.

// Construct a map entity from a classname as it appears on disk.
//
// Honors the pre-cutover classnames ("player_start", "trigger_volume", ...) for
// the five types whose generated classname differs, so a map written before P5
// still loads. The first save rewrites the file with the generated name.
//
// Returns nullptr for a classname nothing recognises, which IS a data error --
// the caller must report it rather than skipping the entity quietly.
std::shared_ptr<entities::Entity> create_map_entity(const std::string &classname);

// Construct a fresh entity of a runtime-chosen `type`, wrapped with the
// deleter its result requires: entities have no virtual destructor, so a
// create_entity() pointer must be destroyed through destroy_entity to recover
// the concrete type, never through a bare `delete`. nullptr for Invalid.
std::shared_ptr<entities::Entity> make_entity(entities::entity_type type);

// make_entity(type) plus registering the result with the map in one step --
// the runtime-type counterpart to map_t::add_entity(existing_shared_ptr), for
// callers that don't already have an entity to hand over (placement by menu
// selection, "spawn one of these" commands). {0, nullptr} for Invalid.
std::pair<entity_uid_t, std::shared_ptr<entities::Entity>>
spawn_entity(map_t &map, entities::entity_type type);

// Serialize a map's entities to the canonical VMF-style text.
std::string serialize_map_to_string(const map_t &map);

// Parse canonical VMF-style text into a map_t (entities only; no navmesh).
// Returns true on success, false on failure.
bool parse_map_from_string(const std::string &content, map_t &out_map);

// Loads map from VMF-style text file (entities via parse_map_from_string, plus
// the .navmesh sidecar next to it). Returns true on success, false on failure.
// usage:
//   shared::map_t map;
//   if (shared::load_map("levels/start.map", map)) { ... }
bool load_map(const std::string &filename, map_t &out_map);

// Saves map to VMF-style text file (entities via serialize_map_to_string, plus
// the .navmesh sidecar). Returns true on success, false on failure.
bool save_map(const std::string &filename, const map_t &map);

// Resolve a map identifier (a bare name, or any path from which only the
// basename is used) to a loadable file inside `maps_dir`. This is how a
// maps-relative wire id (sent by the server) or a last_map.txt entry becomes an
// actual on-disk path — and crucially, it's per-side: a client pointed at a
// different maps_dir will naturally FAIL to find a map it has no local copy of,
// which is exactly the cache-miss that triggers streaming. Joins with '/';
// filename() copes with either path separator.
std::string resolve_map_path(const std::string &maps_dir,
                             const std::string &identifier);

// Computes an FNV-1a 32-bit hash of the map's CANONICAL serialization
// (serialize_map_to_string), NOT the raw on-disk bytes. Hashing the canonical
// form makes the hash independent of on-disk whitespace/formatting and — once
// compiled-package streaming lands — byte-for-byte identical to the streamed
// entity payload. Used to verify that client and server are running the same
// map: both sides load, serialize, and hash, so a match means identical entity
// data regardless of how each side's file happened to be formatted.
uint32_t compute_map_content_hash(const map_t &map);

// Saves only the .navmesh sidecar alongside the given map file path.
// Returns false if nav is not valid.
bool save_navmesh_sidecar(const std::string &map_path, const navmesh_t &nav);

// Compute world-space AABB bounds for an entity.
// Data-driven: uses mesh bounds if available, else entity-specific shape,
// else default 1x1x1 box at position.
aabb_bounds_t compute_entity_bounds(const entities::Entity *entity);

// Compute outward-facing collision planes for an entity's shape.
// AABB entities -> 6 planes, Wedge entities -> 5 planes (including slope),
// Static mesh / fallback -> 6 AABB planes from bounds.
std::vector<Plane> compute_entity_collision_planes(const entities::Entity *entity);

// Returns polygon vertices for each face, parallel to compute_entity_collision_planes().
std::vector<std::vector<linalg::vec3>> compute_entity_face_polygons(const entities::Entity *entity);

// --- Uniform per-uid accessors over both regimes -----------------------------
//
// The other half of the editable-object seam (the identity half lives on map_t).
// These are free functions rather than members because resolving a static mesh's
// bounds means touching the asset cache, which map.hpp has no business pulling in.

// World-space AABB of whichever object holds `uid`. Logs and returns a degenerate
// box at the origin if nothing does.
aabb_bounds_t compute_object_bounds(const map_t &map, entity_uid_t uid);

// Position of whichever object holds `uid`. False (and out untouched) if none does.
bool get_object_position(const map_t &map, entity_uid_t uid, linalg::vec3 &out_position);
bool set_object_position(map_t &map, entity_uid_t uid, const linalg::vec3 &position);

// The axis-aligned box an object is resized through — its own center and
// half-extents, not its derived bounds. False (outputs untouched, nothing
// written) for objects that have no editable box: a static mesh takes its size
// from its asset, a point entity has none. That "false" is what makes an object
// un-sculptable and turns the gizmo's reshape handles off, in one place instead
// of a type test per tool.
bool get_object_box(const map_t &map, entity_uid_t uid, linalg::vec3 &out_center,
                    linalg::vec3 &out_half_extents);
bool set_object_box(map_t &map, entity_uid_t uid, const linalg::vec3 &center,
                    const linalg::vec3 &half_extents);

// Every editable object's uid and world bounds, geometry first then entities.
// This is what "iterate the editable map objects" means for the tools: box
// select, hover preview and the picking BVH all want exactly (identity, bounds)
// and nothing else, so none of them has to walk two lists or branch on regime.
std::vector<std::pair<entity_uid_t, aabb_bounds_t>>
collect_object_bounds(const map_t &map);

} // namespace shared
