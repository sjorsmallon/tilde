#pragma once

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/map_connection.hpp"
#include "uid_pick.hpp"

#include <optional>
#include <string>

namespace client
{

// Draws the @Editable fields of entities[0] and copies a leaf edited here onto
// the rest, which must share its type; a multi-edit leaves out position.
// Returns the leaf edited this frame.
std::optional<std::string> render_entity_fields_in_an_imgui_window(
    Span<entities::Entity* const> entities,
    shared::entity_uid_t uid = shared::null_entity_uid,
    const shared::map_t* map = nullptr,
    uid_pick_t* pick = nullptr);

bool render_field_widget(
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
