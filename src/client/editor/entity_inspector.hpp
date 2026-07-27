#pragma once

#include "../../shared/entities/entity_reflection.hpp"

namespace client
{

// Renders every @Editable field of `entity` as ImGui widgets, in declaration
// order. Components are flattened into dotted leaves ("render.material.color"),
// and enum / asset fields render as a Combo over the closed set the generator
// knows -- there is no free-form text box that can name a value that does not
// exist.
void render_imgui_entity_fields_in_a_window(entities::Entity *entity);

} // namespace client
