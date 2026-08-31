#pragma once

#include "../../shared/entities/entity_reflection.hpp"

namespace client
{


void render_entity_fields_in_an_imgui_window(entities::Entity *entity);
// rotation is stored as quaternions, but it's nice to audit using euler angles.
bool edit_rotation_as_euler(const char *label, linalg::quatf &rotation);

} // namespace client
