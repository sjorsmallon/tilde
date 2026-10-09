#pragma once

// The whole mutable state of the asset system. ONE per process, owned by the
// launcher; every module holds a pointer (set_state). See the ownership note
// in asset_types.hpp for what a per-module copy cost.
//
// Its own header, after asset_types.hpp, because two of the value types are
// the domain's (animation.hpp, hitbox_rig.hpp) and hitbox_rig.hpp includes
// asset_types.hpp itself.

#include "animation.hpp"
#include "asset_id.hpp"
#include "asset_types.hpp"
#include "asset_walk.hpp"
#include "hitbox_rig.hpp"

#include <vector>

namespace assets
{

struct hashed_asset_id_t
{
  uint64_t name_hash;
  uint16_t id;
};

// One class's rows in id order, plus the same ids sorted by name hash, which
// is what a name resolves through: a binary search over integers, never a
// string compare.
struct asset_class_table_t
{
  std::vector<asset_entry_t>     entries;
  std::vector<hashed_asset_id_t> by_hash;
};

struct asset_state_t
{
  Enum_Array<asset_class_t, asset_class_table_t> tables;
  bool                                           tables_built = false;
  uint32_t                                       table_hash   = 0;

  Asset_Pool<mesh_asset_t>                     mesh_asset_pool;
  std::vector<asset_handle_t<mesh_asset_t>>    mesh_asset_handles;
  Asset_Pool<texture_asset_t>                  texture_asset_pool;
  std::vector<asset_handle_t<texture_asset_t>> texture_asset_handles;
  Asset_Pool<sound_asset_t>                    sound_asset_pool;
  std::vector<asset_handle_t<sound_asset_t>>   sound_asset_handles;
  Asset_Pool<animation_asset_t>                  animation_asset_pool;
  std::vector<asset_handle_t<animation_asset_t>> animation_asset_handles;
  Asset_Pool<hitbox_rig_t>                     hitbox_rig_pool;
  std::vector<asset_handle_t<hitbox_rig_t>>    hitbox_rig_handles;
  Asset_Pool<font_asset_t>                     font_asset_pool;
  std::vector<asset_handle_t<font_asset_t>>    font_asset_handles;
  Asset_Pool<pbr_material_asset_t>                  pbr_material_pool;
  std::vector<asset_handle_t<pbr_material_asset_t>> pbr_material_handles;
  Asset_Pool<cubemap_asset_t>                  cubemap_asset_pool;
  std::vector<asset_handle_t<cubemap_asset_t>> cubemap_asset_handles;

  bool registered = false;

  asset_source_t          source;
  path_referenced_pools_t path_referenced;
};

// Point THIS MODULE's accessors at the launcher's state. Every module that
// resolves an asset calls it exactly once: the exe for itself, client::init
// and server::init for their DLLs. Null is an error, not a reset.
void set_state(asset_state_t* state);

// This module's pointer to the launcher's state. Fatal if unset: a module that
// was never pointed at the state would resolve every asset to nothing.
[[nodiscard]] asset_state_t& state_for(const char* who);

// Number the ids from a classification of the tree (asset_walk.hpp): Missing
// at 0, then every entry in (class, path) order. Dies naming every error the
// walk reported and every pair of names that collide on a hash. Idempotent.
void build_asset_tables(asset_state_t& state, const asset_classification_t& classification);

// The loose-tree spelling of the above: walk `root` and number from it. What
// assets::init() does in a loose build, and what a test process that builds
// entities calls after set_state, since an entity's constructor resolves its
// asset defaults by name.
void number_asset_ids_from_tree(asset_state_t& state, const char* root);

} // namespace assets
