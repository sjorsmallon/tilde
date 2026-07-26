#include "particle_editor_tool.hpp"
#include "../entity_inspector.hpp"
#include "../../../shared/entities/particle_emitter_entity.hpp"
#include "../../renderer.hpp"
#include "imgui.h"

namespace client
{

void Particle_Editor_Tool::on_enable(editor_context_t &ctx)
{
  selected_emitter_uid = shared::invalid_entity_uid;

  // Auto-select the first particle emitter if there is one
  if (ctx.map)
  {
    for (auto [uid, emitter] : ctx.map->entities_of_type<network::Particle_Emitter_Entity>())
    {
      selected_emitter_uid = uid;
      break;
    }
  }
}

void Particle_Editor_Tool::on_disable(editor_context_t &) {}

void Particle_Editor_Tool::on_update(editor_context_t &ctx,
                                    const viewport_state_t &view, float /*dt*/)
{
  viewport = view;
}

void Particle_Editor_Tool::on_mouse_down(editor_context_t &ctx,
                                        const input::mouse_event_t &e)
{
  if (e.button != input::mouse_button_t::Left)
    return;

  // Raycast to pick a particle emitter
  if (!ctx.bvh)
    return;

  // Just find the closest particle emitter to the ray
  float best_dist = 1e18f;
  shared::entity_uid_t best_uid = shared::invalid_entity_uid;

  for (auto [uid, emitter] : ctx.map->entities_of_type<network::Particle_Emitter_Entity>())
  {
    // Simple sphere pick test (particle emitters are point-like)
    linalg::vec3 to_emitter = emitter->position - viewport.mouse_ray.origin;
    float t = linalg::dot(to_emitter, viewport.mouse_ray.direction);
    if (t < 0.f)
      continue;

    linalg::vec3 closest = viewport.mouse_ray.origin + viewport.mouse_ray.direction * t;
    linalg::vec3 delta = emitter->position - closest;
    float dist = linalg::length(delta);

    // 32-unit pick radius
    if (dist < 32.f && t < best_dist)
    {
      best_dist = t;
      best_uid = uid;
    }
  }

  if (best_uid != shared::invalid_entity_uid)
    selected_emitter_uid = best_uid;
}

void Particle_Editor_Tool::on_mouse_drag(editor_context_t &, const input::mouse_event_t &) {}
void Particle_Editor_Tool::on_mouse_up(editor_context_t &, const input::mouse_event_t &) {}
void Particle_Editor_Tool::on_key_down(editor_context_t &, const key_event_t &) {}

void Particle_Editor_Tool::on_draw_overlay(editor_context_t &ctx,
                                          overlay_renderer_t &renderer)
{
  // Highlight all particle emitters with a circle, selected one brighter
  for (auto [uid, emitter] : ctx.map->entities_of_type<network::Particle_Emitter_Entity>())
  {
    color_t color = (uid == selected_emitter_uid) ? colors::yellow : color_t{128, 128, 0};
    renderer.draw_circle(emitter->position, 16.f, {0, 1, 0}, color);
    renderer.draw_wire_box(emitter->position, {4, 4, 4}, color);
  }
}

void Particle_Editor_Tool::on_draw_ui(editor_context_t &ctx)
{
  ImGui::Begin("Particle Editor", nullptr, ImGuiWindowFlags_NoNav);

  // Emitter selector dropdown
  if (ImGui::BeginCombo("Emitter",
                         selected_emitter_uid ? "Selected" : "None"))
  {
    for (auto [uid, emitter] : ctx.map->entities_of_type<network::Particle_Emitter_Entity>())
    {
      char label[64];
      snprintf(label, sizeof(label), "Emitter #%u (%.0f, %.0f, %.0f)",
               uid, emitter->position.x, emitter->position.y, emitter->position.z);

      bool is_selected = (uid == selected_emitter_uid);
      if (ImGui::Selectable(label, is_selected))
        selected_emitter_uid = uid;
    }
    ImGui::EndCombo();
  }

  if (ImGui::Button("New Emitter"))
  {
    auto emitter = std::make_shared<network::Particle_Emitter_Entity>();
    // Place at camera position
    emitter->position = {viewport.camera.position.x, viewport.camera.position.y,
                         viewport.camera.position.z};
    selected_emitter_uid = ctx.map->add_entity(emitter);
    if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
      *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
  }

  ImGui::Separator();

  // Find selected emitter
  network::Particle_Emitter_Entity *pe = nullptr;
  if (selected_emitter_uid != 0)
  {
    auto *entry = ctx.map->find_by_uid(selected_emitter_uid);
    if (entry)
      pe = shared::entity_as<network::Particle_Emitter_Entity>(entry->entity.get());
  }

  if (!pe)
  {
    ImGui::Text("No particle emitter selected.");
    ImGui::Text("Click an emitter or create one.");
    ImGui::End();
    return;
  }

  // Use the schema-based inspector for all fields
  render_imgui_entity_fields_in_a_window(pe);

  ImGui::Separator();
  ImGui::Text("Quick Presets");

  if (ImGui::Button("Smoke"))
  {
    pe->emit_rate = 15.0f;
    pe->max_particles = 64;
    pe->lifetime_min = 1.0f;
    pe->lifetime_max = 3.0f;
    pe->velocity_min = 2.0f;
    pe->velocity_max = 5.0f;
    pe->spread = 0.5f;
    pe->gravity = {0, 0.5f, 0};
    pe->drag = 0.3f;
    pe->size_start = 2.0f;
    pe->size_end = 8.0f;
    pe->rotation_speed_min = -1.0f;
    pe->rotation_speed_max = 1.0f;
    pe->color_start = {0.6f, 0.6f, 0.6f};
    pe->color_end = {0.3f, 0.3f, 0.3f};
    pe->alpha_start = 0.6f;
    pe->alpha_end = 0.0f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Fire"))
  {
    pe->emit_rate = 40.0f;
    pe->max_particles = 128;
    pe->lifetime_min = 0.2f;
    pe->lifetime_max = 0.6f;
    pe->velocity_min = 10.0f;
    pe->velocity_max = 30.0f;
    pe->spread = 0.8f;
    pe->gravity = {0, 2.0f, 0};
    pe->drag = 0.5f;
    pe->size_start = 1.0f;
    pe->size_end = 4.0f;
    pe->rotation_speed_min = -2.0f;
    pe->rotation_speed_max = 2.0f;
    pe->color_start = {1.0f, 0.7f, 0.1f};
    pe->color_end = {0.8f, 0.2f, 0.0f};
    pe->alpha_start = 0.9f;
    pe->alpha_end = 0.0f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Explosion"))
  {
    pe->emit_rate = 200.0f;
    pe->max_particles = 48;
    pe->lifetime_min = 0.3f;
    pe->lifetime_max = 0.8f;
    pe->velocity_min = 40.0f;
    pe->velocity_max = 120.0f;
    pe->spread = 2.0f;
    pe->gravity = {0, -20.0f, 0};
    pe->drag = 1.5f;
    pe->size_start = 3.0f;
    pe->size_end = 8.0f;
    pe->rotation_speed_min = -3.0f;
    pe->rotation_speed_max = 3.0f;
    pe->color_start = {1.0f, 0.8f, 0.3f};
    pe->color_end = {0.4f, 0.4f, 0.4f};
    pe->alpha_start = 0.9f;
    pe->alpha_end = 0.0f;
  }

  if (ImGui::Button("Sparks"))
  {
    pe->emit_rate = 30.0f;
    pe->max_particles = 64;
    pe->lifetime_min = 0.1f;
    pe->lifetime_max = 0.4f;
    pe->velocity_min = 50.0f;
    pe->velocity_max = 150.0f;
    pe->spread = 1.5f;
    pe->gravity = {0, -80.0f, 0};
    pe->drag = 0.2f;
    pe->size_start = 0.5f;
    pe->size_end = 0.2f;
    pe->rotation_speed_min = 0.0f;
    pe->rotation_speed_max = 0.0f;
    pe->color_start = {1.0f, 0.9f, 0.5f};
    pe->color_end = {1.0f, 0.4f, 0.0f};
    pe->alpha_start = 1.0f;
    pe->alpha_end = 0.0f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Steam"))
  {
    pe->emit_rate = 25.0f;
    pe->max_particles = 96;
    pe->lifetime_min = 0.5f;
    pe->lifetime_max = 2.0f;
    pe->velocity_min = 5.0f;
    pe->velocity_max = 15.0f;
    pe->spread = 0.3f;
    pe->gravity = {0, 3.0f, 0};
    pe->drag = 0.8f;
    pe->size_start = 1.0f;
    pe->size_end = 6.0f;
    pe->rotation_speed_min = -0.5f;
    pe->rotation_speed_max = 0.5f;
    pe->color_start = {1.0f, 1.0f, 1.0f};
    pe->color_end = {0.9f, 0.9f, 0.9f};
    pe->alpha_start = 0.4f;
    pe->alpha_end = 0.0f;
  }

  ImGui::Separator();
  if (ImGui::Button("Delete Emitter"))
  {
    ctx.map->remove_entity(selected_emitter_uid);
    selected_emitter_uid = 0;
    if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
      *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
  }

  ImGui::End();
}

} // namespace client
