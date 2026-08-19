#pragma once

// A screen: the retained node tree, the tweens over it, and the focused node.
// ONE type because all three are addressed by the same ui_node_id_t, so held
// apart a rebuild leaves the ids naming whatever now occupies that index.
//
// Retained at all because focus and animation need identity across frames.
// Every property has one owner: AUTHORED by the build, BOUND (rewritten each
// frame from the game, the screen size or the focus), or ANIMATED. See
// ui_def.md.

#include "../../shared/color.hpp"
#include "../../shared/linalg.hpp"
#include "../renderer.hpp"
#include "font.hpp"
#include "layout.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace client::ui
{

using ui_node_id_t = uint16_t;

// Also the node cap: a screen cannot hold an id that would collide with "none".
inline constexpr ui_node_id_t UI_INVALID_NODE_ID = 0xffff;

// --- Content -----------------------------------------------------------------

// A solid fill. It carries no members because the node's `tint` already says
// what colour it is -- a struct with nothing in it is still worth having, since
// it is what distinguishes "a filled rect" from "an empty layout node".
struct ui_solid_content_t
{
};

// The `TextLayout` object that is deliberately NOT a builder: the node already
// owns these fields, so an object wrapping them would own nothing, and layout
// stays a pure function of them (draw_text_aligned in font.hpp).
struct ui_text_content_t
{
  font_size_t  size  = font_size_t::medium;
  std::string  text;
  text_align_t align = text_align_t::left;
};

struct ui_image_content_t
{
  renderer::texture_handle_t texture;
  linalg::vec2               uv_min = {0.0f, 0.0f};
  linalg::vec2               uv_max = {1.0f, 1.0f};
};

using ui_content_t =
    std::variant<std::monostate, ui_solid_content_t, ui_text_content_t, ui_image_content_t>;

// --- The node ----------------------------------------------------------------

struct ui_node_t
{
  // Structure. Authored by the build.
  ui_node_id_t parent       = UI_INVALID_NODE_ID;
  ui_node_id_t first_child  = UI_INVALID_NODE_ID;
  ui_node_id_t next_sibling = UI_INVALID_NODE_ID;

  // Layout, in PARENT space -- except the root's, which is in framebuffer
  // pixels. Bound, not authored: the screen's layout pass rewrites every rect
  // from the live screen size, which is the whole window-resize story.
  ui_rect_t rect;

  // Whether navigation and hit-testing can land here. Nesting one focusable
  // inside another is a design error, not a supported case.
  bool focusable = false;

  // Animatable presentation, both INHERITED down the walk. `offset` is added to
  // `rect` and is deliberately a separate field from it: the layout pass
  // rewrites rects every frame, so an animation that drove `rect` would be
  // stomped, where one driving `offset` composes with it. Animating the root's
  // offset slides an entire screen, which is the whole reason no matrix is
  // needed here.
  float        opacity = 1.0f;
  linalg::vec2 offset  = {0.0f, 0.0f};

  // Authored or bound colour. Final alpha is this modulated by the inherited
  // opacity, so the two never fight over one byte.
  color_t tint = colors::white;

  ui_content_t content;
};

// --- Animation ---------------------------------------------------------------

// The animatable properties, kept to the ones with no other owner. `tint` is
// deliberately absent: colour is authored or bound, and `opacity` already covers
// every fade, so animating both would be two writers on one alpha byte.
enum class ui_property_t : uint8_t
{
  opacity,
  offset_x,
  offset_y
};

enum class ease_t : uint8_t
{
  linear,
  in_cubic,
  out_cubic,
  in_out_cubic
};

struct ui_animation_t
{
  ui_node_id_t  node     = UI_INVALID_NODE_ID;
  ui_property_t property = ui_property_t::opacity;
  float         from     = 0.0f;
  float         to       = 0.0f;
  float         duration = 0.0f;
  float         elapsed  = 0.0f;
  ease_t        easing   = ease_t::linear;
};

// --- The screen --------------------------------------------------------------

struct ui_screen_t
{
  std::vector<ui_node_t>      nodes;
  std::vector<ui_animation_t> animations;

  // The node a NON-POSITIONAL activate would hit. Hover is deliberately not
  // stored beside it: a positional input resolves its own target with hit_test()
  // as it arrives. Whether pointer movement also writes this is a per-screen
  // policy, not part of what focus means.
  ui_node_id_t focused_node = UI_INVALID_NODE_ID;

  // Set by the first add_node; a second root is a build bug and fatal_errors.
  ui_node_id_t root = UI_INVALID_NODE_ID;

  [[nodiscard]] ui_node_t       &operator[](ui_node_id_t id) { return nodes[id]; }
  [[nodiscard]] const ui_node_t &operator[](ui_node_id_t id) const { return nodes[id]; }
};

// --- Building ----------------------------------------------------------------

// Append a node under `parent`, or as the root when `parent` is
// UI_INVALID_NODE_ID. Children keep insertion order, which is also draw order.
//
// A screen is BUILT ONCE by a function that returns it as a value and is
// replaced wholesale, never edited structurally -- which is what keeps every id
// in `animations` and `focused_node` in range with no check anywhere.
//
// Not try_: a bad parent, a second root or a 65535th node is a caller bug with
// no recovery, so all three fatal_error.
ui_node_id_t add_node(ui_screen_t &screen, ui_node_id_t parent, ui_rect_t rect);

// --- Resolution --------------------------------------------------------------

// A node's absolute rect and effective opacity, accumulated up the parent chain.
//
// This is the ONE definition of where a node ends up, used by the draw walk, by
// hit-testing and by navigation alike. Accumulating downward in the walk would
// be marginally cheaper and would give three call sites three chances to
// disagree about what "resolved" means; at menu depths the difference is noise.
struct resolved_node_t
{
  ui_rect_t rect;
  float     opacity = 1.0f;
};

[[nodiscard]] resolved_node_t resolve_node(const ui_screen_t &screen, ui_node_id_t id);

// Depth from the root, 0 for the root itself. UI_INVALID_NODE_ID gives 0.
[[nodiscard]] uint32_t node_depth(const ui_screen_t &screen, ui_node_id_t id);

// --- Drawing -----------------------------------------------------------------

// Emit the whole screen, parents before children. A PURE FUNCTION OF THE SCREEN
// -- no input, no game state, no side effects, nothing read that a test cannot
// hand it. An empty screen emits nothing.
void draw_screen(renderer::ui_draw_list_t &list, const ui_screen_t &screen, const ui_font_t &font);

// --- Animating ---------------------------------------------------------------

// Read/write one property by handle. Exhaustive over the enum, so adding a
// property is a compile error here rather than a silently ignored animation.
[[nodiscard]] float property_value(const ui_node_t &node, ui_property_t property);
void                set_property_value(ui_node_t &node, ui_property_t property, float value);

[[nodiscard]] float apply_ease(ease_t easing, float t);

// Returned by animate(). Every setter mutates the entry that animate() already
// appended and returns *this, so a chain reads as written and an omitted term
// simply keeps its default -- there is no build() to forget.
class ui_animation_builder_t
{
public:
  ui_animation_builder_t(ui_screen_t &screen, uint32_t index) : screen_(&screen), index_(index) {}

  ui_animation_builder_t &from(float value)
  {
    entry().from = value;
    return *this;
  }
  ui_animation_builder_t &to(float value)
  {
    entry().to = value;
    return *this;
  }
  ui_animation_builder_t &duration(float seconds)
  {
    entry().duration = seconds;
    return *this;
  }
  ui_animation_builder_t &ease(ease_t easing)
  {
    entry().easing = easing;
    return *this;
  }

private:
  [[nodiscard]] ui_animation_t &entry() { return screen_->animations[index_]; }

  ui_screen_t *screen_ = nullptr;
  uint32_t     index_  = 0;
};

// Start (or RESTART) an animation on one node property:
//
//   animate(screen, row, ui_property_t::opacity)
//       .from(0.0f).to(1.0f).duration(0.25f).ease(ease_t::out_cubic);
//
// An existing animation on the same (node, property) is REPLACED rather than
// appended to. Two tweens writing one float is not a blend, it is whichever
// happened to be advanced last -- and the case that makes it happen is mundane:
// a focus highlight re-targeted before its previous slide finished.
//
// `from` and `to` both default to the property's CURRENT value, so `.to(1.0f)`
// alone means "from wherever it is now to one", the common case for a highlight
// catching up.
[[nodiscard]] ui_animation_builder_t animate(ui_screen_t &screen, ui_node_id_t node,
                                             ui_property_t property);

// Advance every active animation and write its property. Finished ones write
// exactly `to` before retiring, so a fade lands on 1.0 and not 0.998 -- the same
// contract as debug_draw_list_t::retire(dt).
void advance_animations(ui_screen_t &screen, float delta_seconds);

} // namespace client::ui
