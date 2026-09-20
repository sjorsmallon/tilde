#pragma once

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/map_connection.hpp"
#include "uid_pick.hpp"

#include <optional>

namespace client
{

void render_entity_fields_in_an_imgui_window(
    entities::Entity* entity,
    shared::entity_uid_t uid = shared::null_entity_uid,
    const shared::map_t* map = nullptr,
    uid_pick_t *pick = nullptr);

void render_field_widget(
    void* field_bytes,
    const field_info_t &field,
    const char* label,
    int id,
    const shared::map_t* map = nullptr);


bool draw_entity_uid_combo(
    const shared::map_t& map,
    const char* label,
    shared::entity_uid_t& uid,
    std::optional<entities::entity_action> annotate_refusal_of = std::nullopt);

// rotation is stored as quaternions, but it's nice to audit using euler angles.
bool edit_rotation_as_euler(const char *label, linalg::quatf &rotation);

} // namespace client
