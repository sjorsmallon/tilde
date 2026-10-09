#include "asset_state.hpp"

#include "log.hpp"

#include <algorithm>
#include <string>

namespace assets
{

// The ONE piece of static storage in the asset system, and it is per module
// by design: each module points its own copy at the single launcher-owned
// state. game_shared is a static lib linked into the exe and both DLLs.
static asset_state_t* g_asset_state = nullptr;

void set_state(asset_state_t* state)
{
  if (state == nullptr)
  {
    log_error("assets: set_state(nullptr) -- the launcher owns the one asset state and it must "
              "outlive every module");
    return;
  }
  g_asset_state = state;
}

asset_state_t& state_for(const char* who)
{
  if (g_asset_state == nullptr)
    fatal_error("assets: {} called before assets::set_state() -- this module was never pointed "
                "at the launcher's asset state",
                who);
  return *g_asset_state;
}

namespace
{

void mix(uint32_t& hash, std::string_view bytes)
{
  for (const char byte : bytes)
  {
    hash ^= (uint8_t)byte;
    hash *= 16777619u;
  }
}

const asset_class_table_t& table_for(asset_class_t asset_class, const char* who)
{
  const asset_state_t& state = state_for(who);
  if (!state.tables_built)
    fatal_error("assets: {} called before assets::init() -- the ids are numbered there, so "
                "nothing resolves until it has run",
                who);
  return state.tables[asset_class];
}

std::optional<uint16_t> try_find_id_by_hash(const asset_class_table_t& table, uint64_t name_hash)
{
  const auto found = std::lower_bound(
      table.by_hash.begin(), table.by_hash.end(), name_hash,
      [](const hashed_asset_id_t& row, uint64_t hash) { return row.name_hash < hash; });
  if (found == table.by_hash.end() || found->name_hash != name_hash)
    return std::nullopt;
  return found->id;
}

uint16_t id_for_name(asset_class_t asset_class, asset_name_t name, const char* who)
{
  if (name.empty())
    return 0;

  const asset_class_table_t& table = table_for(asset_class, who);
  if (const std::optional<uint16_t> id = try_find_id_by_hash(table, name.hash))
    return *id;

  fatal_error("assets: no {} named '{}' -- the name is a literal in code, so either the file is "
              "not under resources/ or the literal is misspelled",
              to_string(asset_class), name.text);
}

template <typename Id_T> std::optional<Id_T> id_for_text(asset_class_t asset_class, std::string_view text)
{
  const asset_class_table_t& table = table_for(asset_class, "try_from_string");
  if (const std::optional<uint16_t> id = try_find_id_by_hash(table, hash_asset_name(text)))
    return (Id_T)*id;
  return std::nullopt;
}

template <typename Id_T> const char* name_for_id(asset_class_t asset_class, Id_T id)
{
  const Span<const asset_entry_t> entries = asset_class_entries(asset_class);
  if ((uint32_t)id >= entries.size())
    return "(no such asset)";
  return entries[(uint32_t)id].name.c_str();
}

} // namespace

void build_asset_tables(asset_state_t& state, const asset_classification_t& classification)
{
  if (state.tables_built)
    return;

  if (!classification.errors.empty())
  {
    for (const std::string& error : classification.errors)
      log_error("assets: {}", error);
    fatal_error("assets: the resource tree has {} problem(s), listed above", classification.errors.size());
  }

  for (uint32_t which = 0; which < ASSET_CLASS_COUNT; ++which)
  {
    asset_class_table_t& table = state.tables[(asset_class_t)which];
    table.entries.clear();
    table.by_hash.clear();
    table.entries.push_back({"Missing", "", hash_asset_name("Missing")});
  }

  for (const classified_asset_t& asset : classification.assets)
  {
    asset_class_table_t& table = state.tables[asset.asset_class];
    if (table.entries.size() >= UINT16_MAX)
      fatal_error("assets: more than {} {} entries; an id is sixteen bits", UINT16_MAX,
                  to_string(asset.asset_class));
    table.entries.push_back({asset.name, asset.path, hash_asset_name(asset.name)});
  }

  uint32_t hash = 2166136261u;
  for (uint32_t which = 0; which < ASSET_CLASS_COUNT; ++which)
  {
    const asset_class_t  asset_class = (asset_class_t)which;
    asset_class_table_t& table       = state.tables[asset_class];

    mix(hash, to_string(asset_class));
    for (uint16_t id = 0; id < table.entries.size(); ++id)
    {
      const asset_entry_t& entry = table.entries[id];
      mix(hash, entry.name);
      mix(hash, entry.path);
      table.by_hash.push_back({entry.name_hash, id});
    }

    std::sort(table.by_hash.begin(), table.by_hash.end(),
              [](const hashed_asset_id_t& left, const hashed_asset_id_t& right) {
                return left.name_hash < right.name_hash;
              });
    for (size_t index = 1; index < table.by_hash.size(); ++index)
    {
      if (table.by_hash[index - 1].name_hash != table.by_hash[index].name_hash)
        continue;
      fatal_error("assets: {} names '{}' and '{}' hash alike; rename one",
                  to_string(asset_class), table.entries[table.by_hash[index - 1].id].name,
                  table.entries[table.by_hash[index].id].name);
    }
  }

  state.table_hash   = hash;
  state.tables_built = true;

  for (uint32_t which = 0; which < ASSET_CLASS_COUNT; ++which)
    log_terminal("assets: {} {} id(s)", state.tables[(asset_class_t)which].entries.size() - 1,
                 to_string((asset_class_t)which));
  log_terminal("assets: table hash {:#010x}", hash);
}

void number_asset_ids_from_tree(asset_state_t& state, const char* root)
{
  const asset_tree_listing_t listing = list_asset_tree(root);
  asset_classification_t classification = classify_asset_paths(Span<const std::string>(listing.paths));
  classification.errors.insert(classification.errors.end(), listing.errors.begin(),
                               listing.errors.end());
  build_asset_tables(state, classification);
}

Span<const asset_entry_t> asset_class_entries(asset_class_t asset_class)
{
  return Span<const asset_entry_t>(table_for(asset_class, "asset_class_entries").entries);
}

Span<const asset_entry_t> asset_class_entries(int32_t asset_class_id)
{
  if (asset_class_id < 0 || asset_class_id >= (int32_t)ASSET_CLASS_COUNT)
    fatal_error("assets: asset class id {} names no class; the generated field table is the only "
                "source of one",
                asset_class_id);
  return asset_class_entries((asset_class_t)asset_class_id);
}

uint32_t asset_count(asset_class_t asset_class)
{
  return asset_class_entries(asset_class).size();
}

uint32_t asset_table_hash()
{
  const asset_state_t& state = state_for("asset_table_hash");
  if (!state.tables_built)
    fatal_error("assets: asset_table_hash() before assets::init()");
  return state.table_hash;
}

mesh_asset      mesh_id(asset_name_t name)         { return (mesh_asset)id_for_name(asset_class_t::mesh_asset, name, "mesh_id"); }
texture_asset   texture_id(asset_name_t name)      { return (texture_asset)id_for_name(asset_class_t::texture_asset, name, "texture_id"); }
sound_asset     sound_id(asset_name_t name)        { return (sound_asset)id_for_name(asset_class_t::sound_asset, name, "sound_id"); }
animation_asset animation_id(asset_name_t name)    { return (animation_asset)id_for_name(asset_class_t::animation_asset, name, "animation_id"); }
hitbox_rig      hitbox_rig_id(asset_name_t name)   { return (hitbox_rig)id_for_name(asset_class_t::hitbox_rig, name, "hitbox_rig_id"); }
font_asset      font_id(asset_name_t name)         { return (font_asset)id_for_name(asset_class_t::font_asset, name, "font_id"); }
pbr_material    pbr_material_id(asset_name_t name) { return (pbr_material)id_for_name(asset_class_t::pbr_material, name, "pbr_material_id"); }
cubemap_asset   cubemap_id(asset_name_t name)      { return (cubemap_asset)id_for_name(asset_class_t::cubemap_asset, name, "cubemap_id"); }

template <> std::optional<mesh_asset>      try_from_string<mesh_asset>(std::string_view text)      { return id_for_text<mesh_asset>(asset_class_t::mesh_asset, text); }
template <> std::optional<texture_asset>   try_from_string<texture_asset>(std::string_view text)   { return id_for_text<texture_asset>(asset_class_t::texture_asset, text); }
template <> std::optional<sound_asset>     try_from_string<sound_asset>(std::string_view text)     { return id_for_text<sound_asset>(asset_class_t::sound_asset, text); }
template <> std::optional<animation_asset> try_from_string<animation_asset>(std::string_view text) { return id_for_text<animation_asset>(asset_class_t::animation_asset, text); }
template <> std::optional<hitbox_rig>      try_from_string<hitbox_rig>(std::string_view text)      { return id_for_text<hitbox_rig>(asset_class_t::hitbox_rig, text); }
template <> std::optional<font_asset>      try_from_string<font_asset>(std::string_view text)      { return id_for_text<font_asset>(asset_class_t::font_asset, text); }
template <> std::optional<pbr_material>    try_from_string<pbr_material>(std::string_view text)    { return id_for_text<pbr_material>(asset_class_t::pbr_material, text); }
template <> std::optional<cubemap_asset>   try_from_string<cubemap_asset>(std::string_view text)   { return id_for_text<cubemap_asset>(asset_class_t::cubemap_asset, text); }

const char* to_string(mesh_asset id)      { return name_for_id(asset_class_t::mesh_asset, id); }
const char* to_string(texture_asset id)   { return name_for_id(asset_class_t::texture_asset, id); }
const char* to_string(sound_asset id)     { return name_for_id(asset_class_t::sound_asset, id); }
const char* to_string(animation_asset id) { return name_for_id(asset_class_t::animation_asset, id); }
const char* to_string(hitbox_rig id)      { return name_for_id(asset_class_t::hitbox_rig, id); }
const char* to_string(font_asset id)      { return name_for_id(asset_class_t::font_asset, id); }
const char* to_string(pbr_material id)    { return name_for_id(asset_class_t::pbr_material, id); }
const char* to_string(cubemap_asset id)   { return name_for_id(asset_class_t::cubemap_asset, id); }

} // namespace assets
