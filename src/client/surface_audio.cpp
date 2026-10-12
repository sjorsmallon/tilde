#include "surface_audio.hpp"

#include "../shared/array.hpp"
#include "../shared/assets/asset.hpp"
#include "../shared/span.hpp"
#include "client_context.hpp"

#include <algorithm>

namespace client
{

namespace
{

// No step is on disk for any kind yet: every row resolves to Missing, which the
// audio system logs once. Drop step_<kind>_N.wav into resources/sounds/ and name
// it here. A kind with no recording of its own names Default's files by literal.
constexpr assets::asset_name_t DEFAULT_STEPS[] = {assets::NO_ASSET_NAME};

struct surface_sounds_t
{
  shared::Surface_Kind             kind;
  Span<const assets::asset_name_t> step;
  assets::asset_name_t             land;
};

constexpr Enum_Array<shared::Surface_Kind, surface_sounds_t> SURFACE_SOUNDS = {{
    {shared::Surface_Kind::Default, DEFAULT_STEPS, "player_land_new"},
    {shared::Surface_Kind::Stone, DEFAULT_STEPS, "player_land_new"},
    {shared::Surface_Kind::Metal, DEFAULT_STEPS, "player_land_new"},
    {shared::Surface_Kind::Wood, DEFAULT_STEPS, "player_land_new"},
    {shared::Surface_Kind::Glass, DEFAULT_STEPS, "player_land_new"},
    {shared::Surface_Kind::Fabric, DEFAULT_STEPS, "player_land_new"},
    {shared::Surface_Kind::Plastic, DEFAULT_STEPS, "player_land_new"},
}};
static_assert(rows_in_enum_order<&surface_sounds_t::kind>(SURFACE_SOUNDS),
              "SURFACE_SOUNDS must list every Surface_Kind in declaration order");

} // namespace

shared::Surface_Kind surface_kind_for_material(const client_context_t& context,
                                               uint16_t material_index)
{
  const std::vector<std::string>& materials = context.world.session.materials;
  if (material_index == 0 || material_index >= materials.size())
    return shared::Surface_Kind::Default;
  const std::string& path = materials[material_index];
  if (path.empty())
    return shared::Surface_Kind::Default;
  return assets::get(assets::load_material_asset(path.c_str()))->surface;
}

assets::sound_asset step_sound_for(shared::Surface_Kind kind, uint32_t step_index)
{
  const Span<const assets::asset_name_t> variants = SURFACE_SOUNDS[kind].step;
  return assets::sound_id(variants[step_index % variants.count]);
}

assets::sound_asset land_sound_for(shared::Surface_Kind kind)
{
  return assets::sound_id(SURFACE_SOUNDS[kind].land);
}

float step_volume_for(float step_speed, float run_speed)
{
  if (run_speed <= 0.f)
    return 1.f;
  return std::clamp(step_speed / run_speed, 0.3f, 1.f);
}

float land_volume_for(float land_impact_speed)
{
  return std::clamp(land_impact_speed / 600.f, 0.3f, 1.f);
}

} // namespace client
