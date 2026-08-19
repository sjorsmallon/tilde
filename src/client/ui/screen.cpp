// The screen's structure, resolution, draw walk and tweens. GPU-free on purpose
// -- like ui_draw_list.cpp and font.cpp it compiles straight into ui_test, so
// the whole layout, inheritance and animation model is checked with no device,
// no swapchain and no window.

#include "screen.hpp"

#include "../../shared/log.hpp"

namespace client::ui
{

namespace
{

// Alpha is the product of the authored tint and every opacity above it, rounded
// once at the end. Doing it per level would quantize twice and make a stack of
// nested fades visibly darker than the same fade applied once.
[[nodiscard]] color_t modulated(color_t tint, float opacity)
{
  const float alpha = (float)tint.a * opacity;
  return with_alpha(tint, color_channel_from_float(alpha * (1.0f / 255.0f)));
}

void draw_subtree(renderer::ui_draw_list_t &list, const ui_screen_t &screen, const ui_font_t &font,
                  ui_node_id_t id)
{
  const ui_node_t      &node     = screen[id];
  const resolved_node_t resolved = resolve_node(screen, id);

  // Opacity multiplies down the tree, so a fully faded parent has a fully faded
  // subtree. Bailing here skips the children too, which is correct rather than
  // merely an optimization.
  if (resolved.opacity <= 0.0f)
    return;

  const color_t color = modulated(node.tint, resolved.opacity);

  // Exhaustive over the closed content set: adding a kind makes this a compile
  // error under -Werror=switch, which is the point of the variant being closed.
  if (std::holds_alternative<ui_solid_content_t>(node.content))
  {
    list.rect(resolved.rect.min, resolved.rect.max, color);
  }
  else if (const ui_text_content_t *text = std::get_if<ui_text_content_t>(&node.content))
  {
    draw_text_aligned(list, font, text->size, resolved.rect, text->align, text->text, color);
  }
  else if (const ui_image_content_t *image = std::get_if<ui_image_content_t>(&node.content))
  {
    list.quad(resolved.rect.min, resolved.rect.max, image->uv_min, image->uv_max, color,
              image->texture);
  }

  // Children after the parent: insertion order is draw order, back to front.
  for (ui_node_id_t child = node.first_child; child != UI_INVALID_NODE_ID;
       child              = screen[child].next_sibling)
    draw_subtree(list, screen, font, child);
}

} // namespace

ui_node_id_t add_node(ui_screen_t &screen, ui_node_id_t parent, ui_rect_t rect)
{
  if (screen.nodes.size() >= UI_INVALID_NODE_ID)
    fatal_error("[ui] add_node: screen is full ({} nodes) -- a screen this size is a bug",
                screen.nodes.size());

  if (parent != UI_INVALID_NODE_ID && parent >= screen.nodes.size())
    fatal_error("[ui] add_node: parent {} does not exist ({} nodes)", parent, screen.nodes.size());

  if (parent == UI_INVALID_NODE_ID && screen.root != UI_INVALID_NODE_ID)
    fatal_error("[ui] add_node: screen already has root {} -- a second root would never be drawn",
                screen.root);

  const ui_node_id_t id = (ui_node_id_t)screen.nodes.size();

  ui_node_t node;
  node.parent = parent;
  node.rect   = rect;
  screen.nodes.push_back(node);

  if (parent == UI_INVALID_NODE_ID)
  {
    screen.root = id;
  }
  else
  {
    // Append to the end of the sibling list so children draw in the order they
    // were added. A prepend would be O(1) too and would silently reverse every
    // screen's z-order.
    ui_node_id_t &head = screen.nodes[parent].first_child;
    if (head == UI_INVALID_NODE_ID)
    {
      head = id;
    }
    else
    {
      ui_node_id_t sibling = head;
      while (screen.nodes[sibling].next_sibling != UI_INVALID_NODE_ID)
        sibling = screen.nodes[sibling].next_sibling;
      screen.nodes[sibling].next_sibling = id;
    }
  }

  return id;
}

resolved_node_t resolve_node(const ui_screen_t &screen, ui_node_id_t id)
{
  resolved_node_t resolved;
  if (id == UI_INVALID_NODE_ID || id >= screen.nodes.size())
    return resolved;

  const ui_node_t &node = screen[id];

  // The node's own box, in its parent's space, displaced by its animation.
  resolved.rect    = {{node.rect.min.x + node.offset.x, node.rect.min.y + node.offset.y},
                      {node.rect.max.x + node.offset.x, node.rect.max.y + node.offset.y}};
  resolved.opacity = node.opacity;

  // Walk up, translating by each ancestor's origin and multiplying its opacity.
  // The root's rect is already in framebuffer pixels, so the chain simply ends.
  for (ui_node_id_t ancestor = node.parent; ancestor != UI_INVALID_NODE_ID;
       ancestor              = screen[ancestor].parent)
  {
    const ui_node_t &above  = screen[ancestor];
    const float      base_x = above.rect.min.x + above.offset.x;
    const float      base_y = above.rect.min.y + above.offset.y;

    resolved.rect.min.x += base_x;
    resolved.rect.min.y += base_y;
    resolved.rect.max.x += base_x;
    resolved.rect.max.y += base_y;
    resolved.opacity *= above.opacity;
  }

  return resolved;
}

uint32_t node_depth(const ui_screen_t &screen, ui_node_id_t id)
{
  if (id == UI_INVALID_NODE_ID || id >= screen.nodes.size())
    return 0;

  uint32_t depth = 0;
  for (ui_node_id_t ancestor = screen[id].parent; ancestor != UI_INVALID_NODE_ID;
       ancestor              = screen[ancestor].parent)
    ++depth;

  return depth;
}

void draw_screen(renderer::ui_draw_list_t &list, const ui_screen_t &screen, const ui_font_t &font)
{
  if (screen.root == UI_INVALID_NODE_ID)
    return;

  draw_subtree(list, screen, font, screen.root);
}

float property_value(const ui_node_t &node, ui_property_t property)
{
  switch (property)
  {
  case ui_property_t::opacity:
    return node.opacity;
  case ui_property_t::offset_x:
    return node.offset.x;
  case ui_property_t::offset_y:
    return node.offset.y;
  }

  return 0.0f;
}

void set_property_value(ui_node_t &node, ui_property_t property, float value)
{
  switch (property)
  {
  case ui_property_t::opacity:
    node.opacity = value;
    return;
  case ui_property_t::offset_x:
    node.offset.x = value;
    return;
  case ui_property_t::offset_y:
    node.offset.y = value;
    return;
  }
}

float apply_ease(ease_t easing, float t)
{
  // Clamped so a caller that oversteps the duration cannot overshoot the
  // endpoint -- cubic curves are not bounded outside [0,1].
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;

  const float inverted = 1.0f - t;

  switch (easing)
  {
  case ease_t::linear:
    return t;
  case ease_t::in_cubic:
    return t * t * t;
  case ease_t::out_cubic:
    return 1.0f - inverted * inverted * inverted;
  case ease_t::in_out_cubic:
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * inverted * inverted * inverted;
  }

  return t;
}

ui_animation_builder_t animate(ui_screen_t &screen, ui_node_id_t node, ui_property_t property)
{
  if (node == UI_INVALID_NODE_ID || node >= screen.nodes.size())
    fatal_error("[ui] animate: node {} does not exist ({} nodes)", node, screen.nodes.size());

  // Replace rather than stack. Note this shifts the indices of later entries,
  // so a ui_animation_builder_t is only valid until the next animate() call --
  // which is exactly how it is used, as a temporary in one chained expression.
  for (uint32_t index = 0; index < screen.animations.size(); ++index)
  {
    if (screen.animations[index].node == node && screen.animations[index].property == property)
    {
      screen.animations.erase(screen.animations.begin() + index);
      break;
    }
  }

  const float current = property_value(screen[node], property);

  ui_animation_t animation;
  animation.node     = node;
  animation.property = property;
  animation.from     = current;
  animation.to       = current;
  animation.duration = 0.2f;
  animation.easing   = ease_t::linear;

  screen.animations.push_back(animation);
  return ui_animation_builder_t(screen, (uint32_t)screen.animations.size() - 1);
}

void advance_animations(ui_screen_t &screen, float delta_seconds)
{
  uint32_t surviving = 0;

  for (uint32_t index = 0; index < screen.animations.size(); ++index)
  {
    ui_animation_t &animation = screen.animations[index];

    // Unreachable while screens are replaced as values: nodes and animations are
    // never separately alive. Kept because `nodes` is public, and shrinking it
    // behind the animations' back is a caller bug.
    if (animation.node >= screen.nodes.size())
      fatal_error("[ui] animation targets node {} which does not exist ({} nodes) -- the screen's "
                  "nodes were mutated without its animations",
                  animation.node, screen.nodes.size());

    animation.elapsed += delta_seconds;

    const bool finished = animation.duration <= 0.0f || animation.elapsed >= animation.duration;
    if (finished)
    {
      // Land on the exact endpoint. Letting the last frame's fraction decide
      // leaves a fade at 0.998 and a slide a pixel short, both of which are
      // visible and neither of which any later frame corrects.
      set_property_value(screen[animation.node], animation.property, animation.to);
      continue;
    }

    const float fraction = apply_ease(animation.easing, animation.elapsed / animation.duration);
    set_property_value(screen[animation.node], animation.property,
                       animation.from + (animation.to - animation.from) * fraction);

    screen.animations[surviving++] = animation;
  }

  screen.animations.resize(surviving);
}

} // namespace client::ui
