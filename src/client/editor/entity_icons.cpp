#include "entity_icons.hpp"

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/lighting.hpp"
#include "../../shared/map.hpp"
#include "../renderer.hpp"
#include "entity_editor_traits.hpp"

#include <imgui.h>

#include <algorithm>

namespace client
{

namespace
{

constexpr float ICON_PIXEL_SIZE = 44.f;

// A switched-off light is drawn as one, because the alternative is a lamp that
// looks lit in the editor and is black in the game -- the inspector already says
// "Switched OFF" and this is the same fact where you are looking.
constexpr color_t SWITCHED_OFF_COLOR{120, 120, 130, 200};

// The tint is the light's OWN colour, which is the reason an icon beats a cross:
// a wall of yellow markers says where the lights are, and this says what they
// are set to. Entities with no Light component fall back to the gizmo colour the
// type already declares.
color_t icon_color_for(const entities::Entity* entity, color_t fallback)
{
  const entities::Light* light = entities::get_component<entities::Light>(entity);
  if (!light)
    return fallback;
  if (!shared::light_is_switched_on(*entity))
    return SWITCHED_OFF_COLOR;

  // Normalized, not raw: intensity rides in `intensity`, but an authored colour
  // of {4, 4, 1} is legal and would clip to white here, losing exactly the
  // information the tint exists to carry.
  const float brightest =
      std::max({light->color.x, light->color.y, light->color.z, 0.0001f});
  return color_from_vec3(light->color * (1.f / brightest));
}

void draw_icon(ImDrawList* draw_list, assets::texture_asset icon, linalg::vec2 center,
               float pixel_size, color_t color)
{
  // Uploaded on the first ask and cached from then on, like every other texture
  // that arrives as an asset handle. Null is an upload that failed, which
  // register_texture has already said out loud.
  ImTextureID texture = (ImTextureID)renderer::imgui_texture_id(assets::get_texture(icon));
  if (!texture)
    return;

  const float half = pixel_size * 0.5f;
  draw_list->AddImage(texture, ImVec2(center.x - half, center.y - half),
                      ImVec2(center.x + half, center.y + half), ImVec2(0.f, 0.f),
                      ImVec2(1.f, 1.f), to_abgr(color));
}

} // namespace

void draw_entity_icons(const shared::map_t& map, const viewport_state_t& view,
                       Span<const shared::entity_uid_t> hidden)
{
  ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

  for (const shared::map_entity_t& entry : map.entities)
  {
    if (!entry.entity)
      continue;

    if (std::find(hidden.begin(), hidden.end(), entry.uid) != hidden.end())
      continue;

    const entity_icon_t icon = get_entity_icon(entry.entity.get());
    if (!icon.texture)
      continue;

    // Behind the camera has no screen position at all, which is the case a
    // projection that forgets the sign draws mirrored into the visible half.
    const std::optional<linalg::vec2> screen =
        try_project_to_screen(view, entry.entity->position);
    if (!screen)
      continue;

    // Cull by the icon's own extent rather than by the point, or a glyph
    // straddling the edge pops out whole as its centre crosses.
    const float margin = ICON_PIXEL_SIZE;
    if (screen->x < -margin || screen->y < -margin ||
        screen->x > view.display_size.x + margin ||
        screen->y > view.display_size.y + margin)
      continue;

    draw_icon(draw_list, *icon.texture, *screen, ICON_PIXEL_SIZE,
              icon_color_for(entry.entity.get(), icon.fallback_color));
  }
}

} // namespace client
