#include "entity_icons.hpp"

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/lighting.hpp"
#include "../../shared/map.hpp"
#include "entity_editor_traits.hpp"

#include <imgui.h>

namespace client
{

namespace
{

// -- The glyphs --------------------------------------------------------
//
// All three are authored in one 680x460 y-down canvas centred on (340, 230), so
// they read at the same weight beside each other. Edit them in that space; the
// normalization is icon_shape_t's job.

constexpr linalg::vec2 BULB_GLASS[] = {
    {340, 60}, {392, 72}, {434, 104}, {458, 150}, {458, 206}, {434, 254},
    {406, 290}, {390, 340}, {290, 340}, {274, 290}, {246, 254}, {222, 206},
    {222, 150}, {246, 104}, {288, 72},
};
constexpr linalg::vec2 BULB_BASE_1[] = {{290, 340}, {390, 340}};
constexpr linalg::vec2 BULB_BASE_2[] = {{296, 358}, {384, 358}};
constexpr linalg::vec2 BULB_BASE_3[] = {{300, 376}, {380, 376}};
constexpr linalg::vec2 BULB_BASE_4[] = {{306, 394}, {374, 394}};
constexpr linalg::vec2 BULB_BASE_5[] = {{314, 412}, {366, 412}};
constexpr linalg::vec2 BULB_BASE_6[] = {{322, 428}, {358, 428}};
constexpr linalg::vec2 BULB_SUPPORT_LEFT[]  = {{314, 340}, {314, 214}};
constexpr linalg::vec2 BULB_SUPPORT_RIGHT[] = {{366, 340}, {366, 214}};
constexpr linalg::vec2 BULB_FILAMENT[] = {{314, 214}, {324, 196}, {334, 214},
                                          {344, 196}, {354, 214}, {366, 196}};
constexpr linalg::vec2 BULB_RAY_1[] = {{340, 24}, {340, 8}};
constexpr linalg::vec2 BULB_RAY_2[] = {{256, 46}, {246, 32}};
constexpr linalg::vec2 BULB_RAY_3[] = {{424, 46}, {434, 32}};
constexpr linalg::vec2 BULB_RAY_4[] = {{198, 108}, {182, 100}};
constexpr linalg::vec2 BULB_RAY_5[] = {{482, 108}, {498, 100}};
constexpr linalg::vec2 BULB_RAY_6[] = {{186, 184}, {170, 184}};
constexpr linalg::vec2 BULB_RAY_7[] = {{494, 184}, {510, 184}};

constexpr icon_polyline_t BULB_STROKES[] = {
    {BULB_GLASS, true},      {BULB_BASE_1},        {BULB_BASE_2},
    {BULB_BASE_3},           {BULB_BASE_4},        {BULB_BASE_5},
    {BULB_BASE_6},           {BULB_SUPPORT_LEFT},  {BULB_SUPPORT_RIGHT},
    {BULB_FILAMENT},         {BULB_RAY_1},         {BULB_RAY_2},
    {BULB_RAY_3},            {BULB_RAY_4},         {BULB_RAY_5},
    {BULB_RAY_6},            {BULB_RAY_7},
};

// A lamp housing over three thrown rays: what a spot light IS, minus the aim,
// which the world-space stub already carries and a flat icon cannot.
constexpr linalg::vec2 SPOT_STEM[]    = {{340, 30}, {340, 80}};
constexpr linalg::vec2 SPOT_HOUSING[] = {{250, 80}, {430, 80}, {490, 210}, {190, 210}};
constexpr linalg::vec2 SPOT_RAY_LEFT[]   = {{230, 250}, {170, 410}};
constexpr linalg::vec2 SPOT_RAY_MIDDLE[] = {{340, 255}, {340, 420}};
constexpr linalg::vec2 SPOT_RAY_RIGHT[]  = {{450, 250}, {510, 410}};

constexpr icon_polyline_t SPOT_STROKES[] = {
    {SPOT_STEM},       {SPOT_HOUSING, true}, {SPOT_RAY_LEFT},
    {SPOT_RAY_MIDDLE}, {SPOT_RAY_RIGHT},
};

constexpr linalg::vec2 SUN_DISC[] = {
    {430.0f, 230.0f}, {423.2f, 264.4f}, {403.6f, 293.6f}, {374.4f, 313.2f},
    {340.0f, 320.0f}, {305.6f, 313.2f}, {276.4f, 293.6f}, {256.8f, 264.4f},
    {250.0f, 230.0f}, {256.8f, 195.6f}, {276.4f, 166.4f}, {305.6f, 146.8f},
    {340.0f, 140.0f}, {374.4f, 146.8f}, {403.6f, 166.4f}, {423.2f, 195.6f},
};
constexpr linalg::vec2 SUN_RAY_E[]  = {{460.0f, 230.0f}, {540.0f, 230.0f}};
constexpr linalg::vec2 SUN_RAY_SE[] = {{424.9f, 314.9f}, {481.4f, 371.4f}};
constexpr linalg::vec2 SUN_RAY_S[]  = {{340.0f, 350.0f}, {340.0f, 430.0f}};
constexpr linalg::vec2 SUN_RAY_SW[] = {{255.1f, 314.9f}, {198.6f, 371.4f}};
constexpr linalg::vec2 SUN_RAY_W[]  = {{220.0f, 230.0f}, {140.0f, 230.0f}};
constexpr linalg::vec2 SUN_RAY_NW[] = {{255.1f, 145.1f}, {198.6f, 88.6f}};
constexpr linalg::vec2 SUN_RAY_N[]  = {{340.0f, 110.0f}, {340.0f, 30.0f}};
constexpr linalg::vec2 SUN_RAY_NE[] = {{424.9f, 145.1f}, {481.4f, 88.6f}};

constexpr icon_polyline_t SUN_STROKES[] = {
    {SUN_DISC, true}, {SUN_RAY_E},  {SUN_RAY_SE}, {SUN_RAY_S},  {SUN_RAY_SW},
    {SUN_RAY_W},      {SUN_RAY_NW}, {SUN_RAY_N},  {SUN_RAY_NE},
};

// -- The pass ----------------------------------------------------------

constexpr float ICON_PIXEL_SIZE = 44.f;
constexpr float ICON_THICKNESS  = 1.75f;

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

void stroke_icon(ImDrawList* draw_list, const icon_shape_t& icon, linalg::vec2 center,
                 float pixel_size, color_t color)
{
  const float scale = pixel_size / icon.canvas_height;
  const ImU32 packed = to_abgr(color);

  for (const icon_polyline_t& polyline : icon.polylines)
  {
    for (const linalg::vec2& point : polyline.points)
    {
      // y is NOT flipped: the canvas is authored y-down and the screen is
      // y-down, so the one place the two could disagree is the one place they
      // do not.
      draw_list->PathLineTo(ImVec2(center.x + (point.x - icon.canvas_center.x) * scale,
                                   center.y + (point.y - icon.canvas_center.y) * scale));
    }
    draw_list->PathStroke(packed, polyline.closed ? ImDrawFlags_Closed : 0, ICON_THICKNESS);
  }
}

} // namespace

const icon_shape_t POINT_LIGHT_ICON{BULB_STROKES, {340.f, 230.f}, 420.f};
const icon_shape_t SPOT_LIGHT_ICON{SPOT_STROKES, {340.f, 230.f}, 400.f};
const icon_shape_t DIRECTIONAL_LIGHT_ICON{SUN_STROKES, {340.f, 230.f}, 400.f};

void draw_entity_icons(const shared::map_t& map, const viewport_state_t& view)
{
  ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

  for (const shared::map_entity_t& entry : map.entities)
  {
    if (!entry.entity)
      continue;

    const entity_icon_t icon = get_entity_icon(entry.entity.get());
    if (!icon.shape)
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

    stroke_icon(draw_list, *icon.shape, *screen, ICON_PIXEL_SIZE,
                icon_color_for(entry.entity.get(), icon.fallback_color));
  }
}

} // namespace client
